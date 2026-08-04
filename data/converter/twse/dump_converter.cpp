#include "data/converter/twse/dump_converter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "data/converter/twse/basic_info.h"
#include "data/converter/twse/bcd_decoder.h"
#include "data/converter/twse/csv_writer.h"
#include "data/converter/twse/protocol.h"

namespace aries::data::twse {
namespace {

constexpr std::uint64_t kMaximumRecoveryScanBytes = 1024U * 1024U;
constexpr std::size_t kRecoveryScanBufferSize = 64U * 1024U;

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

void Seek(std::ifstream &input, std::uint64_t offset) {
  input.clear();
  input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
  if (!input.good()) {
    throw std::runtime_error(
        fmt::format("failed to seek TWSE dump to byte {}", offset));
  }
}

bool IsValidFrameAt(std::ifstream &input, std::uint64_t offset,
                    std::uint64_t file_size) {
  if (file_size - offset < protocol::kHeaderSize) {
    return false;
  }

  try {
    Seek(input, offset);
    std::array<std::uint8_t, protocol::kHeaderSize> raw_header{};
    input.read(reinterpret_cast<char *>(raw_header.data()),
               static_cast<std::streamsize>(raw_header.size()));
    if (input.gcount() != static_cast<std::streamsize>(raw_header.size())) {
      return false;
    }
    const auto header = DecodeMessageHeader(raw_header);
    if (header.message_length > file_size - offset) {
      return false;
    }
    std::vector<std::uint8_t> body(header.message_length -
                                   protocol::kHeaderSize);
    input.read(reinterpret_cast<char *>(body.data()),
               static_cast<std::streamsize>(body.size()));
    if (input.gcount() != static_cast<std::streamsize>(body.size())) {
      return false;
    }
    ValidateFrame(raw_header, body);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

std::optional<std::uint64_t> FindNextValidFrame(std::ifstream &input,
                                                std::uint64_t search_start,
                                                std::uint64_t file_size) {
  if (search_start >= file_size) {
    return std::nullopt;
  }
  const auto search_end =
      std::min(file_size, search_start + kMaximumRecoveryScanBytes);
  std::vector<std::uint8_t> scan_buffer(kRecoveryScanBufferSize);
  auto cursor = search_start;
  while (cursor < search_end) {
    Seek(input, cursor);
    const auto requested = static_cast<std::streamsize>(
        std::min<std::uint64_t>(scan_buffer.size(), search_end - cursor));
    input.read(reinterpret_cast<char *>(scan_buffer.data()), requested);
    const auto bytes_read = input.gcount();
    if (bytes_read <= 0) {
      break;
    }
    for (std::streamsize index = 0; index < bytes_read; ++index) {
      if (scan_buffer[static_cast<std::size_t>(index)] != protocol::kEscape) {
        continue;
      }
      const auto candidate = cursor + static_cast<std::uint64_t>(index);
      if (IsValidFrameAt(input, candidate, file_size)) {
        return candidate;
      }
    }
    cursor += static_cast<std::uint64_t>(bytes_read);
  }
  return std::nullopt;
}

bool HasSymbolPrefix(MessageType message_type) {
  switch (message_type) {
    case MessageType::kStockBasicInfo:
    case MessageType::kStockDepthV:
    case MessageType::kStockOddLotBasicInfo:
    case MessageType::kStockOddLotDepthV:
    case MessageType::kWarrantDepthV:
      return true;
    default:
      return false;
  }
}

bool HasDepthTime(MessageType message_type) {
  return message_type == MessageType::kStockDepthV ||
         message_type == MessageType::kWarrantDepthV ||
         message_type == MessageType::kStockOddLotDepthV;
}

bool IsEndControlSymbol(std::span<const std::uint8_t> body) {
  constexpr std::string_view kEndControlSymbol = "000000";
  return body.size() >= kEndControlSymbol.size() &&
         std::equal(kEndControlSymbol.begin(), kEndControlSymbol.end(),
                    body.begin());
}

std::optional<std::string> AffectedSymbol(
    const std::optional<MessageHeader> &header,
    std::span<const std::uint8_t> body) {
  if (!header.has_value() || !HasSymbolPrefix(header->message_type) ||
      body.size() < protocol::kSymbolSize) {
    return std::nullopt;
  }
  auto end = protocol::kSymbolSize;
  for (std::size_t index = 0; index < end; ++index) {
    const auto byte = body[index];
    if (byte != 0 && (byte < 0x20U || byte > 0x7EU)) {
      return std::nullopt;
    }
  }
  while (end > 0 && (body[end - 1] == 0 ||
                     body[end - 1] == static_cast<std::uint8_t>(' '))) {
    --end;
  }
  if (end == 0) {
    return std::nullopt;
  }
  return std::string(reinterpret_cast<const char *>(body.data()), end);
}

}  // namespace

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
    throw std::invalid_argument("orderbook and basic-info outputs must differ");
  }

