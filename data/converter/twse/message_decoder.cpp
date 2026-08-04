#include "data/converter/twse/message_decoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

#include "data/converter/twse/basic_info.h"
#include "data/converter/twse/bcd_decoder.h"

namespace aries::data::twse {
namespace {

bool IsDigit(char value) {
  return value >= '0' && value <= '9';
}

bool IsPadding(char value) {
  return value == ' ' || value == '\0';
}

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

std::size_t ServiceIndex(ServiceType service_type) {
  return service_type == ServiceType::kListed ? 0U : 1U;
}

Market ToMarket(ServiceType service_type) {
  return service_type == ServiceType::kListed ? Market::kTwse : Market::kTpex;
}

ServiceType ToServiceType(std::string_view market) {
  if (market == "TWSE") {
    return ServiceType::kListed;
  }
  if (market == "TPEX") {
    return ServiceType::kOtc;
  }
  throw std::runtime_error("unsupported TWSE basic-info market");
}

bool IsDepthFormat(MessageType message_type) {
  return message_type == MessageType::kStockDepthV ||
         message_type == MessageType::kWarrantDepthV ||
         message_type == MessageType::kStockOddLotDepthV;
}

bool IsHeld(LimitState state) {
  const auto trend = state.instantaneous_trend();
  return trend == InstantaneousTrend::kHeldDown ||
         trend == InstantaneousTrend::kHeldUp;
}

bool IsSideEligible(SessionState state) {
  return !state.is_trial() && !state.is_opening() && !state.is_closing() &&
         state.matching_method() == MatchingMethod::kContinuous;
}

TradeSide InferTradeSide(bool odd_lot, bool side_eligible, bool book_valid,
                         double first_price, double best_ask_price,
                         std::int64_t best_ask_volume, double best_bid_price,
                         std::int64_t best_bid_volume) {
  if (odd_lot || !side_eligible || !book_valid || best_ask_price <= 0.0 ||
      best_ask_volume <= 0 || best_bid_price <= 0.0 || best_bid_volume <= 0 ||
      best_bid_price >= best_ask_price) {
    return TradeSide::kUnknown;
  }
  const bool buy = first_price >= best_ask_price;
  const bool sell = first_price <= best_bid_price;
  if (buy == sell) {
    return TradeSide::kUnknown;
  }
  return buy ? TradeSide::kBuy : TradeSide::kSell;
}

void CopySymbol(char (&output)[16], std::string_view symbol) {
  std::fill(std::begin(output), std::end(output), '\0');
  std::copy(symbol.begin(), symbol.end(), std::begin(output));
}

}  // namespace

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

std::string_view ToString(MatchGroupIssueReason reason) {
  switch (reason) {
    case MatchGroupIssueReason::kTimestampChanged:
      return "timestamp_changed";
    case MatchGroupIssueReason::kSourceFormatChanged:
      return "source_format_changed";
    case MatchGroupIssueReason::kSequenceGap:
      return "sequence_gap";
    case MatchGroupIssueReason::kEndOfFile:
      return "end_of_file";
    case MatchGroupIssueReason::kFrameCorruption:
      return "frame_corruption";
  }
  throw std::invalid_argument("invalid TWSE match-group issue reason");
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
      trading_day_(trading_day),
      mode_(mode) {}

const Orderbook<5> *MessageDecoder::Process(const MessageHeader &header,
                                            std::span<const std::uint8_t> body,
                                            std::int64_t local_ns) {
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
      return ProcessDepth(header, body, local_ns, false,
                          mode_ != SymbolFilterMode::kWarrant);
    case MessageType::kStockOddLotBasicInfo:
      if (mode_ == SymbolFilterMode::kOddLot) {
        ProcessOddLotBasicInfo(header, body);
      }
      return nullptr;
    case MessageType::kStockOddLotDepthV:
      if (mode_ != SymbolFilterMode::kOddLot) {
        return nullptr;
      }
      return ProcessDepth(header, body, local_ns, true, true);
    case MessageType::kWarrantDepthV:
      return ProcessDepth(header, body, local_ns, false,
                          mode_ == SymbolFilterMode::kWarrant);
    default:
      return nullptr;
  }
}

std::size_t MessageDecoder::symbol_count() const noexcept {
  return states_[0].size() + states_[1].size();
}

MessageDecoder::StockState *MessageDecoder::FindOrCreate(
    ServiceType service_type, std::span<const std::uint8_t> exchange_symbol) {
  const std::string_view raw_symbol(
      reinterpret_cast<const char *>(exchange_symbol.data()),
      exchange_symbol.size());
  auto symbol = NormalizeSymbol(exchange_symbol);
  const auto index = ServiceIndex(service_type);
  const bool metadata_warrant = mode_ == SymbolFilterMode::kWarrant &&
                                warrant_symbols_[index].contains(symbol);
  if (!MatchesSymbol(mode_, raw_symbol) && !metadata_warrant) {
    return nullptr;
  }

  auto [iterator, inserted] = states_[index].try_emplace(symbol);
  if (inserted) {
    auto &state = iterator->second;
    state.symbol = symbol;
    state.symbol_id = next_symbol_id_++;
    state.market = ToMarket(service_type);
    state.invalidated = invalidated_symbols_[index].contains(symbol);
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
  const auto service_type = ToServiceType(basic_info.market);
  if (IsWarrantSecurity(basic_info)) {
    warrant_symbols_[ServiceIndex(service_type)].insert(basic_info.symbol);
  }

  std::string exchange_symbol = basic_info.symbol;
  if (exchange_symbol.size() > protocol::kSymbolSize) {
    throw std::runtime_error("TWSE basic-info symbol exceeds six bytes");
  }
  exchange_symbol.resize(protocol::kSymbolSize, ' ');
  auto *state = FindOrCreate(
      service_type,
      std::span<const std::uint8_t>(
          reinterpret_cast<const std::uint8_t *>(exchange_symbol.data()),
          exchange_symbol.size()));
  if (state == nullptr) {
    return;
  }
  if (basic_info.multiplier == 0) {
    const auto index = ServiceIndex(service_type);
    if (missing_multiplier_symbols_[index].insert(state->symbol).second) {
      stats_.missing_multiplier_symbols.push_back(
          {.market = state->market, .symbol = state->symbol});
    }
    state->invalidated = true;
    invalidated_symbols_[index].insert(state->symbol);
    return;
  }
  state->multiplier = basic_info.multiplier;
  state->multiplier_known = true;
}

void MessageDecoder::ProcessOddLotBasicInfo(
    const MessageHeader &header, std::span<const std::uint8_t> body) {
  RequireBodySize(body, protocol::kOddLotBasicBodySize, "odd-lot basic info");
  RequireTrailer(body);
  (void)FindOrCreate(header.service_type, body.first(protocol::kSymbolSize));
}

void MessageDecoder::AuditSequence(const MessageHeader &header) {
  if (!IsDepthFormat(header.message_type)) {
    return;
  }
  const auto market_index = ServiceIndex(header.service_type);
  const auto format_index = static_cast<std::size_t>(header.message_type);
  auto &sequence = sequences_[market_index][format_index];
  if (sequence.initialized && header.sequence != sequence.last_sequence + 1) {
    stats_.sequence_gaps.push_back({
        .market = ToMarket(header.service_type),
        .source_format = header.message_type,
        .expected_sequence = sequence.last_sequence + 1,
        .actual_sequence = header.sequence,
    });
    for (auto &[symbol, state] : states_[market_index]) {
      (void)symbol;
      if (state.pending.active &&
          state.pending.source_format == header.message_type) {
        state.pending.has_gap = true;
      }
    }
  }
  sequence.initialized = true;
  sequence.last_sequence = header.sequence;
}

void MessageDecoder::RecordIncomplete(StockState &state,
                                      MatchGroupIssueReason reason) {
  if (!state.pending.active) {
    return;
  }
  stats_.incomplete_match_groups.push_back({
      .trading_day = trading_day_,
      .market = state.market,
      .symbol = state.symbol,
      .source_format = state.pending.source_format,
      .first_sequence = state.pending.first_sequence,
      .last_sequence = state.pending.last_sequence,
      .trade_count = state.pending.trade_count,
      .trade_volume = state.pending.trade_volume,
      .reason = reason,
  });
}

void MessageDecoder::InvalidateSymbol(ServiceType service_type,
                                      std::string symbol) {
  if (symbol.empty()) {
    return;
  }
  const auto index = ServiceIndex(service_type);
  invalidated_symbols_[index].insert(symbol);
  const auto iterator = states_[index].find(symbol);
  if (iterator != states_[index].end()) {
    auto &state = iterator->second;
    if (state.pending.active) {
      RecordIncomplete(state, MatchGroupIssueReason::kFrameCorruption);
      state.pending = {};
    }
    state.invalidated = true;
  }
}

void MessageDecoder::Finalize() {
  if (finalized_) {
    return;
  }
  for (auto &market_states : states_) {
    for (auto &[symbol, state] : market_states) {
      (void)symbol;
      if (state.pending.active) {
        RecordIncomplete(state, MatchGroupIssueReason::kEndOfFile);
        state.pending = {};
      }
    }
  }
  finalized_ = true;
}

const Orderbook<5> *MessageDecoder::ProcessDepth(
    const MessageHeader &header, std::span<const std::uint8_t> body,
    std::int64_t local_ns, bool odd_lot, bool emit) {
  const auto info_size =
      odd_lot ? protocol::kOddLotDepthInfoSize : protocol::kStockDepthInfoSize;
  const auto level_size =
      odd_lot ? protocol::kOddLotLevelSize : protocol::kStockLevelSize;
  const auto volume_size = odd_lot ? std::size_t{6} : std::size_t{4};
  if (body.size() < info_size + protocol::kMessageTrailerSize) {
    throw std::runtime_error("stock depth body is too short");
  }

  const DisclosureState disclosure{body[protocol::kDepthDataFlagOffset]};
  const LimitState limit_state{body[protocol::kDepthLimitFlagOffset]};
  const SessionState session_state{body[protocol::kDepthStatusOffset]};
  const auto ask_levels =
      static_cast<std::size_t>(disclosure.ask_level_count());
  const auto bid_levels =
      static_cast<std::size_t>(disclosure.bid_level_count());
  if (ask_levels > protocol::kMaximumDepthLevels ||
      bid_levels > protocol::kMaximumDepthLevels) {
    throw std::runtime_error("stock depth contains more than five levels");
  }

  const auto encoded_level_count =
      static_cast<std::size_t>(disclosure.has_trade()) + bid_levels +
      ask_levels;
  const auto expected_size = info_size + encoded_level_count * level_size +
                             protocol::kMessageTrailerSize;
  RequireBodySize(body, expected_size, "stock depth");
  RequireTrailer(body);
  AuditSequence(header);

  const auto total_volume_offset = odd_lot ? protocol::kOddLotTotalVolumeOffset
                                           : protocol::kStockTotalVolumeOffset;
  const auto total_volume = static_cast<std::int64_t>(
      DecodeBcdInteger(body.subspan(total_volume_offset, volume_size)));

  double trade_price = 0.0;
  std::int64_t trade_volume = 0;
  std::array<double, 5> bid_price{};
  std::array<std::int64_t, 5> bid_volume{};
  std::array<double, 5> ask_price{};
  std::array<std::int64_t, 5> ask_volume{};
  auto offset = info_size;
  if (disclosure.has_trade()) {
    const auto trade = DecodeLevel(body, offset, volume_size);
    trade_price = trade.first;
    trade_volume = trade.second;
    offset += level_size;
  }
  for (std::size_t index = 0; index < bid_levels; ++index) {
    const auto level = DecodeLevel(body, offset, volume_size);
    bid_price[index] = level.first;
    bid_volume[index] = level.second;
    offset += level_size;
  }
  for (std::size_t index = 0; index < ask_levels; ++index) {
    const auto level = DecodeLevel(body, offset, volume_size);
    ask_price[index] = level.first;
    ask_volume[index] = level.second;
    offset += level_size;
  }

  const bool held = IsHeld(limit_state);
  const bool actual_trade =
      disclosure.has_trade() && !session_state.is_trial() && !held;
  if (actual_trade) {
    ++stats_.source_actual_trade_payloads;
  }

  auto *state =
      FindOrCreate(header.service_type, body.first(protocol::kSymbolSize));
  if (state == nullptr) {
    return nullptr;
  }
  if (state->invalidated) {
    ++stats_.invalidated_symbol_messages;
    return nullptr;
  }
  if (!odd_lot && !state->multiplier_known) {
    const auto index = ServiceIndex(header.service_type);
    if (missing_multiplier_symbols_[index].insert(state->symbol).second) {
      stats_.missing_multiplier_symbols.push_back(
          {.market = state->market, .symbol = state->symbol});
    }
    ++stats_.missing_multiplier_messages;
    if (total_volume != 0 || actual_trade) {
      state->invalidated = true;
      invalidated_symbols_[index].insert(state->symbol);
      return nullptr;
    }
  }
  if (session_state.is_trial()) {
    return nullptr;
  }

  const auto exchange_ns =
      trading_day_start_ns_ +
      DecodeBcdTimeNanoseconds(body.subspan(protocol::kDepthTimeOffset, 6));

  const bool grouped_format =
      !odd_lot && (header.message_type == MessageType::kStockDepthV ||
                   header.message_type == MessageType::kWarrantDepthV);
  const bool trade_only = grouped_format && disclosure.has_trade() &&
                          disclosure.disclosure_tag() && !held;

  if (state->pending.active &&
      state->pending.source_format != header.message_type) {
    RecordIncomplete(*state, MatchGroupIssueReason::kSourceFormatChanged);
    state->pending = {};
    state->book.valid = false;
  } else if (state->pending.active &&
             state->pending.exchange_ns != exchange_ns) {
    RecordIncomplete(*state, state->pending.has_gap
                                 ? MatchGroupIssueReason::kSequenceGap
                                 : MatchGroupIssueReason::kTimestampChanged);
    state->pending = {};
    state->book.valid = false;
  }

  if (actual_trade) {
    if (!state->pending.active) {
      state->pending.active = true;
      state->pending.source_format = header.message_type;
      state->pending.exchange_ns = exchange_ns;
      state->pending.first_sequence = header.sequence;
      state->pending.pre_book = state->book;
      state->pending.side_eligible = IsSideEligible(session_state);
      state->pending.first_price = trade_price;
    }
    state->pending.last_sequence = header.sequence;
    state->pending.side_eligible =
        state->pending.side_eligible && IsSideEligible(session_state);
    ++state->pending.trade_count;
    state->pending.trade_volume += trade_volume;

    state->last_price = trade_price;
    if (state->open == 0.0) {
      state->open = trade_price;
      state->high = trade_price;
      state->low = trade_price;
    } else {
      state->high = std::max(state->high, trade_price);
      state->low = std::min(state->low, trade_price);
    }
    const auto multiplier = odd_lot ? std::uint64_t{1} : state->multiplier;
    state->interval_observed_volume += trade_volume;
    state->interval_observed_value += trade_price *
                                      static_cast<double>(trade_volume) *
                                      static_cast<double>(multiplier);
    ++stats_.actual_trade_payloads;
  }

  if (trade_only) {
    return nullptr;
  }

  if (state->pending.active) {
    state->pending.last_sequence = header.sequence;
    if (!held) {
      state->pending.side_eligible =
          state->pending.side_eligible && IsSideEligible(session_state);
    }
  }

  const auto multiplier = odd_lot ? std::uint64_t{1} : state->multiplier;
  const auto volume_difference = total_volume - state->accepted_total_volume;
  const auto missing_volume =
      volume_difference - state->interval_observed_volume;
  if (missing_volume > 0) {
    stats_.value_imputations.push_back({
        .market = state->market,
        .symbol = state->symbol,
        .source_sequence = header.sequence,
        .volume_difference = volume_difference,
        .observed_volume = state->interval_observed_volume,
        .missing_volume = missing_volume,
        .price = state->last_price,
    });
  }
  state->total_value += state->interval_observed_value +
                        static_cast<double>(missing_volume) *
                            state->last_price * static_cast<double>(multiplier);
  state->accepted_total_volume = total_volume;
  state->interval_observed_volume = 0;
  state->interval_observed_value = 0.0;

  if (held) {
    state->book = {};
  } else {
    state->book.ask_price = ask_price;
    state->book.ask_volume = ask_volume;
    state->book.bid_price = bid_price;
    state->book.bid_volume = bid_volume;
    state->book.valid = true;
  }

  const bool incomplete_group = state->pending.active && state->pending.has_gap;
  if (incomplete_group) {
    RecordIncomplete(*state, MatchGroupIssueReason::kSequenceGap);
    state->pending = {};
    return nullptr;
  }

  const auto trade_count =
      state->pending.active ? state->pending.trade_count : 0U;
  const auto event_trade_volume =
      state->pending.active ? state->pending.trade_volume : 0;
  TradeSide trade_side = TradeSide::kUnknown;
  if (state->pending.active) {
    const auto &pre_book = state->pending.pre_book;
    trade_side = InferTradeSide(odd_lot, state->pending.side_eligible,
                                pre_book.valid, state->pending.first_price,
                                pre_book.ask_price[0], pre_book.ask_volume[0],
                                pre_book.bid_price[0], pre_book.bid_volume[0]);
  }

  auto &output = state->output;
  CopySymbol(output.symbol, state->symbol);
  output.exchange_ns = exchange_ns;
  output.local_ns = local_ns;
  output.symbol_id = state->symbol_id;
  output.market = state->market;
  output.disclosure = disclosure;
  output.limit_state = limit_state;
  output.session_state = session_state;
  output.trade_side = trade_side;
  output.trade_count = trade_count;
  output.last_price = state->last_price;
  output.open = state->open;
  output.high = state->high;
  output.low = state->low;
  output.trade_volume = event_trade_volume;
  output.total_volume = state->accepted_total_volume;
  output.total_value = state->total_value;
  for (std::size_t index = 0; index < 5; ++index) {
    output.ask_price[index] = state->book.ask_price[index];
    output.ask_volume[index] = state->book.ask_volume[index];
    output.bid_price[index] = state->book.bid_price[index];
    output.bid_volume[index] = state->book.bid_volume[index];
  }
  output.source_sequence = header.sequence;

  if (emit) {
    ++stats_.published_rows;
    if (trade_count > 0) {
      ++stats_.match_groups;
      if (trade_count > 1) {
        ++stats_.multi_trade_groups;
        stats_.trades_in_multi_groups += trade_count;
      }
      if (held) {
        ++stats_.held_ended_groups;
      }
      switch (trade_side) {
        case TradeSide::kBuy:
          ++stats_.buy_groups;
          break;
        case TradeSide::kSell:
          ++stats_.sell_groups;
          break;
        case TradeSide::kUnknown:
          ++stats_.unknown_groups;
          break;
      }
    }
  }
  state->pending = {};
  return emit ? &output : nullptr;
}

}  // namespace aries::data::twse
