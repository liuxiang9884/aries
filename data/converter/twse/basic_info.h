#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "data/converter/twse/protocol.h"

namespace aries::data::twse {

struct BasicInfoRecord {
  std::int32_t trading_day{};
  std::string market;
  std::string symbol;
  std::string industry_code;
  std::string security_type;
  std::uint64_t anomaly_code{};
  std::optional<std::string> stock_group_code;
  std::string board_code;
  double reference_price{};
  double high_limit{};
  double low_limit{};
  std::string abnormal_recommendation;
  std::string special_abnormal;
  std::string day_trading_code;
  std::string margin_short_exempt;
  std::string borrow_short_exempt;
  std::uint64_t matching_cycle_seconds{};
  std::string warrant_flag;
  std::optional<double> strike_price;
  std::optional<std::uint64_t> previous_exercise_volume;
  std::optional<std::uint64_t> previous_cancellation_volume;
  std::optional<std::uint64_t> outstanding_volume;
  std::optional<double> exercise_ratio;
  std::optional<double> warrant_upper_price;
  std::optional<double> warrant_lower_price;
  std::optional<std::int32_t> maturity_date;
  std::optional<std::string> foreign_stock_flag;
  std::uint64_t multiplier{};
  std::string currency;
  std::uint64_t market_data_line{};

  bool operator==(const BasicInfoRecord &) const = default;
};

enum class BasicInfoControlKind {
  kNone,
  kAll,
  kNew,
};

struct DecodedBasicInfo {
  std::optional<BasicInfoRecord> record;
  BasicInfoControlKind control_kind{BasicInfoControlKind::kNone};
  std::uint64_t control_count{};
};

[[nodiscard]] DecodedBasicInfo
DecodeBasicInfo(std::int32_t trading_day, const MessageHeader &header,
                std::span<const std::uint8_t> body);

[[nodiscard]] bool IsWarrantSecurity(const BasicInfoRecord &record);

class BasicInfoCatalog {
public:
  explicit BasicInfoCatalog(std::int32_t trading_day);

  [[nodiscard]] const BasicInfoRecord *
  Process(const MessageHeader &header, std::span<const std::uint8_t> body,
          std::uint64_t offset);

  [[nodiscard]] std::vector<BasicInfoRecord> records() const;

  [[nodiscard]] std::uint64_t normal_messages() const noexcept {
    return normal_messages_;
  }

  [[nodiscard]] std::uint64_t control_records() const noexcept {
    return control_records_;
  }

  [[nodiscard]] std::uint64_t identical_duplicates() const noexcept {
    return identical_duplicates_;
  }

private:
  using Key = std::tuple<std::int32_t, std::string, std::string>;

  struct Entry {
    BasicInfoRecord record;
    std::uint64_t offset{};
    std::uint64_t sequence{};
  };

  std::int32_t trading_day_;
  std::map<Key, Entry> records_;
  std::array<std::uint64_t, 2> cycle_messages_{};
  std::array<bool, 2> cycle_synchronized_{};
  std::uint64_t normal_messages_{};
  std::uint64_t control_records_{};
  std::uint64_t identical_duplicates_{};
};

} // namespace aries::data::twse
