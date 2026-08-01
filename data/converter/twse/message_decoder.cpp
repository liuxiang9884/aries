#include "data/converter/twse/message_decoder.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

#include "data/converter/twse/basic_info.h"
#include "data/converter/twse/bcd_decoder.h"

namespace aries::data::twse {
namespace {

bool IsDigit(char value) { return value >= '0' && value <= '9'; }

bool IsPadding(char value) { return value == ' ' || value == '\0'; }

bool IsStock(std::string_view symbol) {
  return symbol.size() == protocol::kSymbolSize && symbol[4] == ' ';
}

bool IsETF(std::string_view symbol) {
  if (symbol.size() != protocol::kSymbolSize || symbol[0] != '0' ||
      symbol[1] != '0' || symbol[2] < '4' || symbol[2] > '9' ||
      !IsDigit(symbol[3])) {
    return false;
  }
  if (IsPadding(symbol[4])) {
    return true;
  }
  if (!IsDigit(symbol[4])) {
    return false;
  }
  if (IsPadding(symbol[5]) || IsDigit(symbol[5])) {
    return true;
  }
  constexpr std::string_view kETFTypes = "ABCDKLMRSTUV";
  return kETFTypes.find(symbol[5]) != std::string_view::npos;
}

bool IsWarrant(std::string_view symbol) {
  if (symbol.size() != protocol::kSymbolSize) {
    return false;
  }
  if (!std::all_of(symbol.begin(), symbol.begin() + 5, IsDigit)) {
    return false;
  }
  return IsDigit(symbol[5]) || symbol[5] == 'P' || symbol[5] == 'U' ||
         symbol[5] == 'T';
}

std::string NormalizeSymbol(std::span<const std::uint8_t> symbol) {
  if (symbol.size() != protocol::kSymbolSize) {
    throw std::runtime_error("TWSE symbol must contain 6 bytes");
  }
  auto length = symbol.size();
  while (length > 0 && (symbol[length - 1] == static_cast<std::uint8_t>(' ') ||
                        symbol[length - 1] == 0)) {
    --length;
  }
  if (length == 0) {
    throw std::runtime_error("TWSE symbol is empty");
  }
  return std::string(reinterpret_cast<const char *>(symbol.data()), length);
}

void RequireBodySize(std::span<const std::uint8_t> body, std::size_t expected,
                     std::string_view message_name) {
  if (body.size() != expected) {
    throw std::runtime_error(std::string(message_name) +
                             " body has invalid length");
  }
}

void RequireTrailer(std::span<const std::uint8_t> body) {
  if (body.size() < protocol::kMessageTrailerSize ||
      body[body.size() - 2] != '\r' || body[body.size() - 1] != '\n') {
    throw std::runtime_error("TWSE message has invalid trailer");
  }
}

double DecodePrice(std::span<const std::uint8_t> bytes) {
  return DecodeBcdDecimal(bytes, 4);
}

std::pair<double, std::int64_t> DecodeLevel(std::span<const std::uint8_t> body,
                                            std::size_t offset,
                                            std::size_t volume_size) {
  const auto price = DecodePrice(body.subspan(offset, 5));
  const auto volume = DecodeBcdInteger(body.subspan(offset + 5, volume_size));
  return {price, static_cast<std::int64_t>(volume)};
}

void RequireSupportedFormatVersion(const MessageHeader &header) {
  std::uint8_t expected_version;
  switch (header.message_type) {
  case MessageType::kStockBasicInfo:
    expected_version = protocol::kStockBasicFormatVersion;
    break;
  case MessageType::kStockDepthV:
  case MessageType::kWarrantDepthV:
    expected_version = protocol::kStockDepthFormatVersion;
    break;
  case MessageType::kStockOddLotBasicInfo:
  case MessageType::kStockOddLotDepthV:
    expected_version = protocol::kOddLotFormatVersion;
    break;
  default:
    return;
  }
  if (header.format_version != expected_version) {
    throw std::runtime_error("unsupported TWSE message format version");
  }
}

} // namespace

SymbolFilterMode ParseSymbolFilterMode(std::string_view mode) {
  if (mode == "all") {
    return SymbolFilterMode::kAll;
  }
  if (mode == "etf") {
    return SymbolFilterMode::kETF;
  }
  if (mode == "warrant") {
    return SymbolFilterMode::kWarrant;
  }
  if (mode == "odd_lot") {
    return SymbolFilterMode::kOddLot;
  }
  if (mode == "stock") {
    return SymbolFilterMode::kStock;
  }
  throw std::invalid_argument("unknown TWSE symbol filter mode");
}

std::string_view ToString(SymbolFilterMode mode) {
  switch (mode) {
  case SymbolFilterMode::kAll:
    return "all";
  case SymbolFilterMode::kETF:
    return "etf";
  case SymbolFilterMode::kWarrant:
    return "warrant";
  case SymbolFilterMode::kOddLot:
    return "odd_lot";
  case SymbolFilterMode::kStock:
    return "stock";
  }
  throw std::invalid_argument("invalid TWSE symbol filter mode");
}

bool MatchesSymbol(SymbolFilterMode mode, std::string_view exchange_symbol) {
  if (exchange_symbol.size() != protocol::kSymbolSize) {
    return false;
  }
  if (exchange_symbol == "000000") {
    return false;
  }
  switch (mode) {
  case SymbolFilterMode::kAll:
    return true;
  case SymbolFilterMode::kETF:
    return IsETF(exchange_symbol);
  case SymbolFilterMode::kWarrant:
    return IsStock(exchange_symbol) || IsWarrant(exchange_symbol);
  case SymbolFilterMode::kOddLot:
  case SymbolFilterMode::kStock:
    return IsStock(exchange_symbol);
  }
  return false;
}

MessageHeader DecodeMessageHeader(std::span<const std::uint8_t> bytes) {
  if (bytes.size() != protocol::kHeaderSize) {
    throw std::runtime_error("TWSE header must contain 10 bytes");
  }
  if (bytes[0] != protocol::kEscape) {
    throw std::runtime_error("TWSE header has invalid escape byte");
  }

  const auto message_length = DecodeBcdInteger(bytes.subspan(1, 2));
  if (message_length < protocol::kHeaderSize ||
      message_length > protocol::kMaximumMessageSize) {
    throw std::runtime_error("TWSE message length is out of range");
  }

  const auto service_type = DecodeBcdInteger(bytes.subspan(3, 1));
  if (service_type != static_cast<std::uint8_t>(ServiceType::kListed) &&
      service_type != static_cast<std::uint8_t>(ServiceType::kOtc)) {
    throw std::runtime_error("TWSE message has unsupported service type");
  }

  return MessageHeader{
      .message_length = static_cast<std::size_t>(message_length),
      .service_type = static_cast<ServiceType>(service_type),
      .message_type =
          static_cast<MessageType>(DecodeBcdInteger(bytes.subspan(4, 1))),
      .format_version =
          static_cast<std::uint8_t>(DecodeBcdInteger(bytes.subspan(5, 1))),
      .sequence = DecodeBcdInteger(bytes.subspan(6, 4)),
  };
}

std::int64_t TradingDayStartNanoseconds(std::int32_t trading_day) {
  const auto year_value = trading_day / 10'000;
  const auto month_value = trading_day / 100 % 100;
  const auto day_value = trading_day % 100;
  const auto date = std::chrono::year{year_value} /
                    std::chrono::month{static_cast<unsigned>(month_value)} /
                    std::chrono::day{static_cast<unsigned>(day_value)};
  if (!date.ok()) {
    throw std::invalid_argument("invalid trading day");
  }

  const auto utc_time = std::chrono::sys_days{date} - std::chrono::hours{8};
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             utc_time.time_since_epoch())
      .count();
}

