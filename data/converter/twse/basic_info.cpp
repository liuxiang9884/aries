#include "data/converter/twse/basic_info.h"

#include <cctype>
#include <chrono>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>

#include "data/converter/twse/bcd_decoder.h"

namespace aries::data::twse {
namespace {

constexpr std::size_t kIndustryCodeOffset = 22;
constexpr std::size_t kSecurityTypeOffset = 24;
constexpr std::size_t kStockEntriesOffset = 26;
constexpr std::size_t kAnomalyCodeOffset = 28;
constexpr std::size_t kListedBoardCodeOffset = 29;
constexpr std::size_t kOtcStockGroupCodeOffset = 29;
constexpr std::size_t kOtcBoardCodeOffset = 30;
constexpr std::size_t kMultiplierOffset = 94;
constexpr std::size_t kCurrencyOffset = 97;
constexpr std::size_t kMarketDataLineOffset = 100;
constexpr std::uint64_t kWarrantVolumeScale = 1'000;
constexpr double kWarrantRatioScale = 1'000.0;

void ValidateTradingDay(std::int32_t trading_day) {
  const auto year_value = trading_day / 10'000;
  const auto month_value = trading_day / 100 % 100;
  const auto day_value = trading_day % 100;
  const auto date = std::chrono::year{year_value} /
                    std::chrono::month{static_cast<unsigned>(month_value)} /
                    std::chrono::day{static_cast<unsigned>(day_value)};
  if (!date.ok()) {
    throw std::invalid_argument("invalid trading day");
  }
}

std::string DecodeCode(std::span<const std::uint8_t> bytes,
                       std::string_view field_name) {
  for (const auto byte : bytes) {
    if (byte != 0 && (byte < 0x20U || byte > 0x7EU)) {
      throw std::runtime_error(
          fmt::format("{} contains a non-ASCII byte", field_name));
    }
    if (byte == static_cast<std::uint8_t>(',')) {
      throw std::runtime_error(
          fmt::format("{} contains a CSV delimiter", field_name));
    }
  }

  std::size_t begin = 0;
  while (
      begin < bytes.size() &&
      (bytes[begin] == 0 || bytes[begin] == static_cast<std::uint8_t>(' '))) {
    ++begin;
  }
  std::size_t end = bytes.size();
  while (end > begin && (bytes[end - 1] == 0 ||
                         bytes[end - 1] == static_cast<std::uint8_t>(' '))) {
    --end;
  }
  return std::string(reinterpret_cast<const char *>(bytes.data() + begin),
                     end - begin);
}

std::string DecodeSymbol(std::span<const std::uint8_t> bytes) {
  auto symbol = DecodeCode(bytes, "symbol");
  if (symbol.empty()) {
    throw std::runtime_error("TWSE symbol is empty");
  }
  return symbol;
}

std::string DecodeFlag(std::span<const std::uint8_t> body, std::size_t offset,
                       std::string_view field_name) {
  return DecodeCode(body.subspan(offset, 1), field_name);
}

std::uint64_t DecodeControlCount(std::span<const std::uint8_t> bytes) {
  std::uint64_t result = 0;
  for (const auto byte : bytes) {
    if (!std::isdigit(static_cast<unsigned char>(byte))) {
      throw std::runtime_error("format1 control count is not decimal ASCII");
    }
    result = result * 10U + static_cast<std::uint64_t>(byte - '0');
  }
  return result;
}

void ValidateMessage(const MessageHeader &header,
                     std::span<const std::uint8_t> body) {
  if (header.message_type != MessageType::kStockBasicInfo) {
    throw std::runtime_error("message is not format1 basic info");
  }
  if (header.format_version != protocol::kStockBasicFormatVersion) {
    throw std::runtime_error("unsupported format1 version");
  }
  if (header.service_type != ServiceType::kListed &&
      header.service_type != ServiceType::kOtc) {
    throw std::runtime_error("format1 has unsupported service type");
  }
  if (body.size() != protocol::kStockBasicBodySize ||
      header.message_length != protocol::kHeaderSize + body.size()) {
    throw std::runtime_error("format1 message has invalid length");
  }
  if (body[body.size() - 2] != '\r' || body[body.size() - 1] != '\n') {
    throw std::runtime_error("format1 message has invalid trailer");
  }
}

bool IsValidCalendarDate(std::int32_t value) {
  const auto year_value = value / 10'000;
  const auto month_value = value / 100 % 100;
  const auto day_value = value % 100;
  return (std::chrono::year{year_value} /
          std::chrono::month{static_cast<unsigned>(month_value)} /
          std::chrono::day{static_cast<unsigned>(day_value)})
      .ok();
}

template <typename T> std::string ValueText(const T &value) {
  return fmt::format("{}", value);
}

std::string ValueText(const std::string &value) {
  return fmt::format("'{}'", value);
}

template <typename T> std::string ValueText(const std::optional<T> &value) {
  return value.has_value() ? ValueText(*value) : "<empty>";
}

template <typename T>
void AddDifference(std::vector<std::string> &differences, std::string_view name,
                   const T &old_value, const T &new_value) {
  if (old_value != new_value) {
    differences.push_back(fmt::format(
        "{}: old={} new={}", name, ValueText(old_value), ValueText(new_value)));
  }
}

std::string DescribeDifferences(const BasicInfoRecord &old_record,
                                const BasicInfoRecord &new_record) {
  std::vector<std::string> differences;
#define ARIES_ADD_BASIC_INFO_DIFFERENCE(field)                                 \
  AddDifference(differences, #field, old_record.field, new_record.field)
  ARIES_ADD_BASIC_INFO_DIFFERENCE(trading_day);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(market);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(symbol);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(industry_code);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(security_type);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(anomaly_code);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(stock_group_code);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(board_code);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(reference_price);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(high_limit);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(low_limit);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(abnormal_recommendation);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(special_abnormal);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(day_trading_code);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(margin_short_exempt);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(borrow_short_exempt);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(matching_cycle_seconds);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(warrant_flag);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(strike_price);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(previous_exercise_volume);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(previous_cancellation_volume);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(outstanding_volume);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(exercise_ratio);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(warrant_upper_price);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(warrant_lower_price);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(maturity_date);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(foreign_stock_flag);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(multiplier);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(currency);
  ARIES_ADD_BASIC_INFO_DIFFERENCE(market_data_line);
#undef ARIES_ADD_BASIC_INFO_DIFFERENCE
  return fmt::format("{}", fmt::join(differences, ", "));
}

std::size_t ServiceIndex(ServiceType service_type) {
  return service_type == ServiceType::kListed ? 0U : 1U;
}

} // namespace

DecodedBasicInfo DecodeBasicInfo(std::int32_t trading_day,
                                 const MessageHeader &header,
                                 std::span<const std::uint8_t> body) {
  ValidateTradingDay(trading_day);
  ValidateMessage(header, body);

  const auto stock_entries =
      DecodeCode(body.subspan(kStockEntriesOffset, 2), "stock_entries");
  if (!stock_entries.empty()) {
    BasicInfoControlKind kind;
    if (stock_entries == "AL") {
      kind = BasicInfoControlKind::kAll;
    } else if (stock_entries == "NE") {
      kind = BasicInfoControlKind::kNew;
    } else {
      throw std::runtime_error("format1 has unknown stock_entries code");
    }
    return DecodedBasicInfo{
        .record = std::nullopt,
        .control_kind = kind,
        .control_count = DecodeControlCount(body.first(protocol::kSymbolSize)),
    };
  }

  const bool listed = header.service_type == ServiceType::kListed;
  const std::size_t shift = listed ? 0U : 1U;
  BasicInfoRecord record{
      .trading_day = trading_day,
      .market = listed ? "TWSE" : "TPEX",
      .symbol = DecodeSymbol(body.first(protocol::kSymbolSize)),
      .industry_code =
          DecodeCode(body.subspan(kIndustryCodeOffset, 2), "industry_code"),
      .security_type =
          DecodeCode(body.subspan(kSecurityTypeOffset, 2), "security_type"),
      .anomaly_code = DecodeBcdInteger(body.subspan(kAnomalyCodeOffset, 1)),
      .stock_group_code =
          listed ? std::nullopt
                 : std::optional<std::string>{DecodeFlag(
                       body, kOtcStockGroupCodeOffset, "stock_group_code")},
      .board_code = DecodeFlag(
          body, listed ? kListedBoardCodeOffset : kOtcBoardCodeOffset,
          "board_code"),
      .reference_price = DecodeBcdDecimal(body.subspan(30 + shift, 5), 4),
      .high_limit = DecodeBcdDecimal(body.subspan(35 + shift, 5), 4),
      .low_limit = DecodeBcdDecimal(body.subspan(40 + shift, 5), 4),
      .abnormal_recommendation =
          DecodeFlag(body, 46 + shift, "abnormal_recommendation"),
      .special_abnormal = DecodeFlag(body, 47 + shift, "special_abnormal"),
      .day_trading_code = DecodeFlag(body, 48 + shift, "day_trading_code"),
      .margin_short_exempt =
          DecodeFlag(body, 49 + shift, "margin_short_exempt"),
      .borrow_short_exempt =
          DecodeFlag(body, 50 + shift, "borrow_short_exempt"),
      .matching_cycle_seconds = DecodeBcdInteger(body.subspan(51 + shift, 3)),
      .warrant_flag = DecodeFlag(body, 54 + shift, "warrant_flag"),
      .strike_price = std::nullopt,
      .previous_exercise_volume = std::nullopt,
      .previous_cancellation_volume = std::nullopt,
      .outstanding_volume = std::nullopt,
      .exercise_ratio = std::nullopt,
      .warrant_upper_price = std::nullopt,
      .warrant_lower_price = std::nullopt,
      .maturity_date = std::nullopt,
      .foreign_stock_flag = listed ? std::optional<std::string>{DecodeFlag(
                                         body, 93, "foreign_stock_flag")}
                                   : std::nullopt,
      .multiplier = DecodeBcdInteger(body.subspan(kMultiplierOffset, 3)),
      .currency = DecodeCode(body.subspan(kCurrencyOffset, 3), "currency"),
      .market_data_line =
          DecodeBcdInteger(body.subspan(kMarketDataLineOffset, 1)),
  };
  if (record.currency.empty()) {
    record.currency = "TWD";
  } else if (record.currency.size() != 3) {
    throw std::runtime_error("currency must contain a three-character code");
  }

  if (IsWarrantSecurity(record)) {
    record.strike_price = DecodeBcdDecimal(body.subspan(55 + shift, 5), 4);
    record.previous_exercise_volume =
        DecodeBcdInteger(body.subspan(60 + shift, 5)) * kWarrantVolumeScale;
    record.previous_cancellation_volume =
        DecodeBcdInteger(body.subspan(65 + shift, 5)) * kWarrantVolumeScale;
    record.outstanding_volume =
        DecodeBcdInteger(body.subspan(70 + shift, 5)) * kWarrantVolumeScale;
    record.exercise_ratio =
        DecodeBcdDecimal(body.subspan(75 + shift, 4), 2) / kWarrantRatioScale;
    record.warrant_upper_price =
        DecodeBcdDecimal(body.subspan(79 + shift, 5), 4);
    record.warrant_lower_price =
        DecodeBcdDecimal(body.subspan(84 + shift, 5), 4);
    const auto maturity_date = static_cast<std::int32_t>(
        DecodeBcdInteger(body.subspan(89 + shift, 4)));
    if (!IsValidCalendarDate(maturity_date)) {
      throw std::runtime_error("warrant maturity_date is invalid");
    }
    record.maturity_date = maturity_date;
  }

  return DecodedBasicInfo{.record = std::move(record)};
}

bool IsWarrantSecurity(const BasicInfoRecord &record) {
  return record.warrant_flag == "Y" ||
         (!record.security_type.empty() &&
          record.security_type.front() == 'W') ||
         record.market_data_line == 2;
}

BasicInfoCatalog::BasicInfoCatalog(std::int32_t trading_day)
    : trading_day_(trading_day) {
  ValidateTradingDay(trading_day_);
}

const BasicInfoRecord *
BasicInfoCatalog::Process(const MessageHeader &header,
                          std::span<const std::uint8_t> body,
                          std::uint64_t offset) {
  auto decoded = DecodeBasicInfo(trading_day_, header, body);
  const auto service_index = ServiceIndex(header.service_type);
  if (!decoded.record.has_value()) {
    if (cycle_synchronized_[service_index] &&
        cycle_messages_[service_index] != decoded.control_count) {
      throw std::runtime_error(fmt::format(
          "format1 cycle count mismatch service={} kind={} expected={} "
          "actual={} offset={} sequence={}",
          static_cast<unsigned>(header.service_type),
          decoded.control_kind == BasicInfoControlKind::kAll ? "AL" : "NE",
          decoded.control_count, cycle_messages_[service_index], offset,
          header.sequence));
    }
    cycle_messages_[service_index] = 0;
    cycle_synchronized_[service_index] = true;
    ++control_records_;
    return nullptr;
  }

  ++cycle_messages_[service_index];
  ++normal_messages_;
  auto record = std::move(*decoded.record);
  Key key{record.trading_day, record.market, record.symbol};
  auto iterator = records_.find(key);
  if (iterator == records_.end()) {
    iterator = records_
                   .emplace(std::move(key), Entry{.record = std::move(record),
                                                  .offset = offset,
                                                  .sequence = header.sequence})
                   .first;
    return &iterator->second.record;
  }
  if (iterator->second.record == record) {
    ++identical_duplicates_;
    return &iterator->second.record;
  }
  throw std::runtime_error(fmt::format(
      "format1 basic info conflict key=({},{},{}) old_offset={} "
      "old_sequence={} new_offset={} new_sequence={} differences=[{}]",
      iterator->second.record.trading_day, iterator->second.record.market,
      iterator->second.record.symbol, iterator->second.offset,
      iterator->second.sequence, offset, header.sequence,
      DescribeDifferences(iterator->second.record, record)));
}

std::vector<BasicInfoRecord> BasicInfoCatalog::records() const {
  std::vector<BasicInfoRecord> result;
  result.reserve(records_.size());
  for (const auto &[key, entry] : records_) {
    (void)key;
    result.push_back(entry.record);
  }
  return result;
}

} // namespace aries::data::twse
