#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace aries::data::taifex::test {

inline void AppendAscii(std::vector<std::uint8_t> &output,
                        std::string_view value, std::size_t width) {
  const auto copied = std::min(value.size(), width);
  output.insert(output.end(), value.begin(), value.begin() + copied);
  output.insert(output.end(), width - copied, static_cast<std::uint8_t>(' '));
}

inline void AppendBcd(std::vector<std::uint8_t> &output, std::uint64_t value,
                      std::size_t width) {
  std::vector<std::uint8_t> digits(width * 2);
  for (auto iterator = digits.rbegin(); iterator != digits.rend(); ++iterator) {
    *iterator = static_cast<std::uint8_t>(value % 10);
    value /= 10;
  }
  if (value != 0) {
    throw std::invalid_argument("BCD test value exceeds field width");
  }
  for (std::size_t i = 0; i < width; ++i) {
    output.push_back(
        static_cast<std::uint8_t>((digits[i * 2] << 4U) | digits[i * 2 + 1]));
  }
}

inline std::vector<std::uint8_t> MakeI010(std::string_view symbol,
                                          std::uint64_t reference_price,
                                          std::uint8_t decimal_locator = 2,
                                          char contract_type = 'I') {
  std::vector<std::uint8_t> body;
  AppendAscii(body, symbol, 10);
  AppendBcd(body, reference_price, 5);
  body.push_back(contract_type);
  AppendBcd(body, decimal_locator, 1);
  AppendBcd(body, 0, 1);
  AppendBcd(body, 20260101, 4);
  AppendBcd(body, 20261231, 4);
  AppendBcd(body, 1, 1);
  AppendBcd(body, 20261231, 4);
  body.push_back('Y');
  body.insert(body.end(), 8, 0);
  return body;
}