MessageDecoder::MessageDecoder(std::int32_t trading_day, SymbolFilterMode mode)
    : trading_day_start_ns_(TradingDayStartNanoseconds(trading_day)),
      trading_day_(trading_day), mode_(mode) {}

const Orderbook<5> *
MessageDecoder::Process(const MessageHeader &header,
                        std::span<const std::uint8_t> body) {
  if (header.message_length != protocol::kHeaderSize + body.size()) {
    throw std::runtime_error("TWSE header and body lengths do not match");
  }
  RequireSupportedFormatVersion(header);

  switch (header.message_type) {
  case MessageType::kStockBasicInfo:
    if (mode_ != SymbolFilterMode::kOddLot) {
      ProcessBasicInfo(header, body);
    }
    return nullptr;
  case MessageType::kStockDepthV:
    if (mode_ == SymbolFilterMode::kOddLot) {
      return nullptr;
    }
    return ProcessDepth(header, body, false,
                        mode_ != SymbolFilterMode::kWarrant);
  case MessageType::kStockOddLotBasicInfo:
    if (mode_ == SymbolFilterMode::kOddLot) {
      ProcessOddLotBasicInfo(body);
    }
    return nullptr;
  case MessageType::kStockOddLotDepthV:
    if (mode_ != SymbolFilterMode::kOddLot) {
      return nullptr;
    }
    return ProcessDepth(header, body, true, true);
  case MessageType::kWarrantDepthV:
    return ProcessDepth(header, body, false,
                        mode_ == SymbolFilterMode::kWarrant);
  default:
    return nullptr;
  }
}

