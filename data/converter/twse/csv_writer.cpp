#include "data/converter/twse/csv_writer.h"

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

#include <fmt/format.h>

namespace aries::data::twse {
namespace {

constexpr std::string_view kHeader =
    "symbol,symbol_id,exchtime,localtime,high_limit,low_limit,last_price,"
    "ask_price1,bid_price1,ask_price2,bid_price2,ask_price3,bid_price3,"
    "ask_price4,bid_price4,ask_price5,bid_price5,open,total_trade,"
    "total_volume,total_value,status,sequence\n";

bool DirectoryEntryExists(const std::filesystem::path &path) {
  return std::filesystem::symlink_status(path).type() !=
         std::filesystem::file_type::not_found;
}

} // namespace

struct LegacyCsvWriter::Impl {
  explicit Impl(const std::filesystem::path &requested_output_path,
                bool allow_overwrite)
      : output_path(requested_output_path), overwrite(allow_overwrite) {
    if (output_path.empty()) {
      throw std::invalid_argument("CSV output path is empty");
    }
    if (!overwrite && DirectoryEntryExists(output_path)) {
      throw std::runtime_error("CSV output already exists");
    }

    const auto parent = output_path.parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent);
    }
    partial_path = output_path;
    partial_path += ".partial." + std::to_string(::getpid());
    if (DirectoryEntryExists(partial_path)) {
      throw std::runtime_error("CSV partial output already exists");
    }

    file_buffer.resize(4U * 1024U * 1024U);
    output.rdbuf()->pubsetbuf(file_buffer.data(),
                              static_cast<std::streamsize>(file_buffer.size()));
    output.open(partial_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      std::error_code error;
      std::filesystem::remove(partial_path, error);
      throw std::runtime_error("failed to open CSV partial output");
    }
    output.write(kHeader.data(), static_cast<std::streamsize>(kHeader.size()));
    if (!output.good()) {
      output.close();
      std::error_code error;
      std::filesystem::remove(partial_path, error);
      throw std::runtime_error("failed to write CSV header");
    }
  }

  ~Impl() {
    if (committed) {
      return;
    }
    output.close();
    std::error_code error;
    std::filesystem::remove(partial_path, error);
  }

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
    output.write(row_buffer.data(),
                 static_cast<std::streamsize>(row_buffer.size()));
    if (!output.good()) {
      throw std::runtime_error("failed to write CSV output");
    }
  }

  void Commit() {
    if (committed) {
      throw std::logic_error("CSV output was already committed");
    }
    output.flush();
    if (!output.good()) {
      throw std::runtime_error("failed to flush CSV output");
    }
    output.close();
    if (output.fail()) {
      throw std::runtime_error("failed to close CSV output");
    }
    if (!overwrite && DirectoryEntryExists(output_path)) {
      throw std::runtime_error("CSV output appeared during conversion");
    }
    if (std::rename(partial_path.c_str(), output_path.c_str()) != 0) {
      throw std::system_error(errno, std::generic_category(),
                              "failed to publish CSV output");
    }
    committed = true;
  }

  std::filesystem::path output_path;
  std::filesystem::path partial_path;
  bool overwrite;
  bool committed{false};
  std::ofstream output;
  std::vector<char> file_buffer;
  fmt::memory_buffer row_buffer;
};

LegacyCsvWriter::LegacyCsvWriter(const std::filesystem::path &output_path,
                                 bool overwrite)
    : impl_(std::make_unique<Impl>(output_path, overwrite)) {}

LegacyCsvWriter::~LegacyCsvWriter() = default;
LegacyCsvWriter::LegacyCsvWriter(LegacyCsvWriter &&) noexcept = default;
LegacyCsvWriter &
LegacyCsvWriter::operator=(LegacyCsvWriter &&) noexcept = default;

void LegacyCsvWriter::Write(const DepthRecord &record) { impl_->Write(record); }

void LegacyCsvWriter::Commit() { impl_->Commit(); }

} // namespace aries::data::twse