inline std::vector<std::uint8_t> MakeI011(std::string_view kind_id,
                                          double multiplier,
                                          std::uint8_t decimal_locator = 2,
                                          char contract_subtype = 'I') {
  std::vector<std::uint8_t> body;
  AppendAscii(body, kind_id, 4);
  body.insert(body.end(), 30, static_cast<std::uint8_t>(' '));
  AppendAscii(body, "", 6);
  body.push_back(contract_subtype);
  AppendBcd(body, static_cast<std::uint64_t>(multiplier * 10'000.0), 6);
  body.push_back('N');
  body.push_back('1');
  AppendBcd(body, decimal_locator, 1);
  AppendBcd(body, 0, 1);
  body.push_back('Y');
  AppendAscii(body, "20260101", 8);
  body.push_back('Y');
  body.push_back('S');
  body.push_back(' ');
  AppendBcd(body, 1, 1);
  body.push_back('0');
  return body;
}

struct BookLevel {
  char type;
  std::uint64_t price;
  std::uint64_t volume;
  std::uint8_t level;
};

struct Trade {
  std::uint64_t price;
  std::uint64_t volume;
  char sign{'0'};
};

inline std::vector<std::uint8_t>
MakeI024(std::string_view symbol, std::uint64_t sequence, std::uint64_t price,
         std::uint64_t volume, std::uint64_t total_volume,
         std::uint64_t buy_count, std::uint64_t sell_count,
         char match_flag = '0', std::span<const Trade> extra_trades = {},
         char price_sign = '0') {
  std::vector<std::uint8_t> body;
  AppendAscii(body, symbol, 20);
  AppendBcd(body, sequence, 5);
  body.push_back(match_flag);
  AppendBcd(body, 90'000'001'000ULL, 6);
  body.push_back(price_sign);
  AppendBcd(body, price, 5);
  AppendBcd(body, volume, 4);
  body.push_back(static_cast<std::uint8_t>(0x80U | extra_trades.size()));
  for (const auto &trade : extra_trades) {
    body.push_back(trade.sign);
    AppendBcd(body, trade.price, 5);
    AppendBcd(body, trade.volume, 2);
  }
  AppendBcd(body, total_volume, 4);
  AppendBcd(body, buy_count, 4);
  AppendBcd(body, sell_count, 4);
  return body;
}

struct PriceLimit {
  std::uint64_t level;
  std::uint64_t price;
};

inline std::vector<std::uint8_t>
MakeI012(std::string_view symbol, std::span<const PriceLimit> raise_limits,
         std::span<const PriceLimit> fall_limits) {
  std::vector<std::uint8_t> body;
  AppendAscii(body, symbol, 10);
  AppendBcd(body, raise_limits.size(), 1);
  for (const auto &limit : raise_limits) {
    AppendBcd(body, limit.level, 1);
    AppendBcd(body, limit.price, 5);
  }
  AppendBcd(body, fall_limits.size(), 1);
  for (const auto &limit : fall_limits) {
    AppendBcd(body, limit.level, 1);
    AppendBcd(body, limit.price, 5);
  }
  return body;
}

inline std::vector<std::uint8_t> MakeI025(std::string_view symbol,
                                          std::uint64_t sequence,
                                          std::uint64_t high,
                                          std::uint64_t low) {
  std::vector<std::uint8_t> body;
  AppendAscii(body, symbol, 20);
  AppendBcd(body, sequence, 5);
  body.push_back('0');
  AppendBcd(body, high, 5);
  body.push_back('0');
  AppendBcd(body, low, 5);
  AppendBcd(body, 90'001'001'000ULL, 6);
  return body;
}

inline std::vector<std::uint8_t> MakeI081(std::string_view symbol,
                                          std::uint64_t sequence, char action,
                                          const BookLevel &level) {
  std::vector<std::uint8_t> body;
  AppendAscii(body, symbol, 20);
  AppendBcd(body, sequence, 5);
  AppendBcd(body, 1, 1);
  body.push_back(action);
  body.push_back(level.type);
  body.push_back('0');
  AppendBcd(body, level.price, 5);
  AppendBcd(body, level.volume, 4);
  AppendBcd(body, level.level, 1);
  return body;
}

inline std::vector<std::uint8_t> MakeI083(std::string_view symbol,
                                          std::uint64_t sequence,
                                          std::span<const BookLevel> levels,
                                          char order_flag = '0') {
  std::vector<std::uint8_t> body;
  AppendAscii(body, symbol, 20);
  AppendBcd(body, sequence, 5);
  body.push_back(order_flag);
  AppendBcd(body, levels.size(), 1);
  for (const auto &level : levels) {
    body.push_back(level.type);
    body.push_back('0');
    AppendBcd(body, level.price, 5);
    AppendBcd(body, level.volume, 4);
    AppendBcd(body, level.level, 1);
  }
  return body;
}

inline std::vector<std::uint8_t>
MakeI084Orderbook(std::string_view symbol, std::uint64_t last_sequence,
                  std::span<const BookLevel> levels) {
  std::vector<std::uint8_t> body{'O'};
  AppendBcd(body, 1, 1);
  AppendAscii(body, symbol, 20);
  AppendBcd(body, last_sequence, 5);
  AppendBcd(body, levels.size(), 1);
  for (const auto &level : levels) {
    body.push_back(level.type);
    body.push_back('0');
    AppendBcd(body, level.price, 5);
    AppendBcd(body, level.volume, 4);
    AppendBcd(body, level.level, 1);
  }
  return body;
}

inline std::vector<std::uint8_t>
MakeI084Statistics(std::string_view symbol, std::uint64_t last_price,
                   std::uint64_t last_volume, std::uint64_t total_volume,
                   std::uint64_t buy_count, std::uint64_t sell_count) {
  std::vector<std::uint8_t> body{'S'};
  AppendBcd(body, 1, 1);
  AppendAscii(body, symbol, 20);
  AppendBcd(body, 90'000'000'000ULL, 6);
  body.push_back('0');
  AppendBcd(body, last_price, 5);
  AppendBcd(body, last_volume, 4);
  AppendBcd(body, total_volume, 4);
  AppendBcd(body, buy_count, 4);
  AppendBcd(body, sell_count, 4);
  AppendBcd(body, 85'000'000'000ULL, 6);
  body.push_back('0');
  AppendBcd(body, last_price, 5);
  AppendBcd(body, last_volume, 4);
  body.push_back('0');
  AppendBcd(body, last_price, 5);
  body.push_back('0');
  AppendBcd(body, last_price, 5);
  AppendBcd(body, 0, 4);
  AppendBcd(body, 0, 4);
  AppendBcd(body, 0, 4);
  AppendBcd(body, 0, 4);
  return body;
}

inline void AppendFrame(std::vector<std::uint8_t> &dump, char transmission,
                        char kind, std::uint8_t version,
                        std::span<const std::uint8_t> body,
                        std::uint64_t channel_sequence = 1) {
  std::vector<std::uint8_t> header{0x1B,
                                   static_cast<std::uint8_t>(transmission),
                                   static_cast<std::uint8_t>(kind)};
  AppendBcd(header, 90'000'000'000ULL, 6);
  AppendBcd(header, 1, 2);
  AppendBcd(header, channel_sequence, 5);
  AppendBcd(header, version, 1);
  AppendBcd(header, body.size(), 2);
  dump.insert(dump.end(), header.begin(), header.end());
  dump.insert(dump.end(), body.begin(), body.end());
  std::uint8_t checksum = 0;
  for (const auto byte : std::span(header).subspan(1)) {
    checksum ^= byte;
  }
  for (const auto byte : body) {
    checksum ^= byte;
  }
  dump.push_back(checksum);
  dump.push_back('\r');
  dump.push_back('\n');
}

inline void WriteBinaryFile(const std::filesystem::path &path,
                            std::span<const std::uint8_t> bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
}

} // namespace aries::data::taifex::test