Orderbook<5> *
MessageDecoder::FindOrCreate(std::span<const std::uint8_t> exchange_symbol) {
  const std::string_view raw_symbol(
      reinterpret_cast<const char *>(exchange_symbol.data()),
      exchange_symbol.size());
  auto symbol = NormalizeSymbol(exchange_symbol);
  const bool metadata_warrant =
      mode_ == SymbolFilterMode::kWarrant && warrant_symbols_.contains(symbol);
  if (!MatchesSymbol(mode_, raw_symbol) && !metadata_warrant) {
    return nullptr;
  }

  auto [iterator, inserted] = records_.try_emplace(symbol);
  if (inserted) {
    iterator->second.symbol = std::move(symbol);
  }
  return &iterator->second;
}

void MessageDecoder::ProcessBasicInfo(const MessageHeader &header,
                                      std::span<const std::uint8_t> body) {
  const auto decoded = DecodeBasicInfo(trading_day_, header, body);
  if (decoded.record.has_value()) {
    ApplyBasicInfo(*decoded.record);
  }
}

void MessageDecoder::ApplyBasicInfo(const BasicInfoRecord &basic_info) {
  if (IsWarrantSecurity(basic_info)) {
    warrant_symbols_.insert(basic_info.symbol);
  }

  std::string exchange_symbol = basic_info.symbol;
  if (exchange_symbol.size() > protocol::kSymbolSize) {
    throw std::runtime_error("TWSE basic-info symbol exceeds six bytes");
  }
  exchange_symbol.resize(protocol::kSymbolSize, ' ');
  auto *record = FindOrCreate(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t *>(exchange_symbol.data()),
      exchange_symbol.size()));
  if (record == nullptr) {
    return;
  }
  record->previous_close = basic_info.reference_price;
  record->high_limit = basic_info.high_limit;
  record->low_limit = basic_info.low_limit;
  if (basic_info.multiplier == 0) {
    missing_multiplier_symbols_.insert(record->symbol);
    invalidated_symbols_.insert(record->symbol);
    return;
  }
  record->multiplier = basic_info.multiplier;
}

void MessageDecoder::InvalidateSymbol(std::string symbol) {
  if (!symbol.empty()) {
    invalidated_symbols_.insert(std::move(symbol));
  }
}

void MessageDecoder::ProcessOddLotBasicInfo(
    std::span<const std::uint8_t> body) {
  RequireBodySize(body, protocol::kOddLotBasicBodySize, "odd-lot basic info");
  RequireTrailer(body);
  auto *record = FindOrCreate(body.first(protocol::kSymbolSize));
  if (record == nullptr) {
    return;
  }

  record->previous_close =
      DecodePrice(body.subspan(protocol::kOddLotPreviousCloseOffset, 5));
  record->high_limit =
      DecodePrice(body.subspan(protocol::kOddLotHighLimitOffset, 5));
  record->low_limit =
      DecodePrice(body.subspan(protocol::kOddLotLowLimitOffset, 5));
}

