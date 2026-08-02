#include "data/converter/taifex/csv_writer.h"

#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <fmt/format.h>
#include <quill/CsvWriter.h>

#include "nova/utils/log.h"

namespace aries::data::taifex {
namespace {

struct OrderbookCsvSchema {
  static constexpr char const *header =
      "trading_day,market,symbol,symbol_id,exchtime,localtime,reference_price,"
      "open,high,low,last_price,trade_volume,total_volume,total_value,"
      "total_buy_count,total_sell_count,"
      "ask_price1,ask_volume1,bid_price1,bid_volume1,"
      "ask_price2,ask_volume2,bid_price2,bid_volume2,"
      "ask_price3,ask_volume3,bid_price3,bid_volume3,"
      "ask_price4,ask_volume4,bid_price4,bid_volume4,"
      "ask_price5,ask_volume5,bid_price5,bid_volume5,"
      "derived_ask_price,derived_ask_volume,derived_bid_price,"
      "derived_bid_volume,match_flag,build_type,orderbook_action,sequence";
  static constexpr char const *format =
      "{},{},{},{},{},{},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{},{},"
      "{:.6f},{},{},{:.6f},{},{:.6f},{},{:.6f},{},{:.6f},{},{:.6f},"
      "{},{:.6f},{},{:.6f},{},{:.6f},{},{:.6f},{},{:.6f},{},{:.6f},"
      "{},{:.6f},{},{},{},{},{}";
};

struct BasicInfoCsvSchema {
  static constexpr char const *header =
      "trading_day,market,symbol,kind_id,is_spread,basic_source,contract_type,"
      "contract_subtype,reference_price,decimal_locator,strike_decimal_locator,"
      "listing_date,delisting_date,delivery_date,flow_group,dynamic_banding,"
      "multiplier,currency_code,currency,stock_id,contract_status,quote_flag,"
      "block_trade_flag,expiry_type,underlying_type,close_group,end_session";
  static constexpr char const *format =
      "{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{:.4f},{},{},"
      "{},{},{},{},{},{},{},{}";
};

bool Exists(const std::filesystem::path &path) {
  return std::filesystem::symlink_status(path).type() !=
         std::filesystem::file_type::not_found;
}

void RequireReplaceable(const std::filesystem::path &path) {
  const auto type = std::filesystem::symlink_status(path).type();
  if (type != std::filesystem::file_type::not_found &&
      type != std::filesystem::file_type::regular &&
      type != std::filesystem::file_type::symlink) {
    throw std::runtime_error(fmt::format(
        "CSV output is not a regular file or symlink: {}", path.string()));
  }
}

void Rename(const std::filesystem::path &from, const std::filesystem::path &to,
            std::string_view action) {
  if (std::rename(from.c_str(), to.c_str()) != 0) {
    throw std::system_error(errno, std::generic_category(),
                            fmt::format("{} from '{}' to '{}'", action,
                                        from.string(), to.string()));
  }
}

class StagedFileBase {
 public:
  StagedFileBase(std::filesystem::path output_path, bool overwrite)
      : output_path_(std::move(output_path)), overwrite_(overwrite) {
    if (output_path_.empty()) {
      throw std::invalid_argument("CSV output path is empty");
    }
    RequireReplaceable(output_path_);
    if (!overwrite_ && Exists(output_path_)) {
      throw std::runtime_error(
          fmt::format("CSV output already exists: {}", output_path_.string()));
    }
    if (!output_path_.parent_path().empty()) {
      std::filesystem::create_directories(output_path_.parent_path());
    }
    partial_path_ = output_path_;
    partial_path_ += ".partial." + std::to_string(::getpid());
    if (Exists(partial_path_)) {
      throw std::runtime_error(fmt::format(
          "CSV partial output already exists: {}", partial_path_.string()));
    }
  }

  virtual ~StagedFileBase() {
    if (!published_) {
      RemovePartial();
    }
  }

  StagedFileBase(const StagedFileBase &) = delete;
  StagedFileBase &operator=(const StagedFileBase &) = delete;

  virtual void Prepare() = 0;

