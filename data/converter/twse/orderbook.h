#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace aries::data::twse {

template <std::size_t N> struct Orderbook {
  static_assert(N > 0, "Orderbook must contain at least one level");

  std::string symbol;
  std::int64_t exchtime{};
  std::int64_t localtime{};
  std::int64_t status{};
  double last_price{};
  double previous_close{};
  double open{};
  double high_limit{};
  double low_limit{};
  std::uint64_t multiplier{};
  std::int64_t total_volume{};
  double total_value{};
  std::int64_t total_trade{};
  std::array<double, N> ask_price{};
  std::array<std::int64_t, N> ask_volume{};
  std::array<double, N> bid_price{};
  std::array<std::int64_t, N> bid_volume{};
  std::int64_t sequence{};
};

} // namespace aries::data::twse
