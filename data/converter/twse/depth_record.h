#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace aries::data::twse {

struct DepthRecord {
  std::string symbol;
  std::int64_t exchtime{};
  std::int64_t localtime{};
  std::int64_t status{};
  double last_price{};
  double previous_close{};
  double open{};
  double high_limit{};
  double low_limit{};
  std::int64_t total_volume{};
  double total_value{};
  std::int64_t total_trade{};
  std::array<double, 5> ask_price{};
  std::array<std::int64_t, 5> ask_volume{};
  std::array<double, 5> bid_price{};
  std::array<std::int64_t, 5> bid_volume{};
  std::int64_t sequence{};
};

} // namespace aries::data::twse