  void VerifyPreparedFile() {
    if (prepared_) {
      throw std::logic_error("CSV output was already prepared");
    }
    if (std::filesystem::symlink_status(partial_path_).type() !=
        std::filesystem::file_type::regular) {
      throw std::runtime_error(
          fmt::format("CSV partial output is not a regular file: {}",
                      partial_path_.string()));
    }
    if (std::filesystem::file_size(partial_path_) == 0) {
      throw std::runtime_error(fmt::format("CSV partial output is empty: {}",
                                           partial_path_.string()));
    }
    prepared_ = true;
  }

  void MarkPublished() noexcept {
    published_ = true;
  }
  [[nodiscard]] const std::filesystem::path &output_path() const noexcept {
    return output_path_;
  }
  [[nodiscard]] const std::filesystem::path &partial_path() const noexcept {
    return partial_path_;
  }
  [[nodiscard]] bool overwrite() const noexcept {
    return overwrite_;
  }

 protected:
  void RemovePartial() noexcept {
    std::error_code error;
    std::filesystem::remove(partial_path_, error);
  }

  std::filesystem::path output_path_;
  std::filesystem::path partial_path_;
  bool overwrite_{};
  bool prepared_{};
  bool published_{};
};

template <typename Schema>
class StagedFile : public StagedFileBase {
 public:
  using Writer =
      quill::CsvWriter<Schema, nova::LogManager::NovaFrontendOptions>;

  StagedFile(const std::filesystem::path &output_path, bool overwrite)
      : StagedFileBase(output_path, overwrite) {
    if (nova::kLogManager.logger() == nullptr ||
        !quill::Backend::is_running()) {
      throw std::logic_error(
          "Nova logging must be initialized before creating a CSV writer");
    }
    try {
      writer_ = std::make_unique<Writer>(partial_path_.string());
    } catch (...) {
      RemovePartial();
      throw;
    }
  }

  ~StagedFile() {
    writer_.reset();
  }

  template <typename... Args>
  void Write(Args &&...args) {
    if (prepared_) {
      throw std::logic_error("CSV output was already prepared");
    }
    writer_->append_row(std::forward<Args>(args)...);
  }

  void Prepare() override {
    if (prepared_) {
      throw std::logic_error("CSV output was already prepared");
    }
    writer_->flush();
    writer_.reset();
    VerifyPreparedFile();
  }

 private:
  std::unique_ptr<Writer> writer_;
};

std::string OptionalFixed(const std::optional<double> &value) {
  return value.has_value() ? fmt::format("{:.6f}", *value) : std::string{};
}

std::string OptionalInteger(const std::optional<std::uint8_t> &value) {
  return value.has_value() ? std::to_string(*value) : std::string{};
}

std::string OptionalDate(const std::optional<std::int32_t> &value) {
  if (!value.has_value()) {
    return {};
  }
  return fmt::format("{:04}-{:02}-{:02}", *value / 10'000, *value / 100 % 100,
                     *value % 100);
}

std::string OptionalChar(const std::optional<char> &value) {
  return value.has_value() ? std::string(1, *value) : std::string{};
}

struct PublishState {
  StagedFileBase *file{};
  std::filesystem::path backup;
  bool backed_up{};
  bool published{};
};

void PublishPair(StagedFileBase &first, StagedFileBase &second) {
  std::array<PublishState, 2> states{{
      {.file = &first, .backup = {}, .backed_up = false, .published = false},
      {.file = &second, .backup = {}, .backed_up = false, .published = false},
  }};
  for (auto &state : states) {
    RequireReplaceable(state.file->output_path());
    state.file->Prepare();
    state.backup = state.file->output_path();
    state.backup += ".backup." + std::to_string(::getpid());
    if (Exists(state.backup)) {
      throw std::runtime_error(
          fmt::format("CSV backup exists: {}", state.backup.string()));
    }
    if (!state.file->overwrite() && Exists(state.file->output_path())) {
      throw std::runtime_error(
          fmt::format("CSV output appeared during conversion: {}",
                      state.file->output_path().string()));
    }
  }
  try {
    for (auto &state : states) {
      if (state.file->overwrite() && Exists(state.file->output_path())) {
        Rename(state.file->output_path(), state.backup,
               "failed to stage existing CSV");
        state.backed_up = true;
      }
    }
    for (auto &state : states) {
      Rename(state.file->partial_path(), state.file->output_path(),
             "failed to publish CSV");
      state.published = true;
    }
  } catch (...) {
    std::string rollback_error;
    for (auto iterator = states.rbegin(); iterator != states.rend();
         ++iterator) {
      std::error_code error;
      if (iterator->published) {
        std::filesystem::remove(iterator->file->output_path(), error);
        if (error && rollback_error.empty()) {
          rollback_error = error.message();
        }
      }
      if (iterator->backed_up) {
        try {
          Rename(iterator->backup, iterator->file->output_path(),
                 "failed to restore existing CSV");
        } catch (const std::exception &restore_error) {
          if (rollback_error.empty()) {
            rollback_error = restore_error.what();
          }
        }
      }
    }
    if (!rollback_error.empty()) {
      throw std::runtime_error(
          fmt::format("CSV publication rollback failed: {}", rollback_error));
    }
    throw;
  }
  for (auto &state : states) {
    state.file->MarkPublished();
    if (state.backed_up) {
      std::error_code error;
      std::filesystem::remove(state.backup, error);
      if (error) {
        throw std::system_error(error, "failed to remove CSV backup");
      }
    }
  }
}

}  // namespace

struct CsvWriter::Impl {
  Impl(const std::filesystem::path &orderbook_path,
       const std::filesystem::path &basic_path, bool overwrite)
      : orderbook(orderbook_path, overwrite), basic(basic_path, overwrite) {
    if (orderbook_path.lexically_normal() == basic_path.lexically_normal()) {
      throw std::invalid_argument(
          "orderbook and basic-info outputs must differ");
    }
  }

