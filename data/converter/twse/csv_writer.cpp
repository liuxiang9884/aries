#include "data/converter/twse/csv_writer.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

#include <fmt/format.h>

namespace aries::data::twse {
namespace {

constexpr std::string_view kDepthHeader =
    "symbol,symbol_id,exchtime,localtime,high_limit,low_limit,last_price,"
    "ask_price1,bid_price1,ask_price2,bid_price2,ask_price3,bid_price3,"
    "ask_price4,bid_price4,ask_price5,bid_price5,open,total_trade,"
    "total_volume,total_value,status,sequence\n";

constexpr std::string_view kBasicInfoHeader =
    "trading_day,market,symbol,industry_code,security_type,anomaly_code,"
    "stock_group_code,board_code,reference_price,high_limit,low_limit,"
    "abnormal_recommendation,special_abnormal,day_trading_code,"
    "margin_short_exempt,borrow_short_exempt,matching_cycle_seconds,"
    "warrant_flag,strike_price,previous_exercise_volume,"
    "previous_cancellation_volume,outstanding_volume,exercise_ratio,"
    "warrant_upper_price,warrant_lower_price,maturity_date,"
    "foreign_stock_flag,multiplier,currency,market_data_line\n";

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

class StagedCsvFile {
public:
  StagedCsvFile(const std::filesystem::path &requested_output_path,
                bool allow_overwrite, std::string_view header)
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

    file_buffer_.resize(4U * 1024U * 1024U);
    output_.rdbuf()->pubsetbuf(
        file_buffer_.data(), static_cast<std::streamsize>(file_buffer_.size()));
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

  ~StagedCsvFile() {
    output_.close();
    if (!published_) {
      RemovePartial();
    }
  }

  StagedCsvFile(const StagedCsvFile &) = delete;
  StagedCsvFile &operator=(const StagedCsvFile &) = delete;

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
    if (prepared_) {
      throw std::logic_error("CSV output was already prepared");
    }
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
  std::vector<char> file_buffer_;
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
  StagedCsvFile *file{};
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

} // namespace

struct DepthCsvWriter::Impl {
  Impl(const std::filesystem::path &output_path, bool overwrite)
      : file(output_path, overwrite, kDepthHeader) {}

  void Write(const DepthRecord &record) {
    row_buffer.clear();
    fmt::format_to(
        std::back_inserter(row_buffer),
        "{},-1,{},{},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},"
        "{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{},{},{:.2f},"
        "{},{}\n",
        record.symbol, record.exchtime, record.localtime, record.high_limit,
        record.low_limit, record.last_price, record.ask_price[0],
        record.bid_price[0], record.ask_price[1], record.bid_price[1],
        record.ask_price[2], record.bid_price[2], record.ask_price[3],
        record.bid_price[3], record.ask_price[4], record.bid_price[4],
        record.open, record.total_trade, record.total_volume,
        record.total_value, record.status, record.sequence);
    file.Write(std::string_view(row_buffer.data(), row_buffer.size()));
  }

  StagedCsvFile file;
  fmt::memory_buffer row_buffer;
};

struct BasicInfoCsvWriter::Impl {
  Impl(const std::filesystem::path &output_path, bool overwrite)
      : file(output_path, overwrite, kBasicInfoHeader) {}

  void Write(const BasicInfoRecord &record) {
    row_buffer.clear();
    fmt::format_to(
        std::back_inserter(row_buffer),
        "{},{},{},{},{},{},{},{},{:.4f},{:.4f},{:.4f},{},{},{},{},{},"
        "{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n",
        record.trading_day, record.market, record.symbol, record.industry_code,
        record.security_type, record.anomaly_code,
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
    file.Write(std::string_view(row_buffer.data(), row_buffer.size()));
  }

  StagedCsvFile file;
  fmt::memory_buffer row_buffer;
};

DepthCsvWriter::DepthCsvWriter(const std::filesystem::path &output_path,
                               bool overwrite)
    : impl_(std::make_unique<Impl>(output_path, overwrite)) {}

DepthCsvWriter::~DepthCsvWriter() = default;
DepthCsvWriter::DepthCsvWriter(DepthCsvWriter &&) noexcept = default;
DepthCsvWriter &DepthCsvWriter::operator=(DepthCsvWriter &&) noexcept = default;

void DepthCsvWriter::Write(const DepthRecord &record) { impl_->Write(record); }

BasicInfoCsvWriter::BasicInfoCsvWriter(const std::filesystem::path &output_path,
                                       bool overwrite)
    : impl_(std::make_unique<Impl>(output_path, overwrite)) {}

BasicInfoCsvWriter::~BasicInfoCsvWriter() = default;
BasicInfoCsvWriter::BasicInfoCsvWriter(BasicInfoCsvWriter &&) noexcept =
    default;
BasicInfoCsvWriter &
BasicInfoCsvWriter::operator=(BasicInfoCsvWriter &&) noexcept = default;

void BasicInfoCsvWriter::Write(const BasicInfoRecord &record) {
  impl_->Write(record);
}

void CsvOutputTransaction::Commit(DepthCsvWriter &depth_writer,
                                  BasicInfoCsvWriter &basic_info_writer) {
  std::array<PublishState, 2> states{{
      {.file = &depth_writer.impl_->file,
       .backup_path = {},
       .backed_up = false,
       .published = false},
      {.file = &basic_info_writer.impl_->file,
       .backup_path = {},
       .backed_up = false,
       .published = false},
  }};
  if (states[0].file->output_path() == states[1].file->output_path()) {
    throw std::invalid_argument("depth and basic-info outputs must differ");
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

} // namespace aries::data::twse