const Orderbook<5> *
MessageDecoder::ProcessDepth(const MessageHeader &header,
                             std::span<const std::uint8_t> body, bool odd_lot,
                             bool emit) {
  const auto info_size =
      odd_lot ? protocol::kOddLotDepthInfoSize : protocol::kStockDepthInfoSize;
  const auto level_size =
      odd_lot ? protocol::kOddLotLevelSize : protocol::kStockLevelSize;
  const auto volume_size = odd_lot ? std::size_t{6} : std::size_t{4};
  if (body.size() < info_size + protocol::kMessageTrailerSize) {
    throw std::runtime_error("stock depth body is too short");
  }

  const auto data_flag = body[protocol::kDepthDataFlagOffset];
  const auto ask_levels = static_cast<std::size_t>((data_flag >> 1U) & 0x07U);
  const auto bid_levels = static_cast<std::size_t>((data_flag >> 4U) & 0x07U);
  const auto is_traded = (data_flag & 0x80U) != 0;
  if (ask_levels > protocol::kMaximumDepthLevels ||
      bid_levels > protocol::kMaximumDepthLevels) {
    throw std::runtime_error("stock depth contains more than five levels");
  }

  const auto encoded_level_count =
      static_cast<std::size_t>(is_traded) + bid_levels + ask_levels;
  const auto expected_size = info_size + encoded_level_count * level_size +
                             protocol::kMessageTrailerSize;
  RequireBodySize(body, expected_size, "stock depth");
  RequireTrailer(body);

  auto *record = FindOrCreate(body.first(protocol::kSymbolSize));
  if (record == nullptr) {
    return nullptr;
  }

  if (invalidated_symbols_.contains(record->symbol)) {
    ++invalidated_symbol_messages_;
    return nullptr;
  }

  const auto total_volume_offset = odd_lot ? protocol::kOddLotTotalVolumeOffset
                                           : protocol::kStockTotalVolumeOffset;
  const auto total_volume = static_cast<std::int64_t>(
      DecodeBcdInteger(body.subspan(total_volume_offset, volume_size)));
  if (!odd_lot && record->multiplier == 0) {
    missing_multiplier_symbols_.insert(record->symbol);
    ++missing_multiplier_messages_;
    if (total_volume != 0) {
      invalidated_symbols_.insert(record->symbol);
    }
    return nullptr;
  }

  record->exchtime =
      trading_day_start_ns_ +
      DecodeBcdTimeNanoseconds(body.subspan(protocol::kDepthTimeOffset, 6));
  record->localtime = record->exchtime;
  const auto limit_flag = body[protocol::kDepthLimitFlagOffset];
  const auto market_status = body[protocol::kDepthStatusOffset];
  record->status = static_cast<std::int64_t>(data_flag) |
                   (static_cast<std::int64_t>(limit_flag) << 8U) |
                   (static_cast<std::int64_t>(market_status) << 16U);

  auto offset = info_size;
  if ((market_status & 0x08U) != 0) {
    if (is_traded) {
      record->open = DecodePrice(body.subspan(offset, 5));
    } else {
      record->last_price = 0.0;
    }
  }

  if (is_traded) {
    const auto [price, volume] = DecodeLevel(body, offset, volume_size);
    record->last_price = price;
    record->total_trade = volume;
    offset += level_size;
  } else {
    record->total_trade = 0;
  }

  const auto effective_multiplier =
      odd_lot ? std::uint64_t{1} : record->multiplier;
  record->total_value +=
      static_cast<double>(total_volume - record->total_volume) *
      record->last_price * static_cast<double>(effective_multiplier);
  record->total_volume = total_volume;

  if (record->open == 0.0 && record->total_volume != 0) {
    record->open = record->last_price;
  }

  for (std::size_t i = 0; i < bid_levels; ++i) {
    const auto [price, volume] = DecodeLevel(body, offset, volume_size);
    record->bid_price[i] = price;
    record->bid_volume[i] = volume;
    offset += level_size;
  }
  std::fill(record->bid_price.begin() + bid_levels, record->bid_price.end(),
            0.0);
  std::fill(record->bid_volume.begin() + bid_levels, record->bid_volume.end(),
            0);

  for (std::size_t i = 0; i < ask_levels; ++i) {
    const auto [price, volume] = DecodeLevel(body, offset, volume_size);
    record->ask_price[i] = price;
    record->ask_volume[i] = volume;
    offset += level_size;
  }
  std::fill(record->ask_price.begin() + ask_levels, record->ask_price.end(),
            0.0);
  std::fill(record->ask_volume.begin() + ask_levels, record->ask_volume.end(),
            0);

  record->sequence = static_cast<std::int64_t>(header.sequence);
  return emit ? record : nullptr;
}

} // namespace aries::data::twse
