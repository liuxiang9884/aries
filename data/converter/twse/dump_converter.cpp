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
    throw std::runtime_error("TWSE message has invalid checksum");
  }
}

} // namespace

ConvertStats ConvertDump(const ConvertOptions &options) {
  if (options.dump_path.empty()) {
    throw std::invalid_argument("dump path is empty");
  }
  if (!options.dry_run && options.output_path.empty()) {
    throw std::invalid_argument("output path is required without --dry-run");
  }

  MessageDecoder decoder(options.trading_day, options.symbol_filter_mode);
  std::unique_ptr<LegacyCsvWriter> writer;
  if (!options.dry_run) {
    writer = std::make_unique<LegacyCsvWriter>(options.output_path,
                                               options.overwrite);
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

    try {
      const auto header = DecodeMessageHeader(raw_header);
      const auto body_size = header.message_length - protocol::kHeaderSize;
      body.resize(body_size);
      input.read(reinterpret_cast<char *>(body.data()),
                 static_cast<std::streamsize>(body.size()));
      if (input.gcount() != static_cast<std::streamsize>(body.size())) {
        throw std::runtime_error("truncated TWSE message body");
      }
      ValidateFrame(raw_header, body);

      const auto *record = decoder.Process(header, body);
      ++stats.messages_read;
      if (record != nullptr) {
        ++stats.rows_written;
        if (writer != nullptr) {
          writer->Write(*record);
        }
      }
      offset += header.message_length;
      stats.bytes_read = offset;
    } catch (const std::exception &error) {
      throw std::runtime_error(
          fmt::format("TWSE message at byte {}: {}", offset, error.what()));
    }
  }

  stats.symbols_seen = decoder.symbol_count();
  if (writer != nullptr) {
    writer->Commit();
  }
  return stats;
}

} // namespace aries::data::twse
