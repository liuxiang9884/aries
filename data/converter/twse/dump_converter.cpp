#include "data/converter/twse/dump_converter.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "data/converter/twse/basic_info.h"
#include "data/converter/twse/csv_writer.h"
#include "data/converter/twse/protocol.h"

namespace aries::data::twse {
namespace {

void ValidateFrame(std::span<const std::uint8_t> raw_header,
                   std::span<const std::uint8_t> body) {
  if (body.size() < protocol::kMessageTrailerSize) {
    throw std::runtime_error("TWSE message does not contain a trailer");
  }
  if (body[body.size() - 2] != '\r' || body[body.size() - 1] != '\n') {
    throw std::runtime_error("TWSE message has invalid terminal code");
  }

  std::uint8_t checksum = 0;
  for (const auto byte : raw_header.subspan(1)) {
    checksum ^= byte;
  }
  for (const auto byte :
       body.first(body.size() - protocol::kMessageTrailerSize)) {
    checksum ^= byte;
  }
  if (checksum != body[body.size() - protocol::kMessageTrailerSize]) {
    throw std::runtime_error(fmt::format(
        "TWSE message has invalid checksum expected=0x{:02x} actual=0x{:02x}",
        checksum, body[body.size() - protocol::kMessageTrailerSize]));
  }
}

std::string HeaderContext(const MessageHeader &header) {
  return fmt::format("service={} format={} version={} sequence={}",
                     static_cast<unsigned>(header.service_type),
                     static_cast<unsigned>(header.message_type),
                     header.format_version, header.sequence);
}

} // namespace

ConvertStats ConvertDump(const ConvertOptions &options) {
  if (options.dump_path.empty()) {
    throw std::invalid_argument("dump path is empty");
  }
  if (!options.dry_run && options.output_path.empty()) {
    throw std::invalid_argument("output path is required without --dry-run");
  }
  if (!options.dry_run && options.basic_output_path.empty()) {
    throw std::invalid_argument(
        "basic-info output path is required without --dry-run");
  }
  if (!options.dry_run && options.output_path.lexically_normal() ==
                              options.basic_output_path.lexically_normal()) {
    throw std::invalid_argument("depth and basic-info outputs must differ");
  }

  MessageDecoder decoder(options.trading_day, options.symbol_filter_mode);
  BasicInfoCatalog basic_info_catalog(options.trading_day);
  std::unique_ptr<DepthCsvWriter> depth_writer;
  std::unique_ptr<BasicInfoCsvWriter> basic_info_writer;
  if (!options.dry_run) {
    depth_writer = std::make_unique<DepthCsvWriter>(options.output_path,
                                                    options.overwrite);
    basic_info_writer = std::make_unique<BasicInfoCsvWriter>(
        options.basic_output_path, options.overwrite);
  }

  std::ifstream input;
  std::vector<char> input_buffer(4U * 1024U * 1024U);
  input.rdbuf()->pubsetbuf(input_buffer.data(),
                           static_cast<std::streamsize>(input_buffer.size()));
  input.open(options.dump_path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error("failed to open TWSE dump");
  }

  ConvertStats stats;
  std::array<std::uint8_t, protocol::kHeaderSize> raw_header{};
  std::vector<std::uint8_t> body;
  std::uint64_t offset = 0;
  while (true) {
    input.read(reinterpret_cast<char *>(raw_header.data()),
               static_cast<std::streamsize>(raw_header.size()));
    const auto header_bytes = input.gcount();
    if (header_bytes == 0 && input.eof()) {
      break;
    }
    if (header_bytes != static_cast<std::streamsize>(raw_header.size())) {
      throw std::runtime_error(
          fmt::format("truncated TWSE header at byte {}", offset));
    }

    MessageHeader header;
    try {
      header = DecodeMessageHeader(raw_header);
    } catch (const std::exception &error) {
      throw std::runtime_error(
          fmt::format("TWSE message at byte {}: {}", offset, error.what()));
    }

    try {
      const auto body_size = header.message_length - protocol::kHeaderSize;
      body.resize(body_size);
      input.read(reinterpret_cast<char *>(body.data()),
                 static_cast<std::streamsize>(body.size()));
      if (input.gcount() != static_cast<std::streamsize>(body.size())) {
        throw std::runtime_error(
            fmt::format("truncated TWSE message body expected={} actual={}",
                        body.size(), input.gcount()));
      }
      ValidateFrame(raw_header, body);

      const DepthRecord *record = nullptr;
      if (header.message_type == MessageType::kStockBasicInfo) {
        const auto *basic_info =
            basic_info_catalog.Process(header, body, offset);
        if (basic_info != nullptr &&
            options.symbol_filter_mode != SymbolFilterMode::kOddLot) {
          decoder.ApplyBasicInfo(*basic_info);
        }
      } else {
        record = decoder.Process(header, body);
      }
      ++stats.messages_read;
      if (record != nullptr) {
        ++stats.rows_written;
        if (depth_writer != nullptr) {
          depth_writer->Write(*record);
        }
      }
      offset += header.message_length;
      stats.bytes_read = offset;
    } catch (const std::exception &error) {
      throw std::runtime_error(fmt::format("TWSE message at byte {} {}: {}",
                                           offset, HeaderContext(header),
                                           error.what()));
    }
  }

  stats.symbols_seen = decoder.symbol_count();
  stats.basic_info_messages = basic_info_catalog.normal_messages();
  stats.basic_info_controls = basic_info_catalog.control_records();
  stats.basic_info_duplicates = basic_info_catalog.identical_duplicates();
  const auto basic_info_records = basic_info_catalog.records();
  stats.basic_info_rows = basic_info_records.size();
  if (basic_info_writer != nullptr) {
    for (const auto &record : basic_info_records) {
      basic_info_writer->Write(record);
    }
    CsvOutputTransaction::Commit(*depth_writer, *basic_info_writer);
  }
  return stats;
}

} // namespace aries::data::twse
