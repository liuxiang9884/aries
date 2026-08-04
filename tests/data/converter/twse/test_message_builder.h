#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "data/converter/twse/protocol.h"

namespace aries::data::twse::test {

struct Level {
  std::uint64_t price;
  std::uint64_t volume;
};

struct BasicInfoFields {
  std::string symbol{"2330"};
  ServiceType service_type{ServiceType::kListed};
  std::string industry_code{"01"};
  std::string security_type{"00"};
  std::string stock_entries{"  "};
  std::uint64_t anomaly_code{};
  char stock_group_code{' '};
  char board_code{'0'};
  std::uint64_t reference_price{900000};
  std::uint64_t high_limit{990000};
  std::uint64_t low_limit{810000};
  char abnormal_recommendation{' '};
  char special_abnormal{' '};
  char day_trading_code{' '};
  char margin_short_exempt{' '};
  char borrow_short_exempt{' '};
  std::uint64_t matching_cycle_seconds{};
  char warrant_flag{' '};
  std::uint64_t strike_price{};
  std::uint64_t previous_exercise_volume{};
  std::uint64_t previous_cancellation_volume{};
  std::uint64_t outstanding_volume{};
  std::uint64_t exercise_ratio{};
  std::uint64_t warrant_upper_price{};
  std::uint64_t warrant_lower_price{};
  std::uint64_t maturity_date{};
  char foreign_stock_flag{' '};
  std::uint64_t multiplier{1000};
  std::string currency{"   "};
  std::uint64_t market_data_line{1};
};

template <std::size_t Size>
std::array<std::uint8_t, Size> EncodeBcd(std::uint64_t value) {
  std::array<std::uint8_t, Size> result{};
  for (std::size_t i = Size; i > 0; --i) {
    const auto low = static_cast<std::uint8_t>(value % 10);
    value /= 10;
    const auto high = static_cast<std::uint8_t>(value % 10);
    value /= 10;
    result[i - 1] = static_cast<std::uint8_t>((high << 4U) | low);
  }
  if (value != 0) {
    throw std::invalid_argument("value does not fit BCD field");
  }
  return result;
}

template <std::size_t Size>
void PutBcd(std::vector<std::uint8_t> &output, std::size_t offset,
            std::uint64_t value) {
  const auto encoded = EncodeBcd<Size>(value);
  std::copy(encoded.begin(), encoded.end(), output.begin() + offset);
}

inline void PutSymbol(std::vector<std::uint8_t> &output,
                      std::string_view symbol) {
  if (symbol.size() > protocol::kSymbolSize) {
    throw std::invalid_argument("symbol is too long");
  }
  std::fill_n(output.begin(), protocol::kSymbolSize,
              static_cast<std::uint8_t>(' '));
  std::copy(symbol.begin(), symbol.end(), output.begin());
}

inline void PutAscii(std::vector<std::uint8_t> &output, std::size_t offset,
                     std::size_t size, std::string_view value) {
  if (value.size() > size) {
    throw std::invalid_argument("ASCII value is too long");
  }
  std::fill_n(output.begin() + static_cast<std::ptrdiff_t>(offset), size,
              static_cast<std::uint8_t>(' '));
  std::copy(value.begin(), value.end(),
            output.begin() + static_cast<std::ptrdiff_t>(offset));
}

inline std::vector<std::uint8_t> MakeBasicInfo(const BasicInfoFields &fields) {
  std::vector<std::uint8_t> body(protocol::kStockBasicBodySize);
  PutSymbol(body, fields.symbol);
  PutAscii(body, 22, 2, fields.industry_code);
  PutAscii(body, 24, 2, fields.security_type);
  PutAscii(body, 26, 2, fields.stock_entries);
  PutBcd<1>(body, 28, fields.anomaly_code);

  const bool listed = fields.service_type == ServiceType::kListed;
  const std::size_t shift = listed ? 0U : 1U;
  if (!listed) {
    body[29] = static_cast<std::uint8_t>(fields.stock_group_code);
  }
  body[29 + shift] = static_cast<std::uint8_t>(fields.board_code);
  PutBcd<5>(body, 30 + shift, fields.reference_price);
  PutBcd<5>(body, 35 + shift, fields.high_limit);
  PutBcd<5>(body, 40 + shift, fields.low_limit);
  body[46 + shift] = static_cast<std::uint8_t>(fields.abnormal_recommendation);
  body[47 + shift] = static_cast<std::uint8_t>(fields.special_abnormal);
  body[48 + shift] = static_cast<std::uint8_t>(fields.day_trading_code);
  body[49 + shift] = static_cast<std::uint8_t>(fields.margin_short_exempt);
  body[50 + shift] = static_cast<std::uint8_t>(fields.borrow_short_exempt);
  PutBcd<3>(body, 51 + shift, fields.matching_cycle_seconds);
  body[54 + shift] = static_cast<std::uint8_t>(fields.warrant_flag);
  PutBcd<5>(body, 55 + shift, fields.strike_price);
  PutBcd<5>(body, 60 + shift, fields.previous_exercise_volume);
  PutBcd<5>(body, 65 + shift, fields.previous_cancellation_volume);
  PutBcd<5>(body, 70 + shift, fields.outstanding_volume);
  PutBcd<4>(body, 75 + shift, fields.exercise_ratio);
  PutBcd<5>(body, 79 + shift, fields.warrant_upper_price);
  PutBcd<5>(body, 84 + shift, fields.warrant_lower_price);
  PutBcd<4>(body, 89 + shift, fields.maturity_date);
  if (listed) {
    body[93] = static_cast<std::uint8_t>(fields.foreign_stock_flag);
  }
  PutBcd<3>(body, 94, fields.multiplier);
  PutAscii(body, 97, 3, fields.currency);
  PutBcd<1>(body, 100, fields.market_data_line);
  body[body.size() - 2] = '\r';
  body[body.size() - 1] = '\n';
  return body;
}

inline std::vector<std::uint8_t> MakeBasicControl(
    std::string_view count, std::string_view control,
    ServiceType service_type = ServiceType::kListed) {
  BasicInfoFields fields{
      .symbol = std::string(count),
      .service_type = service_type,
      .stock_entries = std::string(control),
  };
  return MakeBasicInfo(fields);
}

inline std::vector<std::uint8_t> MakeStockBasic(
    std::string_view symbol, std::uint64_t previous_close,
    std::uint64_t high_limit, std::uint64_t low_limit,
    ServiceType service_type = ServiceType::kListed) {
  return MakeBasicInfo(BasicInfoFields{
      .symbol = std::string(symbol),
      .service_type = service_type,
      .reference_price = previous_close,
      .high_limit = high_limit,
      .low_limit = low_limit,
  });
}

inline std::vector<std::uint8_t> MakeOddLotBasic(std::string_view symbol,
                                                 std::uint64_t previous_close,
                                                 std::uint64_t high_limit,
                                                 std::uint64_t low_limit) {
  std::vector<std::uint8_t> body(protocol::kOddLotBasicBodySize);
  PutSymbol(body, symbol);
  PutBcd<5>(body, protocol::kOddLotPreviousCloseOffset, previous_close);
  PutBcd<5>(body, protocol::kOddLotHighLimitOffset, high_limit);
  PutBcd<5>(body, protocol::kOddLotLowLimitOffset, low_limit);
  body[body.size() - 2] = '\r';
  body[body.size() - 1] = '\n';
  return body;
}

inline std::vector<std::uint8_t> MakeDepth(
    std::string_view symbol, std::uint64_t exchange_time,
    std::uint64_t total_volume, bool is_traded, std::uint8_t bid_levels,
    std::uint8_t ask_levels, std::span<const Level> levels,
    std::uint8_t status = 0, std::uint8_t limit_flag = 0,
    bool disclosure_tag = false) {
  const auto expected_levels =
      static_cast<std::size_t>(is_traded) + bid_levels + ask_levels;
  if (levels.size() != expected_levels) {
    throw std::invalid_argument("level count does not match flags");
  }

  std::vector<std::uint8_t> body(protocol::kStockDepthInfoSize +
                                 levels.size() * protocol::kStockLevelSize +
                                 protocol::kMessageTrailerSize);
  PutSymbol(body, symbol);
  PutBcd<6>(body, protocol::kDepthTimeOffset, exchange_time);
  body[protocol::kDepthDataFlagOffset] = static_cast<std::uint8_t>(
      (is_traded ? 0x80U : 0U) | (bid_levels << 4U) | (ask_levels << 1U) |
      (disclosure_tag ? 0x01U : 0U));
  body[protocol::kDepthLimitFlagOffset] = limit_flag;
  body[protocol::kDepthStatusOffset] = status;
  PutBcd<4>(body, protocol::kStockTotalVolumeOffset, total_volume);

  auto offset = protocol::kStockDepthInfoSize;
  for (const auto &level : levels) {
    PutBcd<5>(body, offset, level.price);
    PutBcd<4>(body, offset + 5, level.volume);
    offset += protocol::kStockLevelSize;
  }
  body[body.size() - 2] = '\r';
  body[body.size() - 1] = '\n';
  return body;
}

inline std::vector<std::uint8_t> MakeOddLotDepth(
    std::string_view symbol, std::uint64_t exchange_time,
    std::uint64_t total_volume, bool is_traded, std::uint8_t bid_levels,
    std::uint8_t ask_levels, std::span<const Level> levels,
    std::uint8_t status = 0, std::uint8_t limit_flag = 0,
    bool disclosure_tag = false) {
  const auto expected_levels =
      static_cast<std::size_t>(is_traded) + bid_levels + ask_levels;
  if (levels.size() != expected_levels) {
    throw std::invalid_argument("level count does not match flags");
  }

  std::vector<std::uint8_t> body(protocol::kOddLotDepthInfoSize +
                                 levels.size() * protocol::kOddLotLevelSize +
                                 protocol::kMessageTrailerSize);
  PutSymbol(body, symbol);
  PutBcd<6>(body, protocol::kDepthTimeOffset, exchange_time);
  body[protocol::kDepthDataFlagOffset] = static_cast<std::uint8_t>(
      (is_traded ? 0x80U : 0U) | (bid_levels << 4U) | (ask_levels << 1U) |
      (disclosure_tag ? 0x01U : 0U));
  body[protocol::kDepthLimitFlagOffset] = limit_flag;
  body[protocol::kDepthStatusOffset] = status;
  PutBcd<6>(body, protocol::kOddLotTotalVolumeOffset, total_volume);

  auto offset = protocol::kOddLotDepthInfoSize;
  for (const auto &level : levels) {
    PutBcd<5>(body, offset, level.price);
    PutBcd<6>(body, offset + 5, level.volume);
    offset += protocol::kOddLotLevelSize;
  }
  body[body.size() - 2] = '\r';
  body[body.size() - 1] = '\n';
  return body;
}

inline MessageHeader MakeHeader(MessageType type, std::size_t body_size,
                                std::uint64_t sequence = 0,
                                ServiceType service = ServiceType::kListed) {
  std::uint8_t format_version = protocol::kOddLotFormatVersion;
  switch (type) {
    case MessageType::kStockBasicInfo:
      format_version = protocol::kStockBasicFormatVersion;
      break;
    case MessageType::kStockDepthV:
    case MessageType::kWarrantDepthV:
      format_version = protocol::kStockDepthFormatVersion;
      break;
    case MessageType::kStockOddLotBasicInfo:
    case MessageType::kStockOddLotDepthV:
    default:
      break;
  }
  return MessageHeader{
      .message_length = protocol::kHeaderSize + body_size,
      .service_type = service,
      .message_type = type,
      .format_version = format_version,
      .sequence = sequence,
  };
}

inline void AppendMessage(std::vector<std::uint8_t> &dump, MessageType type,
                          std::span<const std::uint8_t> body,
                          std::uint64_t sequence = 0,
                          ServiceType service = ServiceType::kListed) {
  const auto header = MakeHeader(type, body.size(), sequence, service);
  const auto start = dump.size();
  dump.resize(start + protocol::kHeaderSize);
  dump[start] = protocol::kEscape;
  const auto length = EncodeBcd<2>(header.message_length);
  std::copy(length.begin(), length.end(), dump.begin() + start + 1);
  dump[start + 3] =
      EncodeBcd<1>(static_cast<std::uint8_t>(header.service_type))[0];
  dump[start + 4] =
      EncodeBcd<1>(static_cast<std::uint8_t>(header.message_type))[0];
  dump[start + 5] = EncodeBcd<1>(header.format_version)[0];
  const auto encoded_sequence = EncodeBcd<4>(sequence);
  std::copy(encoded_sequence.begin(), encoded_sequence.end(),
            dump.begin() + start + 6);
  dump.insert(dump.end(), body.begin(), body.end());

  if (body.size() < protocol::kMessageTrailerSize) {
    throw std::invalid_argument("message body does not contain a trailer");
  }
  std::uint8_t checksum = 0;
  for (auto i = start + 1; i < dump.size() - protocol::kMessageTrailerSize;
       ++i) {
    checksum ^= dump[i];
  }
  dump[dump.size() - protocol::kMessageTrailerSize] = checksum;
}

inline void WriteBinaryFile(const std::filesystem::path &path,
                            std::span<const std::uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary);
  if (!output.is_open()) {
    throw std::runtime_error("failed to create test file");
  }
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

}  // namespace aries::data::twse::test