  void WriteOrderbook(const Orderbook<5> &record) {
    orderbook.Write(
        record.trading_day, "TAIFEX", record.symbol, -1, record.exchtime,
        record.localtime, record.reference_price, record.open, record.high,
        record.low, record.last_price, record.trade_volume, record.total_volume,
        record.total_value, record.total_buy_count, record.total_sell_count,
        record.ask_price[0], record.ask_volume[0], record.bid_price[0],
        record.bid_volume[0], record.ask_price[1], record.ask_volume[1],
        record.bid_price[1], record.bid_volume[1], record.ask_price[2],
        record.ask_volume[2], record.bid_price[2], record.bid_volume[2],
        record.ask_price[3], record.ask_volume[3], record.bid_price[3],
        record.bid_volume[3], record.ask_price[4], record.ask_volume[4],
        record.bid_price[4], record.bid_volume[4], record.derived_ask_price,
        record.derived_ask_volume, record.derived_bid_price,
        record.derived_bid_volume, static_cast<unsigned>(record.match_flag),
        static_cast<unsigned>(record.build_type),
        static_cast<unsigned>(record.orderbook_action), record.sequence);
  }

  void WriteBasic(const BasicInfoRecord &record) {
    basic.Write(
        record.trading_day, "TAIFEX", record.symbol, record.kind_id,
        record.is_spread ? 1 : 0, record.basic_source, record.contract_type,
        record.contract_subtype, OptionalFixed(record.reference_price),
        record.decimal_locator, OptionalInteger(record.strike_decimal_locator),
        OptionalDate(record.listing_date), OptionalDate(record.delisting_date),
        OptionalDate(record.delivery_date), OptionalInteger(record.flow_group),
        OptionalChar(record.dynamic_banding), record.multiplier,
        record.currency_code, record.currency, record.stock_id,
        record.contract_status, record.quote_flag, record.block_trade_flag,
        record.expiry_type, record.underlying_type, record.close_group,
        record.end_session);
  }

  StagedFile<OrderbookCsvSchema> orderbook;
  StagedFile<BasicInfoCsvSchema> basic;
};

CsvWriter::CsvWriter(const std::filesystem::path &orderbook_path,
                     const std::filesystem::path &basic_path, bool overwrite)
    : impl_(std::make_unique<Impl>(orderbook_path, basic_path, overwrite)) {}

CsvWriter::~CsvWriter() = default;
CsvWriter::CsvWriter(CsvWriter &&) noexcept = default;
CsvWriter &CsvWriter::operator=(CsvWriter &&) noexcept = default;

void CsvWriter::WriteOrderbook(const Orderbook<5> &record) {
  impl_->WriteOrderbook(record);
}

void CsvWriter::Commit(std::span<const BasicInfoRecord> basic_records) {
  for (const auto &record : basic_records) {
    impl_->WriteBasic(record);
  }
  PublishPair(impl_->orderbook, impl_->basic);
}

}  // namespace aries::data::taifex
