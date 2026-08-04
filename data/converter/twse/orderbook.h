#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace aries::data::twse {

enum class Market : std::uint8_t {
  kTwse = 1,
  kTpex = 2,
};

enum class PriceLimitState : std::uint8_t {
  kNormal = 0,
  kDownLimit = 1,
  kUpLimit = 2,
  kReserved = 3,
};

enum class InstantaneousTrend : std::uint8_t {
  kNormal = 0,
  kHeldDown = 1,
  kHeldUp = 2,
  kReserved = 3,
};

enum class MatchingMethod : std::uint8_t {
  kCallAuction = 0,
  kContinuous = 1,
};

enum class TradeSide : std::uint8_t {
  kUnknown = 0,
  kBuy = 1,
  kSell = 2,
};

struct DisclosureState {
  std::uint8_t value;

  [[nodiscard]] constexpr bool has_trade() const noexcept {
    return (value & 0x80U) != 0;
  }

  [[nodiscard]] constexpr std::uint8_t bid_level_count() const noexcept {
    return static_cast<std::uint8_t>((value >> 4U) & 0x07U);
  }

  [[nodiscard]] constexpr std::uint8_t ask_level_count() const noexcept {
    return static_cast<std::uint8_t>((value >> 1U) & 0x07U);
  }

  [[nodiscard]] constexpr bool disclosure_tag() const noexcept {
    return (value & 0x01U) != 0;
  }
};

struct LimitState {
  std::uint8_t value;

  [[nodiscard]] constexpr PriceLimitState trade_limit() const noexcept {
    return static_cast<PriceLimitState>((value >> 6U) & 0x03U);
  }

  [[nodiscard]] constexpr PriceLimitState best_bid_limit() const noexcept {
    return static_cast<PriceLimitState>((value >> 4U) & 0x03U);
  }

  [[nodiscard]] constexpr PriceLimitState best_ask_limit() const noexcept {
    return static_cast<PriceLimitState>((value >> 2U) & 0x03U);
  }

  [[nodiscard]] constexpr InstantaneousTrend instantaneous_trend()
      const noexcept {
    return static_cast<InstantaneousTrend>(value & 0x03U);
  }
};

struct SessionState {
  std::uint8_t value;

  [[nodiscard]] constexpr bool is_trial() const noexcept {
    return (value & 0x80U) != 0;
  }

  [[nodiscard]] constexpr bool is_delayed_open() const noexcept {
    return (value & 0x40U) != 0;
  }

  [[nodiscard]] constexpr bool is_delayed_close() const noexcept {
    return (value & 0x20U) != 0;
  }

  [[nodiscard]] constexpr MatchingMethod matching_method() const noexcept {
    return (value & 0x10U) != 0 ? MatchingMethod::kContinuous
                                : MatchingMethod::kCallAuction;
  }

  [[nodiscard]] constexpr bool is_opening() const noexcept {
    return (value & 0x08U) != 0;
  }

  [[nodiscard]] constexpr bool is_closing() const noexcept {
    return (value & 0x04U) != 0;
  }

  [[nodiscard]] constexpr std::uint8_t reserved() const noexcept {
    return static_cast<std::uint8_t>(value & 0x03U);
  }
};

static_assert(sizeof(DisclosureState) == 1);
static_assert(sizeof(LimitState) == 1);
static_assert(sizeof(SessionState) == 1);

template <std::size_t N>
struct Orderbook {
  static_assert(N > 0, "Orderbook must contain at least one level");

  char symbol[16];

  std::int64_t exchange_ns;
  std::int64_t local_ns;

  std::int32_t symbol_id;

  Market market;
  DisclosureState disclosure;
  LimitState limit_state;
  SessionState session_state;

  TradeSide trade_side;
  std::uint32_t trade_count;

  double last_price;
  double open;
  double high;
  double low;

  std::int64_t trade_volume;
  std::int64_t total_volume;
  double total_value;

  double ask_price[N];
  std::int64_t ask_volume[N];
  double bid_price[N];
  std::int64_t bid_volume[N];

  std::uint64_t source_sequence;
};

using Orderbook5 = Orderbook<5>;

static_assert(std::is_trivial_v<Orderbook5>);
static_assert(std::is_standard_layout_v<Orderbook5>);
static_assert(std::is_trivially_copyable_v<Orderbook5>);

}  // namespace aries::data::twse
