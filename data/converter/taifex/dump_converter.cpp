#include "data/converter/taifex/dump_converter.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "data/converter/taifex/csv_writer.h"
#include "data/converter/taifex/message_decoder.h"
#include "data/converter/taifex/protocol.h"

namespace aries::data::taifex {
namespace {

void ValidateFrame(std::span<const std::uint8_t> header,
                   std::span<const std::uint8_t> body,
                   std::span<const std::uint8_t> trailer) {
  if (trailer.size() != protocol::kTrailerSize || trailer[1] != '\r' ||
      trailer[2] != '\n') {
    throw std::runtime_error("TAIFEX message has invalid terminal code");
  }
  std::uint8_t checksum = 0;
  for (const auto byte : header.subspan(1)) {
    checksum ^= byte;
  }
  for (const auto byte : body) {
    checksum ^= byte;
  }
  if (checksum != trailer[0]) {
    throw std::runtime_error(fmt::format(
        "TAIFEX message has invalid checksum expected=0x{:02x} actual=0x{:02x}",
        checksum, trailer[0]));
  }
}

std::string HeaderContext(const MessageHeader &header) {
  return fmt::format(
      "transmission={} kind={} version={} channel={} sequence={}",
      header.transmission_code, header.message_kind, header.version,
      header.channel_id, header.channel_sequence);
}

} // namespace

ConvertStats ConvertDump(const ConvertOptions &options) {
  if (options.dump_path.empty()) {
    throw std::invalid_argument("TAIFEX dump path is empty");
  }
  if (!options.dry_run && options.output_path.empty()) {
    throw std::invalid_argument(
        "orderbook output is required without --dry-run");
  }
  if (!options.dry_run && options.basic_output_path.empty()) {
    throw std::invalid_argument(
        "basic-info output is required without --dry-run");
  }
  if (!options.dry_run && options.output_path.lexically_normal() ==
                              options.basic_output_path.lexically_normal()) {
    throw std::invalid_argument("orderbook and basic-info outputs must differ");
  }

  MessageDecoder decoder(options.trading_day);
  std::unique_ptr<CsvWriter> writer;
  if (!options.dry_run) {
    writer = std::make_unique<CsvWriter>(
        options.output_path, options.basic_output_path, options.overwrite);
  }

  std::ifstream input;
  std::vector<char> input_buffer(4U * 1024U * 1024U);
  input.rdbuf()->pubsetbuf(input_buffer.data(),
                           static_cast<std::streamsize>(input_buffer.size()));
  input.open(options.dump_path, std::ios::binary);
  if (!input.is_open()) {
    throw std::runtime_error(fmt::format("failed to open TAIFEX dump: {}",
                                         options.dump_path.string()));
  }

  ConvertStats stats;
  std::array<std::uint8_t, protocol::kHeaderSize> raw_header{};
  std::array<std::uint8_t, protocol::kTrailerSize> trailer{};
  std::vector<std::uint8_t> body;
  std::uint64_t offset = 0;
  const auto emit = [&](const Orderbook<5> &record) {
    ++stats.rows_written;
    if (writer != nullptr) {
      writer->WriteOrderbook(record);
    }
  };
  while (true) {
    input.read(reinterpret_cast<char *>(raw_header.data()),
               static_cast<std::streamsize>(raw_header.size()));
    const auto header_bytes = input.gcount();
    if (header_bytes == 0 && input.eof()) {
      break;
    }
    if (header_bytes != static_cast<std::streamsize>(raw_header.size())) {
      throw std::runtime_error(
          fmt::format("truncated TAIFEX header at byte {}", offset));
    }

    MessageHeader header;
    try {
      header = DecodeMessageHeader(raw_header);
    } catch (const std::exception &error) {
      throw std::runtime_error(
          fmt::format("TAIFEX message at byte {}: {}", offset, error.what()));
    }
    if (header.exchange_time_ns >=
        protocol::kDaySessionEndExclusiveNanoseconds) {
      stats.day_session_cutoff_reached = true;
      stats.day_session_cutoff_offset = offset;
      break;
    }
    try {
      body.resize(header.body_length);
      input.read(reinterpret_cast<char *>(body.data()),
                 static_cast<std::streamsize>(body.size()));
      if (input.gcount() != static_cast<std::streamsize>(body.size())) {
        throw std::runtime_error(
            fmt::format("truncated body expected={} actual={}", body.size(),
                        input.gcount()));
      }
      input.read(reinterpret_cast<char *>(trailer.data()),
                 static_cast<std::streamsize>(trailer.size()));
      if (input.gcount() != static_cast<std::streamsize>(trailer.size())) {
        throw std::runtime_error("truncated message trailer");
      }
      ValidateFrame(raw_header, body, trailer);
      decoder.Process(header, body, emit);
      ++stats.messages_read;
      offset += protocol::kHeaderSize + body.size() + trailer.size();
      stats.bytes_read = offset;
    } catch (const std::exception &error) {
      throw std::runtime_error(fmt::format("TAIFEX message at byte {} {}: {}",
                                           offset, HeaderContext(header),
                                           error.what()));
    }
  }

  decoder.Finalize();
  const auto basic_records = decoder.BasicInfoRecords();
  stats.symbols_seen = decoder.symbol_count();
  stats.basic_info_rows = basic_records.size();
  const auto &decoder_stats = decoder.stats();
  stats.product_basic_messages = decoder_stats.product_basic_messages;
  stats.contract_basic_messages = decoder_stats.contract_basic_messages;
  stats.basic_duplicates = decoder_stats.identical_basic_duplicates;
  stats.price_limit_messages = decoder_stats.price_limit_messages;
  stats.price_limit_duplicates = decoder_stats.identical_price_limit_duplicates;
  stats.price_limit_conflicts = decoder_stats.price_limit_conflicts;
  stats.ignored_messages = decoder_stats.ignored_messages;
  stats.metadata_missing_messages = decoder_stats.metadata_missing_messages;
  stats.sequence_gaps = decoder_stats.sequence_gaps;
  stats.stale_messages = decoder_stats.stale_messages;
  stats.snapshot_recoveries = decoder_stats.snapshot_recoveries;
  stats.unresolved_sequence_gaps = decoder_stats.unresolved_sequence_gaps;
  stats.gap_cache_overflows = decoder_stats.gap_cache_overflows;
  stats.reset_messages = decoder_stats.reset_messages;
  stats.issues = decoder.issues();
  stats.ignored_message_counts = decoder.IgnoredMessageCounts();
  if (writer != nullptr) {
    writer->Commit(basic_records);
  }
  return stats;
}

} // namespace aries::data::taifex