  MessageDecoder decoder(options.trading_day, options.symbol_filter_mode);
  BasicInfoCatalog basic_info_catalog(options.trading_day);
  std::unique_ptr<OrderbookCsvWriter> orderbook_writer;
  std::unique_ptr<BasicInfoCsvWriter> basic_info_writer;
  if (!options.dry_run) {
    orderbook_writer = std::make_unique<OrderbookCsvWriter>(options.output_path,
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
  const auto file_size = std::filesystem::file_size(options.dump_path);
  const auto trading_day_start_ns =
      TradingDayStartNanoseconds(options.trading_day);
  std::uint64_t offset = 0;
  while (offset < file_size) {
    std::optional<MessageHeader> decoded_header;
    std::streamsize body_bytes_read = 0;
    try {
      input.read(reinterpret_cast<char *>(raw_header.data()),
                 static_cast<std::streamsize>(raw_header.size()));
      const auto header_bytes = input.gcount();
      if (header_bytes != static_cast<std::streamsize>(raw_header.size())) {
        throw std::runtime_error(
            fmt::format("truncated TWSE header expected={} actual={}",
                        raw_header.size(), header_bytes));
      }
      decoded_header = DecodeMessageHeader(raw_header);

      const auto body_size =
          decoded_header->message_length - protocol::kHeaderSize;
      body.resize(body_size);
      input.read(reinterpret_cast<char *>(body.data()),
                 static_cast<std::streamsize>(body.size()));
      body_bytes_read = input.gcount();
      if (body_bytes_read != static_cast<std::streamsize>(body.size())) {
        throw std::runtime_error(
            fmt::format("truncated TWSE message body expected={} actual={}",
                        body.size(), body_bytes_read));
      }
      ValidateFrame(raw_header, body);
    } catch (const std::exception &error) {
      const auto available_body_size = static_cast<std::size_t>(
          std::max<std::streamsize>(0, body_bytes_read));
      const auto affected_symbol = AffectedSymbol(
          decoded_header, std::span<const std::uint8_t>(body).first(
                              std::min(body.size(), available_body_size)));
      if (affected_symbol.has_value() && decoded_header.has_value()) {
        decoder.InvalidateSymbol(decoded_header->service_type,
                                 *affected_symbol);
      }
      const auto recovered_offset =
          FindNextValidFrame(input, offset + 1, file_size);
      const auto next_offset = recovered_offset.value_or(file_size);
      FrameRecoveryIssue issue{
          .offset = offset,
          .skipped_bytes = next_offset - offset,
          .recovered_offset = recovered_offset,
          .service_type = std::nullopt,
          .message_type = std::nullopt,
          .format_version = std::nullopt,
          .sequence = std::nullopt,
          .affected_symbol = affected_symbol,
          .error = error.what(),
      };
      if (decoded_header.has_value()) {
        issue.service_type =
            static_cast<std::uint8_t>(decoded_header->service_type);
        issue.message_type =
            static_cast<std::uint8_t>(decoded_header->message_type);
        issue.format_version = decoded_header->format_version;
        issue.sequence = decoded_header->sequence;
      }
      stats.frame_recovery_issues.push_back(std::move(issue));
      offset = next_offset;
      stats.bytes_read = offset;
      if (recovered_offset.has_value()) {
        Seek(input, offset);
        continue;
      }
      break;
    }

    const auto &header = *decoded_header;
    try {
      const Orderbook<5> *record = nullptr;
      if (header.message_type == MessageType::kStockBasicInfo) {
        const auto *basic_info =
            basic_info_catalog.Process(header, body, offset);
        if (basic_info != nullptr &&
            options.symbol_filter_mode != SymbolFilterMode::kOddLot) {
          decoder.ApplyBasicInfo(*basic_info);
        }
      } else {
        std::int64_t local_ns = 0;
        if (HasDepthTime(header.message_type) && !IsEndControlSymbol(body) &&
            body.size() >= protocol::kDepthTimeOffset + 6) {
          local_ns = trading_day_start_ns +
                     DecodeBcdTimeNanoseconds(
                         std::span<const std::uint8_t>(body).subspan(
                             protocol::kDepthTimeOffset, 6));
        }
        record = decoder.Process(header, body, local_ns);
      }
      ++stats.messages_read;
      if (record != nullptr) {
        ++stats.rows_written;
        if (orderbook_writer != nullptr) {
          orderbook_writer->Write(*record);
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

  decoder.Finalize();
  stats.symbols_seen = decoder.symbol_count();
  stats.basic_info_messages = basic_info_catalog.normal_messages();
  stats.basic_info_controls = basic_info_catalog.control_records();
  stats.basic_info_duplicates = basic_info_catalog.identical_duplicates();
  stats.basic_info_cycle_mismatches = basic_info_catalog.cycle_mismatches();
  const auto &decoder_stats = decoder.stats();
  stats.source_actual_trade_payloads =
      decoder_stats.source_actual_trade_payloads;
  stats.actual_trade_payloads = decoder_stats.actual_trade_payloads;
  stats.match_groups = decoder_stats.match_groups;
  stats.multi_trade_groups = decoder_stats.multi_trade_groups;
  stats.trades_in_multi_groups = decoder_stats.trades_in_multi_groups;
  stats.held_ended_groups = decoder_stats.held_ended_groups;
  stats.buy_groups = decoder_stats.buy_groups;
  stats.sell_groups = decoder_stats.sell_groups;
  stats.unknown_groups = decoder_stats.unknown_groups;
  stats.missing_multiplier_messages = decoder_stats.missing_multiplier_messages;
  stats.invalidated_symbol_messages = decoder_stats.invalidated_symbol_messages;
  stats.missing_multiplier_symbols = decoder_stats.missing_multiplier_symbols;
  std::ranges::sort(stats.missing_multiplier_symbols, {},
                    [](const MarketSymbolIssue &issue) {
                      return std::pair{issue.market, issue.symbol};
                    });
  stats.sequence_gaps = decoder_stats.sequence_gaps;
  stats.incomplete_match_groups = decoder_stats.incomplete_match_groups;
  stats.value_imputations = decoder_stats.value_imputations;
  const auto basic_info_records = basic_info_catalog.records();
  stats.basic_info_rows = basic_info_records.size();
  if (stats.messages_read == 0 && !stats.frame_recovery_issues.empty()) {
    throw std::runtime_error(fmt::format(
        "TWSE dump contains no validated messages; first error at byte {}: {}",
        stats.frame_recovery_issues.front().offset,
        stats.frame_recovery_issues.front().error));
  }
  if (basic_info_writer != nullptr) {
    for (const auto &record : basic_info_records) {
      basic_info_writer->Write(record);
    }
    CsvOutputTransaction::Commit(*orderbook_writer, *basic_info_writer);
  }
  return stats;
}

}  // namespace aries::data::twse
