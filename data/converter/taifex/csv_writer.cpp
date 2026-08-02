#include "data/converter/taifex/csv_writer.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

#include <fmt/format.h>

namespace aries::data::taifex {
namespace {

constexpr std::string_view kOrderbookHeader =
    "trading_day,market,symbol,symbol_id,exchtime,localtime,reference_price,"
    "open,high,low,last_price,trade_volume,total_volume,total_value,"
    "total_buy_count,total_sell_count,"
    "ask_price1,ask_volume1,bid_price1,bid_volume1,"
    "ask_price2,ask_volume2,bid_price2,bid_volume2,"
    "ask_price3,ask_volume3,bid_price3,bid_volume3,"
    "ask_price4,ask_volume4,bid_price4,bid_volume4,"
    "ask_price5,ask_volume5,bid_price5,bid_volume5,"
    "derived_ask_price,derived_ask_volume,derived_bid_price,derived_bid_volume,"
    "match_flag,build_type,orderbook_action,sequence\n";

constexpr std::string_view kBasicHeader =
    "trading_day,market,symbol,kind_id,is_spread,basic_source,contract_type,"
    "contract_subtype,reference_price,decimal_locator,strike_decimal_locator,"
    "listing_date,delisting_date,delivery_date,flow_group,dynamic_banding,"
    "multiplier,currency_code,currency,stock_id,contract_status,quote_flag,"
    "block_trade_flag,expiry_type,underlying_type,close_group,end_session\n";

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

class StagedFile {
public:
  StagedFile(std::filesystem::path output_path, bool overwrite,
             std::string_view header)
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
    buffer_.resize(4U * 1024U * 1024U);
    output_.rdbuf()->pubsetbuf(buffer_.data(),
                               static_cast<std::streamsize>(buffer_.size()));
    output_.open(partial_path_, std::ios::binary | std::ios::trunc);
    if (!output_.is_open()) {
      RemovePartial();
      throw std::runtime_error(fmt::format(
          "failed to open CSV partial output: {}", partial_path_.string()));
    }
    try {
      Write(header);
    } catch (...) {
      output_.close();
      RemovePartial();
      throw;
    }
  }

  ~StagedFile() {
    output_.close();
    if (!published_) {
      RemovePartial();
    }
  }

  StagedFile(const StagedFile &) = delete;
  StagedFile &operator=(const StagedFile &) = delete;

  void Write(std::string_view bytes) {
    if (prepared_) {
      throw std::logic_error("CSV output was already prepared");
    }
    output_.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!output_.good()) {
      throw std::runtime_error(fmt::format("failed to write CSV output: {}",
                                           partial_path_.string()));
    }
  }

  void Prepare() {
    output_.flush();
    if (!output_.good()) {
      throw std::runtime_error(fmt::format("failed to flush CSV output: {}",
                                           partial_path_.string()));
    }
    output_.close();
    if (output_.fail()) {
      throw std::runtime_error(fmt::format("failed to close CSV output: {}",
                                           partial_path_.string()));
    }
    prepared_ = true;
  }

  void MarkPublished() noexcept { published_ = true; }
  [[nodiscard]] const std::filesystem::path &output_path() const noexcept {
    return output_path_;
  }
  [[nodiscard]] const std::filesystem::path &partial_path() const noexcept {
    return partial_path_;
  }
  [[nodiscard]] bool overwrite() const noexcept { return overwrite_; }

private:
  void RemovePartial() noexcept {
    std::error_code error;
    std::filesystem::remove(partial_path_, error);
  }

  std::filesystem::path output_path_;
  std::filesystem::path partial_path_;
  bool overwrite_{};
  bool prepared_{};
  bool published_{};
  std::ofstream output_;
  std::vector<char> buffer_;
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
  StagedFile *file{};
  std::filesystem::path backup;
  bool backed_up{};
  bool published{};
};

void PublishPair(StagedFile &first, StagedFile &second) {
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

} // namespace

struct CsvWriter::Impl {
  Impl(const std::filesystem::path &orderbook_path,
       const std::filesystem::path &basic_path, bool overwrite)
      : orderbook(orderbook_path, overwrite, kOrderbookHeader),
        basic(basic_path, overwrite, kBasicHeader) {
    if (orderbook_path.lexically_normal() == basic_path.lexically_normal()) {
      throw std::invalid_argument(
          "orderbook and basic-info outputs must differ");
    }
  }

  void WriteOrderbook(const Orderbook<5> &record) {
    row.clear();
    fmt::format_to(
        std::back_inserter(row),
        "{},TAIFEX,{},-1,{},{},{:.6f},{:.6f},{:.6f},{:.6f},{:.6f},{},{},"
        "{:.6f},{},{},",
        record.trading_day, record.symbol, record.exchtime, record.localtime,
        record.reference_price, record.open, record.high, record.low,
        record.last_price, record.trade_volume, record.total_volume,
        record.total_value, record.total_buy_count, record.total_sell_count);
    for (std::size_t i = 0; i < 5; ++i) {
      fmt::format_to(std::back_inserter(row), "{:.6f},{},{:.6f},{},",
                     record.ask_price[i], record.ask_volume[i],
                     record.bid_price[i], record.bid_volume[i]);
    }
    fmt::format_to(std::back_inserter(row), "{:.6f},{},{:.6f},{},{},{},{},{}\n",
                   record.derived_ask_price, record.derived_ask_volume,
                   record.derived_bid_price, record.derived_bid_volume,
                   record.match_flag, record.build_type,
                   record.orderbook_action, record.sequence);
    orderbook.Write(std::string_view(row.data(), row.size()));
  }

  void WriteBasic(const BasicInfoRecord &record) {
    row.clear();
    fmt::format_to(
        std::back_inserter(row),
        "{},TAIFEX,{},{},{},{},{},{},{},{},{},{},{},{},{},{},{:.4f},{},{},"
        "{},{},{},{},{},{},{},{}\n",
        record.trading_day, record.symbol, record.kind_id,
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
    basic.Write(std::string_view(row.data(), row.size()));
  }

  StagedFile orderbook;
  StagedFile basic;
  fmt::memory_buffer row;
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

} // namespace aries::data::taifex
