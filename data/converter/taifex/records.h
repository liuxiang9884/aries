#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace aries::data::taifex {

struct BasicInfoRecord {
  std::int32_t trading_day{};
  std::string symbol;
  std::string kind_id;
  bool is_spread{};
  std::string basic_source;
  char contract_type{};
  char contract_subtype{};
  std::optional<double> reference_price;
  std::uint8_t decimal_locator{};
  std::optional<std::uint8_t> strike_decimal_locator;
  std::optional<std::int32_t> listing_date;
  std::optional<std::int32_t> delisting_date;
  std::optional<std::int32_t> delivery_date;
  std::optional<std::uint8_t> flow_group;
  std::optional<char> dynamic_banding;
  double multiplier{};
  char currency_code{};
  std::string currency;
  std::string stock_id;
  char contract_status{};
  char quote_flag{};
  char block_trade_flag{};
  char expiry_type{};
  char underlying_type{};
  std::uint8_t close_group{};
  char end_session{};
};

struct DepthRecord {
  std::int32_t trading_day{};
  std::string symbol;
  std::int64_t exchtime{};
  std::int64_t localtime{};
  double reference_price{};
  double open{};
  double high{};
  double low{};
  double last_price{};
  std::int64_t trade_volume{};
  std::int64_t total_volume{};
  double total_value{};
  std::int64_t total_buy_count{};
  std::int64_t total_sell_count{};
  std::array<double, 5> ask_price{};
  std::array<std::int64_t, 5> ask_volume{};
  std::array<double, 5> bid_price{};
  std::array<std::int64_t, 5> bid_volume{};
  double derived_ask_price{};
  std::int64_t derived_ask_volume{};
  double derived_bid_price{};
  std::int64_t derived_bid_volume{};
  std::uint8_t match_flag{};
  std::uint8_t build_type{};
  std::uint8_t orderbook_action{};
  std::uint64_t sequence{};
};

} // namespace aries::data::taifex
