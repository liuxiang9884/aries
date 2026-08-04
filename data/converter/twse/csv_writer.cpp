#include "data/converter/twse/csv_writer.h"

#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <fmt/format.h>
#include <quill/CsvWriter.h>

#include "nova/utils/log.h"

namespace aries::data::twse {
namespace {

struct OrderbookCsvSchema {
  static constexpr char const *header =
      "symbol,market,exchange_ns,local_ns,disclosure,limit_state,"
      "session_state,trade_side,trade_count,last_price,open,high,low,"
      "trade_volume,total_volume,total_value,ask_price1,ask_volume1,"
      "ask_price2,ask_volume2,ask_price3,ask_volume3,ask_price4,"
      "ask_volume4,ask_price5,ask_volume5,bid_price1,bid_volume1,"
      "bid_price2,bid_volume2,bid_price3,bid_volume3,bid_price4,"
      "bid_volume4,bid_price5,bid_volume5,source_sequence";
  static constexpr char const *format =
      "{},{},{},{},{},{},{},{},{},{:.4f},{:.4f},{:.4f},{:.4f},{},{},"
      "{:.4f},{:.4f},{},{:.4f},{},{:.4f},{},{:.4f},{},{:.4f},{},"
      "{:.4f},{},{:.4f},{},{:.4f},{},{:.4f},{},{:.4f},{},{}";
};

struct BasicInfoCsvSchema {
  static constexpr char const *header =
      "trading_day,market,symbol,industry_code,security_type,anomaly_code,"
      "stock_group_code,board_code,reference_price,high_limit,low_limit,"
      "abnormal_recommendation,special_abnormal,day_trading_code,"
      "margin_short_exempt,borrow_short_exempt,matching_cycle_seconds,"
      "warrant_flag,strike_price,previous_exercise_volume,"
      "previous_cancellation_volume,outstanding_volume,exercise_ratio,"
      "warrant_upper_price,warrant_lower_price,maturity_date,"
      "foreign_stock_flag,multiplier,currency,market_data_line";
  static constexpr char const *format =
      "{},{},{},{},{},{},{},{},{:.4f},{:.4f},{:.4f},{},{},{},{},{},"
      "{},{},{},{},{},{},{},{},{},{},{},{},{},{}";
};

bool DirectoryEntryExists(const std::filesystem::path &path) {
  return std::filesystem::symlink_status(path).type() !=
         std::filesystem::file_type::not_found;
}

void RequireReplaceableOutput(const std::filesystem::path &path) {
  const auto type = std::filesystem::symlink_status(path).type();
  if (type != std::filesystem::file_type::not_found &&
      type != std::filesystem::file_type::regular &&
      type != std::filesystem::file_type::symlink) {
    throw std::runtime_error(fmt::format(
        "CSV output is not a regular file or symlink: {}", path.string()));
  }
}

class StagedCsvFileBase {
 public:
  StagedCsvFileBase(const std::filesystem::path &requested_output_path,
                    bool allow_overwrite)
      : output_path_(requested_output_path), overwrite_(allow_overwrite) {
    if (output_path_.empty()) {
      throw std::invalid_argument("CSV output path is empty");
    }
    RequireReplaceableOutput(output_path_);
    if (!overwrite_ && DirectoryEntryExists(output_path_)) {
      throw std::runtime_error(
          fmt::format("CSV output already exists: {}", output_path_.string()));
    }

    const auto parent = output_path_.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }
    partial_path_ = output_path_;
    partial_path_ += ".partial." + std::to_string(::getpid());
    if (DirectoryEntryExists(partial_path_)) {
      throw std::runtime_error(fmt::format(
          "CSV partial output already exists: {}", partial_path_.string()));
    }
  }

  virtual ~StagedCsvFileBase() {
    if (!published_) {
      RemovePartial();
    }
  }

  StagedCsvFileBase(const StagedCsvFileBase &) = delete;
  StagedCsvFileBase &operator=(const StagedCsvFileBase &) = delete;

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
class StagedCsvFile : public StagedCsvFileBase {
 public:
  using Writer =
      quill::CsvWriter<Schema, nova::LogManager::NovaFrontendOptions>;

  StagedCsvFile(const std::filesystem::path &output_path, bool overwrite)
      : StagedCsvFileBase(output_path, overwrite) {
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

  ~StagedCsvFile() {
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

std::string OptionalInteger(const std::optional<std::uint64_t> &value) {
  return value.has_value() ? std::to_string(*value) : std::string{};
}

std::string OptionalFixed(const std::optional<double> &value, int digits) {
  return value.has_value() ? fmt::format("{:.{}f}", *value, digits)
                           : std::string{};
}

std::string OptionalDate(const std::optional<std::int32_t> &value) {
  if (!value.has_value()) {
    return {};
  }
  return fmt::format("{:04}-{:02}-{:02}", *value / 10'000, *value / 100 % 100,
                     *value % 100);
}

struct PublishState {
  StagedCsvFileBase *file{};
  std::filesystem::path backup_path;
  bool backed_up{};
  bool published{};
};

void Rename(const std::filesystem::path &from, const std::filesystem::path &to,
            std::string_view action) {
  if (std::rename(from.c_str(), to.c_str()) != 0) {
    throw std::system_error(errno, std::generic_category(),
                            fmt::format("{} from '{}' to '{}'", action,
                                        from.string(), to.string()));
  }
}

}  // namespace

struct OrderbookCsvWriter::Impl {
  Impl(const std::filesystem::path &output_path, bool overwrite)
      : file(output_path, overwrite) {}

  void Write(const Orderbook<5> &record) {
    file.Write(
        record.symbol, static_cast<unsigned>(record.market), record.exchange_ns,
        record.local_ns, static_cast<unsigned>(record.disclosure.value),
        static_cast<unsigned>(record.limit_state.value),
        static_cast<unsigned>(record.session_state.value),
        static_cast<unsigned>(record.trade_side), record.trade_count,
        record.last_price, record.open, record.high, record.low,
        record.trade_volume, record.total_volume, record.total_value,
        record.ask_price[0], record.ask_volume[0], record.ask_price[1],
        record.ask_volume[1], record.ask_price[2], record.ask_volume[2],
        record.ask_price[3], record.ask_volume[3], record.ask_price[4],
        record.ask_volume[4], record.bid_price[0], record.bid_volume[0],
        record.bid_price[1], record.bid_volume[1], record.bid_price[2],
        record.bid_volume[2], record.bid_price[3], record.bid_volume[3],
        record.bid_price[4], record.bid_volume[4], record.source_sequence);
  }

  StagedCsvFile<OrderbookCsvSchema> file;
};

struct BasicInfoCsvWriter::Impl {
  Impl(const std::filesystem::path &output_path, bool overwrite)
      : file(output_path, overwrite) {}

  void Write(const BasicInfoRecord &record) {
    file.Write(record.trading_day, record.market, record.symbol,
               record.industry_code, record.security_type, record.anomaly_code,
               record.stock_group_code.value_or(""), record.board_code,
               record.reference_price, record.high_limit, record.low_limit,
               record.abnormal_recommendation, record.special_abnormal,
               record.day_trading_code, record.margin_short_exempt,
               record.borrow_short_exempt, record.matching_cycle_seconds,
               record.warrant_flag, OptionalFixed(record.strike_price, 4),
               OptionalInteger(record.previous_exercise_volume),
               OptionalInteger(record.previous_cancellation_volume),
               OptionalInteger(record.outstanding_volume),
               OptionalFixed(record.exercise_ratio, 5),
               OptionalFixed(record.warrant_upper_price, 4),
               OptionalFixed(record.warrant_lower_price, 4),
               OptionalDate(record.maturity_date),
               record.foreign_stock_flag.value_or(""), record.multiplier,
               record.currency, record.market_data_line);
  }

  StagedCsvFile<BasicInfoCsvSchema> file;
};

OrderbookCsvWriter::OrderbookCsvWriter(const std::filesystem::path &output_path,
                                       bool overwrite)
    : impl_(std::make_unique<Impl>(output_path, overwrite)) {}

OrderbookCsvWriter::~OrderbookCsvWriter() = default;
OrderbookCsvWriter::OrderbookCsvWriter(OrderbookCsvWriter &&) noexcept =
    default;
OrderbookCsvWriter &OrderbookCsvWriter::operator=(
    OrderbookCsvWriter &&) noexcept = default;

void OrderbookCsvWriter::Write(const Orderbook<5> &record) {
  impl_->Write(record);
}

BasicInfoCsvWriter::BasicInfoCsvWriter(const std::filesystem::path &output_path,
                                       bool overwrite)
    : impl_(std::make_unique<Impl>(output_path, overwrite)) {}

BasicInfoCsvWriter::~BasicInfoCsvWriter() = default;
BasicInfoCsvWriter::BasicInfoCsvWriter(BasicInfoCsvWriter &&) noexcept =
    default;
BasicInfoCsvWriter &BasicInfoCsvWriter::operator=(
    BasicInfoCsvWriter &&) noexcept = default;

void BasicInfoCsvWriter::Write(const BasicInfoRecord &record) {
  impl_->Write(record);
}

void CsvOutputTransaction::Commit(OrderbookCsvWriter &orderbook_writer,
                                  BasicInfoCsvWriter &basic_info_writer) {
  std::array<PublishState, 2> states{{
      {.file = &orderbook_writer.impl_->file,
       .backup_path = {},
       .backed_up = false,
       .published = false},
      {.file = &basic_info_writer.impl_->file,
       .backup_path = {},
       .backed_up = false,
       .published = false},
  }};
  if (states[0].file->output_path() == states[1].file->output_path()) {
    throw std::invalid_argument("orderbook and basic-info outputs must differ");
  }

  for (auto &state : states) {
    RequireReplaceableOutput(state.file->output_path());
    state.file->Prepare();
    state.backup_path = state.file->output_path();
    state.backup_path += ".backup." + std::to_string(::getpid());
    if (DirectoryEntryExists(state.backup_path)) {
      throw std::runtime_error(fmt::format(
          "CSV backup output already exists: {}", state.backup_path.string()));
    }
    if (!state.file->overwrite() &&
        DirectoryEntryExists(state.file->output_path())) {
      throw std::runtime_error(
          fmt::format("CSV output appeared during conversion: {}",
                      state.file->output_path().string()));
    }
  }

  try {
    for (auto &state : states) {
      if (state.file->overwrite() &&
          DirectoryEntryExists(state.file->output_path())) {
        Rename(state.file->output_path(), state.backup_path,
               "failed to stage old CSV output");
        state.backed_up = true;
      }
    }
    for (auto &state : states) {
      Rename(state.file->partial_path(), state.file->output_path(),
             "failed to publish CSV output");
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
          Rename(iterator->backup_path, iterator->file->output_path(),
                 "failed to restore old CSV output");
        } catch (const std::exception &error_value) {
          if (rollback_error.empty()) {
            rollback_error = error_value.what();
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
  }
  for (const auto &state : states) {
    if (state.backed_up) {
      std::error_code error;
      std::filesystem::remove(state.backup_path, error);
    }
  }
}

}  // namespace aries::data::twse
