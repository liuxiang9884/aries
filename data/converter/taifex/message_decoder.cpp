#include "data/converter/taifex/message_decoder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include "data/converter/taifex/bcd_decoder.h"

namespace aries::data::taifex {
namespace {

struct ProductInfo {
  std::string symbol;
  std::uint64_t reference_price_raw{};
  char contract_type{};
  std::uint8_t decimal_locator{};
  std::uint8_t strike_decimal_locator{};
  std::int32_t listing_date{};
  std::int32_t delisting_date{};
  std::uint8_t flow_group{};
  std::int32_t delivery_date{};
  char dynamic_banding{};

  bool operator==(const ProductInfo &) const = default;
};

struct ContractInfo {
  std::string kind_id;
  std::string stock_id;
  char contract_subtype{};
  std::uint64_t contract_size_raw{};
  char contract_status{};
  char currency_code{};
  std::uint8_t decimal_locator{};
  std::uint8_t strike_decimal_locator{};
  char quote_flag{};
  std::string begin_date;
  char block_trade_flag{};
  char expiry_type{};
  char underlying_type{};
  std::uint8_t close_group{};
  char end_session{};

  [[nodiscard]] double multiplier() const {
    return static_cast<double>(contract_size_raw) / 10'000.0;
  }

  bool operator==(const ContractInfo &) const = default;
};

struct BookLevel {
  char type{};
  double price{};
  std::int64_t volume{};
  std::uint8_t level{};
};

struct IncrementalLevel : BookLevel {
  char action{};
};

struct EventMeta {
  std::string symbol;
  std::uint64_t sequence{};
  std::int64_t exchtime{};
};

struct TradeEvent {
  EventMeta meta;
  bool trial{};
  std::vector<std::pair<double, std::int64_t>> trades;
  std::int64_t total_volume{};
  std::int64_t total_buy_count{};
  std::int64_t total_sell_count{};
};

struct HighLowEvent {
  EventMeta meta;
  double high{};
  double low{};
};

struct IncrementalEvent {
  EventMeta meta;
  std::vector<IncrementalLevel> levels;
};

struct FullBookEvent {
  EventMeta meta;
  bool trial{};
  std::vector<BookLevel> levels;
};

using SequencedEvent =
    std::variant<TradeEvent, HighLowEvent, IncrementalEvent, FullBookEvent>;

std::string NormalizeText(std::span<const std::uint8_t> bytes,
                          std::string_view field) {
  auto length = bytes.size();
  while (length > 0 && (bytes[length - 1] == static_cast<std::uint8_t>(' ') ||
                        bytes[length - 1] == 0)) {
    --length;
  }
  if (length == 0) {
    if (field == "stock_id") {
      return {};
    }
    throw std::runtime_error(fmt::format("TAIFEX {} is empty", field));
  }
  for (const auto byte : bytes.first(length)) {
    if (byte < 0x20 || byte > 0x7E || byte == ',') {
      throw std::runtime_error(
          fmt::format("TAIFEX {} contains an invalid CSV byte", field));
    }
  }
  return std::string(reinterpret_cast<const char *>(bytes.data()), length);
}

std::string KindId(std::string_view symbol) {
  if (symbol.size() < 3) {
    throw std::runtime_error("TAIFEX product symbol is shorter than kind id");
  }
  return std::string(symbol.substr(0, 3));
}

void RequireBodySize(std::span<const std::uint8_t> body, std::size_t expected,
                     std::string_view name) {
  if (body.size() != expected) {
    throw std::runtime_error(
        fmt::format("TAIFEX {} body has invalid length expected={} actual={}",
                    name, expected, body.size()));
  }
}

void RequireVersion(const MessageHeader &header, std::uint8_t expected,
                    std::string_view name) {
  if (header.version != expected) {
    throw std::runtime_error(
        fmt::format("unsupported TAIFEX {} version expected={} actual={}", name,
                    expected, header.version));
  }
}

std::int32_t DecodeDate(std::span<const std::uint8_t> bytes,
                        std::string_view field) {
  const auto raw = DecodeBcdInteger(bytes);
  if (raw > 99'991'231ULL) {
    throw std::runtime_error(fmt::format("TAIFEX {} is out of range", field));
  }
  const auto value = static_cast<std::int32_t>(raw);
  const auto date =
      std::chrono::year{value / 10'000} /
      std::chrono::month{static_cast<unsigned>(value / 100 % 100)} /
      std::chrono::day{static_cast<unsigned>(value % 100)};
  if (!date.ok()) {
    throw std::runtime_error(fmt::format("TAIFEX {} is invalid", field));
  }
  return value;
}

double DecodeSignedPrice(char sign, std::span<const std::uint8_t> bytes,
                         std::uint8_t locator) {
  const auto price = DecodeBcdDecimal(bytes, locator);
  if (sign == '0') {
    return price;
  }
  if (sign == '-') {
    return -price;
  }
  throw std::runtime_error("TAIFEX price sign is invalid");
}

std::string Currency(char code) {
  switch (code) {
  case '1':
    return "TWD";
  case '2':
    return "USD";
  case '3':
    return "EUR";
  case '4':
    return "JPY";
  case '5':
    return "GBP";
  case '6':
    return "AUD";
  case '7':
    return "HKD";
  case '8':
    return "CNY";
  default:
    throw std::runtime_error("TAIFEX currency code is invalid");
  }
}

void ValidateContractType(char value) {
  constexpr std::string_view kTypes = "IRBCSE";
  if (kTypes.find(value) == std::string_view::npos) {
    throw std::runtime_error("TAIFEX contract type is invalid");
  }
}

void ValidateBinaryFlag(char value, std::string_view field) {
  if (value != '0' && value != '1') {
    throw std::runtime_error(fmt::format("TAIFEX {} is invalid", field));
  }
}

void ValidatePriceSign(char value, std::string_view field) {
  if (value != '0' && value != '-') {
    throw std::runtime_error(fmt::format("TAIFEX {} is invalid", field));
  }
}

void ValidateYesNo(char value, std::string_view field) {
  if (value != 'Y' && value != 'N') {
    throw std::runtime_error(fmt::format("TAIFEX {} is invalid", field));
  }
}

void ValidateAsciiDateOrBlank(std::string_view value, std::string_view field) {
  if (value == "        ") {
    return;
  }
  if (!std::all_of(value.begin(), value.end(), [](char character) {
        return character >= '0' && character <= '9';
      })) {
    throw std::runtime_error(fmt::format("TAIFEX {} is invalid", field));
  }
  const auto numeric = static_cast<std::int32_t>(std::stol(std::string(value)));
  const auto date =
      std::chrono::year{numeric / 10'000} /
      std::chrono::month{static_cast<unsigned>(numeric / 100 % 100)} /
      std::chrono::day{static_cast<unsigned>(numeric % 100)};
  if (!date.ok()) {
    throw std::runtime_error(fmt::format("TAIFEX {} is invalid", field));
  }
}

std::uint64_t DecodeProductSequence(std::span<const std::uint8_t> bytes) {
  const auto sequence = DecodeBcdInteger(bytes);
  if (sequence > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("TAIFEX product sequence exceeds protocol limit");
  }
  return sequence;
}

const EventMeta &Meta(const SequencedEvent &event) {
  return std::visit(
      [](const auto &value) -> const EventMeta & { return value.meta; }, event);
}

class OrderBookBuilder {
public:
  OrderBookBuilder(std::int32_t trading_day, std::string symbol,
                   std::uint8_t decimal_locator, double multiplier,
                   double reference_price, DecoderStats &stats)
      : decimal_locator_(decimal_locator), multiplier_(multiplier),
        stats_(stats) {
    record_.trading_day = trading_day;
    record_.symbol = std::move(symbol);
    record_.reference_price = reference_price;
  }

  [[nodiscard]] std::uint8_t decimal_locator() const noexcept {
    return decimal_locator_;
  }

  [[nodiscard]] bool waiting_for_snapshot() const noexcept {
    return waiting_for_snapshot_;
  }

  [[nodiscard]] std::uint64_t sequence() const noexcept {
    return record_.sequence;
  }

  void SetReferencePrice(double value) noexcept {
    record_.reference_price = value;
  }

  void Reset() {
    const auto trading_day = record_.trading_day;
    const auto symbol = record_.symbol;
    const auto reference_price = record_.reference_price;
    record_ = DepthRecord{};
    record_.trading_day = trading_day;
    record_.symbol = symbol;
    record_.reference_price = reference_price;
    cached_.clear();
    waiting_for_snapshot_ = false;
    history_complete_ = true;
    has_open_ = false;
    last_trade_sequence_ = 0;
    last_actual_trade_sequence_ = 0;
    last_high_low_sequence_ = 0;
  }

  void Process(SequencedEvent event, const DepthCallback &emit) {
    const auto sequence = Meta(event).sequence;
    if (sequence <= record_.sequence && record_.sequence != 0) {
      ++stats_.stale_messages;
      return;
    }
    if (!waiting_for_snapshot_ && sequence == record_.sequence + 1) {
      Apply(event, true, emit);
      return;
    }
    if (std::holds_alternative<FullBookEvent>(event)) {
      if (!waiting_for_snapshot_) {
        ++stats_.sequence_gaps;
      }
      history_complete_ = false;
      waiting_for_snapshot_ = true;
      const auto full_book = std::get<FullBookEvent>(std::move(event));
      Apply(full_book, false, emit);
      EraseCachedThrough(full_book.meta.sequence);
      ReplayCached(emit);
      ++stats_.snapshot_recoveries;
      return;
    }

    if (!waiting_for_snapshot_) {
      waiting_for_snapshot_ = true;
      history_complete_ = false;
      ++stats_.sequence_gaps;
    }
    const auto inserted =
        cached_.try_emplace(sequence, std::move(event)).second;
    if (!inserted) {
      ++stats_.stale_messages;
    }
    if (cached_.size() > protocol::kMaximumCachedEventsPerSymbol) {
      throw std::runtime_error(fmt::format(
          "TAIFEX gap cache exceeds limit for symbol {}", record_.symbol));
    }
  }

  [[nodiscard]] bool Recover(std::uint64_t last_sequence, std::int64_t exchtime,
                             const std::vector<BookLevel> &levels,
                             const DepthCallback &emit) {
    if (!waiting_for_snapshot_ || last_sequence <= record_.sequence) {
      return false;
    }
    ClearBook();
    for (const auto &level : levels) {
      SetSnapshotLevel(level);
    }
    record_.exchtime = exchtime;
    record_.localtime = exchtime;
    record_.sequence = last_sequence;
    EraseCachedThrough(last_sequence);
    ReplayCached(emit);
    ++stats_.snapshot_recoveries;
    return true;
  }

  void ApplyStatistics(std::uint64_t snapshot_sequence, double last_price,
                       std::int64_t total_volume, std::int64_t total_buy_count,
                       std::int64_t total_sell_count, double first_price,
                       double high, double low) {
    const auto has_later_trade = last_trade_sequence_ > snapshot_sequence;
    const auto has_later_actual_trade =
        last_actual_trade_sequence_ > snapshot_sequence;
    const auto has_later_high_low = last_high_low_sequence_ > snapshot_sequence;
    if (!has_later_trade) {
      record_.last_price = last_price;
      record_.trade_volume = 0;
      last_trade_sequence_ = snapshot_sequence;
    }
    if (!has_later_actual_trade) {
      record_.total_volume = total_volume;
      record_.total_buy_count = total_buy_count;
      record_.total_sell_count = total_sell_count;
      last_actual_trade_sequence_ = snapshot_sequence;
    }
    if (total_volume > 0) {
      record_.open = first_price;
      has_open_ = true;
    } else if (!has_later_actual_trade) {
      record_.open = 0.0;
      has_open_ = false;
    }
    if (has_later_high_low) {
      return;
    }
    if (has_later_actual_trade && total_volume > 0) {
      record_.high = std::max(record_.high, high);
      record_.low = std::min(record_.low, low);
      return;
    }
    record_.high = high;
    record_.low = low;
    last_high_low_sequence_ = snapshot_sequence;
  }

private:
  void ReplayCached(const DepthCallback &emit) {
    while (!cached_.empty()) {
      auto iterator = cached_.begin();
      if (iterator->first != record_.sequence + 1) {
        waiting_for_snapshot_ = true;
        return;
      }
      auto event = std::move(iterator->second);
      cached_.erase(iterator);
      Apply(event, false, emit);
    }
    waiting_for_snapshot_ = false;
  }

  void EraseCachedThrough(std::uint64_t sequence) {
    auto iterator = cached_.begin();
    while (iterator != cached_.end() && iterator->first <= sequence) {
      iterator = cached_.erase(iterator);
    }
  }

  void Apply(const SequencedEvent &event, bool continuous,
             const DepthCallback &emit) {
    std::visit([&](const auto &value) { ApplyValue(value, continuous, emit); },
               event);
  }

  void ApplyValue(const TradeEvent &event, bool, const DepthCallback &) {
    record_.match_flag = event.trial ? 1 : 0;
    std::int64_t packet_volume = 0;
    for (const auto &[price, volume] : event.trades) {
      record_.last_price = price;
      packet_volume += volume;
      if (!event.trial) {
        if (!has_open_) {
          record_.open = price;
          record_.high = price;
          record_.low = price;
          has_open_ = true;
        } else {
          record_.high = std::max(record_.high, price);
          record_.low = std::min(record_.low, price);
        }
        record_.total_value +=
            price * static_cast<double>(volume) * multiplier_;
      }
    }
    record_.trade_volume += packet_volume;
    if (!event.trial) {
      record_.total_volume = event.total_volume;
      record_.total_buy_count = event.total_buy_count;
      record_.total_sell_count = event.total_sell_count;
      last_actual_trade_sequence_ = event.meta.sequence;
    }
    last_trade_sequence_ = event.meta.sequence;
    record_.sequence = event.meta.sequence;
  }

  void ApplyValue(const HighLowEvent &event, bool, const DepthCallback &) {
    record_.high = event.high;
    record_.low = event.low;
    last_high_low_sequence_ = event.meta.sequence;
    record_.sequence = event.meta.sequence;
  }

  void ApplyValue(const IncrementalEvent &event, bool continuous,
                  const DepthCallback &emit) {
    record_.orderbook_action = 0;
    for (const auto &level : event.levels) {
      ApplyIncrementalLevel(level);
    }
    PrepareOutput(event.meta, 0, continuous);
    emit(record_);
    FinishOutput();
  }

  void ApplyValue(const FullBookEvent &event, bool continuous,
                  const DepthCallback &emit) {
    ClearBook();
    for (const auto &level : event.levels) {
      SetSnapshotLevel(level);
    }
    record_.match_flag = event.trial ? 1 : 0;
    record_.orderbook_action = 0;
    PrepareOutput(event.meta, 3, continuous);
    emit(record_);
    FinishOutput();
  }

  void PrepareOutput(const EventMeta &meta, std::uint8_t build_type,
                     bool continuous) {
    record_.exchtime = meta.exchtime;
    record_.localtime = meta.exchtime;
    record_.build_type = build_type;
    record_.continuous_flag = continuous && history_complete_ ? 1 : 0;
    record_.sequence = meta.sequence;
  }

  void FinishOutput() {
    record_.trade_volume = 0;
    record_.orderbook_action = 0;
  }

  static void InsertLevel(std::array<double, 5> &prices,
                          std::array<std::int64_t, 5> &volumes,
                          const IncrementalLevel &level) {
    const auto index = static_cast<std::size_t>(level.level - 1);
    for (std::size_t i = prices.size() - 1; i > index; --i) {
      prices[i] = prices[i - 1];
      volumes[i] = volumes[i - 1];
    }
    prices[index] = level.price;
    volumes[index] = level.volume;
  }

  static void DeleteLevel(std::array<double, 5> &prices,
                          std::array<std::int64_t, 5> &volumes,
                          const IncrementalLevel &level) {
    const auto index = static_cast<std::size_t>(level.level - 1);
    for (std::size_t i = index; i + 1 < prices.size(); ++i) {
      prices[i] = prices[i + 1];
      volumes[i] = volumes[i + 1];
    }
    prices.back() = 0.0;
    volumes.back() = 0;
  }

  static void ChangeLevel(std::array<double, 5> &prices,
                          std::array<std::int64_t, 5> &volumes,
                          const IncrementalLevel &level) {
    const auto index = static_cast<std::size_t>(level.level - 1);
    prices[index] = level.price;
    volumes[index] = level.volume;
  }

  void ApplyIncrementalLevel(const IncrementalLevel &level) {
    if (level.action == '5') {
      if (level.type == 'E') {
        record_.derived_bid_price = level.price;
        record_.derived_bid_volume = level.volume;
      } else {
        record_.derived_ask_price = level.price;
        record_.derived_ask_volume = level.volume;
      }
      return;
    }
    auto &prices = level.type == '0' ? record_.bid_price : record_.ask_price;
    auto &volumes = level.type == '0' ? record_.bid_volume : record_.ask_volume;
    switch (level.action) {
    case '0':
      InsertLevel(prices, volumes, level);
      record_.orderbook_action = 1;
      break;
    case '1':
      ChangeLevel(prices, volumes, level);
      break;
    case '2':
      DeleteLevel(prices, volumes, level);
      record_.orderbook_action = 1;
      break;
    default:
      throw std::runtime_error("TAIFEX order book action is invalid");
    }
  }

  void SetSnapshotLevel(const BookLevel &level) {
    if (level.type == 'E') {
      record_.derived_bid_price = level.price;
      record_.derived_bid_volume = level.volume;
    } else if (level.type == 'F') {
      record_.derived_ask_price = level.price;
      record_.derived_ask_volume = level.volume;
    } else {
      const auto index = static_cast<std::size_t>(level.level - 1);
      auto &prices = level.type == '0' ? record_.bid_price : record_.ask_price;
      auto &volumes =
          level.type == '0' ? record_.bid_volume : record_.ask_volume;
      prices[index] = level.price;
      volumes[index] = level.volume;
    }
  }

  void ClearBook() {
    record_.ask_price.fill(0.0);
    record_.ask_volume.fill(0);
    record_.bid_price.fill(0.0);
    record_.bid_volume.fill(0);
    record_.derived_ask_price = 0.0;
    record_.derived_ask_volume = 0;
    record_.derived_bid_price = 0.0;
    record_.derived_bid_volume = 0;
  }

  std::uint8_t decimal_locator_{};
  double multiplier_{};
  DecoderStats &stats_;
  DepthRecord record_;
  bool waiting_for_snapshot_{};
  bool history_complete_{true};
  bool has_open_{};
  std::uint64_t last_trade_sequence_{};
  std::uint64_t last_actual_trade_sequence_{};
  std::uint64_t last_high_low_sequence_{};
  std::map<std::uint64_t, SequencedEvent> cached_;
};

BookLevel DecodeBookLevel(std::span<const std::uint8_t> bytes,
                          std::uint8_t locator) {
  RequireBodySize(bytes, protocol::kBookLevelSize, "book level");
  const auto type = static_cast<char>(bytes[0]);
  if (type != '0' && type != '1' && type != 'E' && type != 'F') {
    throw std::runtime_error("TAIFEX order book entry type is invalid");
  }
  const auto level = DecodeBcdInteger(bytes.subspan(11, 1));
  if ((type == '0' || type == '1') && (level == 0 || level > 5)) {
    throw std::runtime_error("TAIFEX ordinary order book level is invalid");
  }
  if ((type == 'E' || type == 'F') && level != 1) {
    throw std::runtime_error("TAIFEX derived order book level is invalid");
  }
  return {.type = type,
          .price = DecodeSignedPrice(static_cast<char>(bytes[1]),
                                     bytes.subspan(2, 5), locator),
          .volume =
              static_cast<std::int64_t>(DecodeBcdInteger(bytes.subspan(7, 4))),
          .level = static_cast<std::uint8_t>(level)};
}

} // namespace

struct MessageDecoder::Impl {
  explicit Impl(std::int32_t day)
      : trading_day(day),
        trading_day_start_ns(TradingDayStartNanoseconds(day)) {}

  struct ResolvedMetadata {
    std::uint8_t decimal_locator{};
    double multiplier{};
    double reference_price{};
  };

  [[nodiscard]] std::optional<ResolvedMetadata>
  ResolveMetadata(std::string_view symbol) const {
    const auto kind_iterator = contracts.find(KindId(symbol));
    if (kind_iterator == contracts.end()) {
      return std::nullopt;
    }
    const auto &contract = kind_iterator->second;
    const auto product_iterator = products.find(std::string(symbol));
    if (product_iterator == products.end()) {
      return ResolvedMetadata{.decimal_locator = contract.decimal_locator,
                              .multiplier = contract.multiplier(),
                              .reference_price = 0.0};
    }
    const auto &product = product_iterator->second;
    if (product.decimal_locator != contract.decimal_locator) {
      throw std::runtime_error(
          fmt::format("TAIFEX decimal locator mismatch for symbol {}", symbol));
    }
    return ResolvedMetadata{.decimal_locator = product.decimal_locator,
                            .multiplier = contract.multiplier(),
                            .reference_price = 0.0};
  }

  [[nodiscard]] double ProductReferencePrice(const ProductInfo &product) const {
    constexpr std::array<double, 10> kScales{
        1.0,     0.1,      0.01,      0.001,      0.0001,
        0.00001, 0.000001, 0.0000001, 0.00000001, 0.000000001,
    };
    return static_cast<double>(product.reference_price_raw) *
           kScales.at(product.decimal_locator);
  }

  [[nodiscard]] std::optional<ResolvedMetadata>
  ResolveMetadataWithReference(std::string_view symbol) const {
    auto metadata = ResolveMetadata(symbol);
    if (!metadata.has_value()) {
      return std::nullopt;
    }
    const auto iterator = products.find(std::string(symbol));
    if (iterator != products.end()) {
      metadata->reference_price = ProductReferencePrice(iterator->second);
    }
    return metadata;
  }

  OrderBookBuilder *FindBuilder(std::string_view symbol,
                                bool count_missing = true) {
    observed_symbols.emplace(symbol);
    auto iterator = builders.find(std::string(symbol));
    if (iterator != builders.end()) {
      return &iterator->second;
    }
    const auto metadata = ResolveMetadataWithReference(symbol);
    if (!metadata.has_value()) {
      if (count_missing) {
        ++decoder_stats.metadata_missing_messages;
      }
      return nullptr;
    }
    auto [inserted, created] = builders.try_emplace(
        std::string(symbol), trading_day, std::string(symbol),
        metadata->decimal_locator, metadata->multiplier,
        metadata->reference_price, decoder_stats);
    (void)created;
    return &inserted->second;
  }

  void ProcessProductBasic(const MessageHeader &header,
                           std::span<const std::uint8_t> body) {
    RequireVersion(header, 9, "I010");
    RequireBodySize(body, protocol::kProductBasicBodySize, "I010");
    ProductInfo info{
        .symbol = NormalizeText(body.first(10), "product symbol"),
        .reference_price_raw = DecodeBcdInteger(body.subspan(10, 5)),
        .contract_type = static_cast<char>(body[15]),
        .decimal_locator =
            static_cast<std::uint8_t>(DecodeBcdInteger(body.subspan(16, 1))),
        .strike_decimal_locator =
            static_cast<std::uint8_t>(DecodeBcdInteger(body.subspan(17, 1))),
        .listing_date = DecodeDate(body.subspan(18, 4), "listing date"),
        .delisting_date = DecodeDate(body.subspan(22, 4), "delisting date"),
        .flow_group =
            static_cast<std::uint8_t>(DecodeBcdInteger(body.subspan(26, 1))),
        .delivery_date = DecodeDate(body.subspan(27, 4), "delivery date"),
        .dynamic_banding = static_cast<char>(body[31]),
    };
    ValidateContractType(info.contract_type);
    if (info.decimal_locator > 9 || info.strike_decimal_locator > 9) {
      throw std::runtime_error("TAIFEX I010 decimal locator is out of range");
    }
    if (info.dynamic_banding != 'Y' && info.dynamic_banding != 'N') {
      throw std::runtime_error("TAIFEX I010 dynamic banding flag is invalid");
    }
    auto [iterator, inserted] = products.try_emplace(info.symbol, info);
    if (!inserted) {
      if (iterator->second != info) {
        throw std::runtime_error(fmt::format(
            "conflicting TAIFEX I010 record for symbol {}", info.symbol));
      }
      ++decoder_stats.identical_basic_duplicates;
    }
    ++decoder_stats.product_basic_messages;
    const auto builder = builders.find(info.symbol);
    if (builder != builders.end()) {
      if (builder->second.decimal_locator() != info.decimal_locator) {
        throw std::runtime_error(fmt::format(
            "TAIFEX decimal locator changed for symbol {}", info.symbol));
      }
      builder->second.SetReferencePrice(ProductReferencePrice(info));
    }
  }

  void ProcessContractBasic(const MessageHeader &header,
                            std::span<const std::uint8_t> body) {
    RequireVersion(header, 4, "I011");
    RequireBodySize(body, protocol::kContractBasicBodySize, "I011");
    ContractInfo info{
        .kind_id = NormalizeText(body.first(4), "kind id"),
        .stock_id = NormalizeText(body.subspan(34, 6), "stock_id"),
        .contract_subtype = static_cast<char>(body[40]),
        .contract_size_raw = DecodeBcdInteger(body.subspan(41, 6)),
        .contract_status = static_cast<char>(body[47]),
        .currency_code = static_cast<char>(body[48]),
        .decimal_locator =
            static_cast<std::uint8_t>(DecodeBcdInteger(body.subspan(49, 1))),
        .strike_decimal_locator =
            static_cast<std::uint8_t>(DecodeBcdInteger(body.subspan(50, 1))),
        .quote_flag = static_cast<char>(body[51]),
        .begin_date =
            std::string(reinterpret_cast<const char *>(body.data() + 52), 8),
        .block_trade_flag = static_cast<char>(body[60]),
        .expiry_type = static_cast<char>(body[61]),
        .underlying_type = static_cast<char>(body[62]),
        .close_group =
            static_cast<std::uint8_t>(DecodeBcdInteger(body.subspan(63, 1))),
        .end_session = static_cast<char>(body[64]),
    };
    if (info.kind_id.size() != 3) {
      throw std::runtime_error("TAIFEX I011 kind id must contain three bytes");
    }
    ValidateContractType(info.contract_subtype);
    if (info.contract_size_raw == 0) {
      throw std::runtime_error("TAIFEX I011 contract multiplier is zero");
    }
    (void)Currency(info.currency_code);
    if (info.decimal_locator > 9 || info.strike_decimal_locator > 9) {
      throw std::runtime_error("TAIFEX I011 decimal locator is out of range");
    }
    if (info.contract_status != 'N' && info.contract_status != 'P' &&
        info.contract_status != 'U') {
      throw std::runtime_error("TAIFEX I011 status code is invalid");
    }
    ValidateYesNo(info.quote_flag, "I011 quote flag");
    ValidateAsciiDateOrBlank(info.begin_date, "I011 begin date");
    ValidateYesNo(info.block_trade_flag, "I011 block-trade flag");
    if (info.expiry_type != 'S' && info.expiry_type != 'W') {
      throw std::runtime_error("TAIFEX I011 expiry type is invalid");
    }
    if (info.underlying_type != ' ' && info.underlying_type != 'E' &&
        info.underlying_type != 'S') {
      throw std::runtime_error("TAIFEX I011 underlying type is invalid");
    }
    ValidateBinaryFlag(info.end_session, "I011 end-session flag");
    auto [iterator, inserted] = contracts.try_emplace(info.kind_id, info);
    if (!inserted) {
      if (iterator->second != info) {
        throw std::runtime_error(fmt::format(
            "conflicting TAIFEX I011 record for kind {}", info.kind_id));
      }
      ++decoder_stats.identical_basic_duplicates;
    }
    ++decoder_stats.contract_basic_messages;
  }

  std::optional<std::pair<OrderBookBuilder *, std::uint8_t>>
  ResolveRealtime(std::span<const std::uint8_t> body) {
    if (body.size() < protocol::kSymbolSize) {
      throw std::runtime_error(
          "TAIFEX realtime body does not contain a symbol");
    }
    const auto symbol =
        NormalizeText(body.first(protocol::kSymbolSize), "realtime symbol");
    auto *builder = FindBuilder(symbol);
    if (builder == nullptr) {
      return std::nullopt;
    }
    return std::pair{builder, builder->decimal_locator()};
  }

  void ProcessTrade(const MessageHeader &header,
                    std::span<const std::uint8_t> body,
                    const DepthCallback &emit) {
    RequireVersion(header, 1, "I024");
    if (body.size() <
        protocol::kTradeHeaderSize + protocol::kTradeSummarySize) {
      throw std::runtime_error("TAIFEX I024 body is too short");
    }
    const auto count = static_cast<std::size_t>(body[42] & 0x7FU);
    if (count > protocol::kMaximumTradeEntries) {
      throw std::runtime_error(
          "TAIFEX I024 entry count exceeds protocol limit");
    }
    RequireBodySize(body,
                    protocol::kTradeHeaderSize +
                        count * protocol::kTradeEntrySize +
                        protocol::kTradeSummarySize,
                    "I024");
    const auto resolved = ResolveRealtime(body);
    if (!resolved.has_value()) {
      return;
    }
    auto [builder, locator] = *resolved;
    const auto match_flag = static_cast<char>(body[25]);
    ValidateBinaryFlag(match_flag, "I024 match flag");
    TradeEvent event{
        .meta = {.symbol = NormalizeText(body.first(20), "realtime symbol"),
                 .sequence = DecodeProductSequence(body.subspan(20, 5)),
                 .exchtime = trading_day_start_ns + header.exchange_time_ns},
        .trial = match_flag == '1',
        .trades = {},
    };
    event.trades.reserve(count + 1);
    event.trades.emplace_back(
        DecodeSignedPrice(static_cast<char>(body[32]), body.subspan(33, 5),
                          locator),
        static_cast<std::int64_t>(DecodeBcdInteger(body.subspan(38, 4))));
    std::size_t offset = protocol::kTradeHeaderSize;
    for (std::size_t i = 0; i < count; ++i) {
      event.trades.emplace_back(
          DecodeSignedPrice(static_cast<char>(body[offset]),
                            body.subspan(offset + 1, 5), locator),
          static_cast<std::int64_t>(
              DecodeBcdInteger(body.subspan(offset + 6, 2))));
      offset += protocol::kTradeEntrySize;
    }
    event.total_volume =
        static_cast<std::int64_t>(DecodeBcdInteger(body.subspan(offset, 4)));
    event.total_buy_count = static_cast<std::int64_t>(
        DecodeBcdInteger(body.subspan(offset + 4, 4)));
    event.total_sell_count = static_cast<std::int64_t>(
        DecodeBcdInteger(body.subspan(offset + 8, 4)));
    builder->Process(std::move(event), emit);
  }

  void ProcessHighLow(const MessageHeader &header,
                      std::span<const std::uint8_t> body,
                      const DepthCallback &emit) {
    RequireVersion(header, 1, "I025");
    RequireBodySize(body, protocol::kHighLowBodySize, "I025");
    const auto resolved = ResolveRealtime(body);
    if (!resolved.has_value()) {
      return;
    }
    auto [builder, locator] = *resolved;
    HighLowEvent event{
        .meta = {.symbol = NormalizeText(body.first(20), "realtime symbol"),
                 .sequence = DecodeProductSequence(body.subspan(20, 5)),
                 .exchtime = trading_day_start_ns + header.exchange_time_ns},
        .high = DecodeSignedPrice(static_cast<char>(body[25]),
                                  body.subspan(26, 5), locator),
        .low = DecodeSignedPrice(static_cast<char>(body[31]),
                                 body.subspan(32, 5), locator),
    };
    (void)DecodeBcdTimeNanoseconds(body.subspan(37, 6));
    builder->Process(std::move(event), emit);
  }

  void ProcessIncremental(const MessageHeader &header,
                          std::span<const std::uint8_t> body,
                          const DepthCallback &emit) {
    RequireVersion(header, 1, "I081");
    if (body.size() < protocol::kIncrementalHeaderSize) {
      throw std::runtime_error("TAIFEX I081 body is too short");
    }
    const auto count =
        static_cast<std::size_t>(DecodeBcdInteger(body.subspan(25, 1)));
    if (count == 0 || count > protocol::kMaximumIncrementalEntries) {
      throw std::runtime_error(
          "TAIFEX I081 entry count exceeds protocol limit");
    }
    RequireBodySize(body,
                    protocol::kIncrementalHeaderSize +
                        count * protocol::kIncrementalLevelSize,
                    "I081");
    const auto resolved = ResolveRealtime(body);
    if (!resolved.has_value()) {
      return;
    }
    auto [builder, locator] = *resolved;
    IncrementalEvent event{
        .meta = {.symbol = NormalizeText(body.first(20), "realtime symbol"),
                 .sequence = DecodeProductSequence(body.subspan(20, 5)),
                 .exchtime = trading_day_start_ns + header.exchange_time_ns},
        .levels = {},
    };
    event.levels.reserve(count);
    std::size_t offset = protocol::kIncrementalHeaderSize;
    for (std::size_t i = 0; i < count; ++i) {
      const auto action = static_cast<char>(body[offset]);
      const auto level = DecodeBookLevel(
          body.subspan(offset + 1, protocol::kBookLevelSize), locator);
      if (action == '5') {
        if (level.type != 'E' && level.type != 'F') {
          throw std::runtime_error(
              "TAIFEX I081 overlay requires a derived entry");
        }
      } else if ((action != '0' && action != '1' && action != '2') ||
                 (level.type != '0' && level.type != '1')) {
        throw std::runtime_error("TAIFEX I081 action/type pair is invalid");
      }
      IncrementalLevel incremental;
      static_cast<BookLevel &>(incremental) = level;
      incremental.action = action;
      event.levels.push_back(incremental);
      offset += protocol::kIncrementalLevelSize;
    }
    builder->Process(std::move(event), emit);
  }

  void ProcessFullBook(const MessageHeader &header,
                       std::span<const std::uint8_t> body,
                       const DepthCallback &emit) {
    RequireVersion(header, 1, "I083");
    if (body.size() < protocol::kFullBookHeaderSize) {
      throw std::runtime_error("TAIFEX I083 body is too short");
    }
    const auto count =
        static_cast<std::size_t>(DecodeBcdInteger(body.subspan(26, 1)));
    if (count > protocol::kMaximumFullBookEntries) {
      throw std::runtime_error(
          "TAIFEX I083 entry count exceeds protocol limit");
    }
    RequireBodySize(
        body, protocol::kFullBookHeaderSize + count * protocol::kBookLevelSize,
        "I083");
    const auto resolved = ResolveRealtime(body);
    if (!resolved.has_value()) {
      return;
    }
    auto [builder, locator] = *resolved;
    const auto match_flag = static_cast<char>(body[25]);
    ValidateBinaryFlag(match_flag, "I083 order flag");
    FullBookEvent event{
        .meta = {.symbol = NormalizeText(body.first(20), "realtime symbol"),
                 .sequence = DecodeProductSequence(body.subspan(20, 5)),
                 .exchtime = trading_day_start_ns + header.exchange_time_ns},
        .trial = match_flag == '1',
        .levels = {},
    };
    event.levels.reserve(count);
    std::size_t offset = protocol::kFullBookHeaderSize;
    for (std::size_t i = 0; i < count; ++i) {
      event.levels.push_back(DecodeBookLevel(
          body.subspan(offset, protocol::kBookLevelSize), locator));
      offset += protocol::kBookLevelSize;
    }
    builder->Process(std::move(event), emit);
  }

  void ProcessSnapshot(const MessageHeader &header,
                       std::span<const std::uint8_t> body,
                       const DepthCallback &emit) {
    RequireVersion(header, 3, "I084");
    if (body.empty()) {
      throw std::runtime_error("TAIFEX I084 body is empty");
    }
    const auto type = static_cast<char>(body[0]);
    if (type == 'A' || type == 'Z') {
      RequireBodySize(body, 6, "I084 refresh marker");
      (void)DecodeBcdInteger(body.subspan(1, 5));
      snapshot_statistics_sequences.clear();
      return;
    }
    if (type != 'O' && type != 'S' && type != 'P') {
      throw std::runtime_error("TAIFEX I084 snapshot type is invalid");
    }
    if (body.size() < 2) {
      throw std::runtime_error("TAIFEX I084 body has no entry count");
    }
    const auto count =
        static_cast<std::size_t>(DecodeBcdInteger(body.subspan(1, 1)));
    std::size_t offset = 2;
    if (type == 'P') {
      if (count > protocol::kMaximumSnapshotStatusEntries) {
        throw std::runtime_error(
            "TAIFEX I084 status count exceeds protocol limit");
      }
      RequireBodySize(body, offset + count * protocol::kSnapshotStatusSize,
                      "I084 product status");
      for (std::size_t i = 0; i < count; ++i) {
        const auto entry = body.subspan(offset, protocol::kSnapshotStatusSize);
        (void)NormalizeText(entry.first(20), "snapshot status symbol");
        (void)DecodeBcdInteger(entry.subspan(20, 1));
        (void)DecodeBcdInteger(entry.subspan(21, 1));
        (void)DecodeBcdInteger(entry.subspan(30, 1));
        (void)DecodeBcdInteger(entry.subspan(31, 1));
        offset += protocol::kSnapshotStatusSize;
      }
      return;
    }
    if (type == 'S') {
      if (count > protocol::kMaximumSnapshotStatsEntries) {
        throw std::runtime_error(
            "TAIFEX I084 statistics count exceeds protocol limit");
      }
      RequireBodySize(body, offset + count * protocol::kSnapshotStatsSize,
                      "I084 statistics");
      for (std::size_t i = 0; i < count; ++i) {
        const auto entry = body.subspan(offset, protocol::kSnapshotStatsSize);
        const auto symbol = NormalizeText(entry.first(20), "snapshot symbol");
        (void)DecodeBcdTimeNanoseconds(entry.subspan(20, 6));
        ValidatePriceSign(static_cast<char>(entry[26]), "I084 last-match sign");
        (void)DecodeBcdInteger(entry.subspan(27, 5));
        (void)DecodeBcdInteger(entry.subspan(32, 4));
        (void)DecodeBcdInteger(entry.subspan(36, 4));
        (void)DecodeBcdInteger(entry.subspan(40, 4));
        (void)DecodeBcdInteger(entry.subspan(44, 4));
        (void)DecodeBcdTimeNanoseconds(entry.subspan(48, 6));
        ValidatePriceSign(static_cast<char>(entry[54]),
                          "I084 first-match sign");
        (void)DecodeBcdInteger(entry.subspan(55, 5));
        (void)DecodeBcdInteger(entry.subspan(60, 4));
        ValidatePriceSign(static_cast<char>(entry[64]), "I084 high sign");
        (void)DecodeBcdInteger(entry.subspan(65, 5));
        ValidatePriceSign(static_cast<char>(entry[70]), "I084 low sign");
        (void)DecodeBcdInteger(entry.subspan(71, 5));
        (void)DecodeBcdInteger(entry.subspan(76, 4));
        (void)DecodeBcdInteger(entry.subspan(80, 4));
        (void)DecodeBcdInteger(entry.subspan(84, 4));
        (void)DecodeBcdInteger(entry.subspan(88, 4));
        auto *builder = FindBuilder(symbol, false);
        const auto recovery = snapshot_statistics_sequences.find(symbol);
        if (builder != nullptr &&
            recovery != snapshot_statistics_sequences.end()) {
          const auto locator = builder->decimal_locator();
          builder->ApplyStatistics(
              recovery->second,
              DecodeSignedPrice(static_cast<char>(entry[26]),
                                entry.subspan(27, 5), locator),
              static_cast<std::int64_t>(DecodeBcdInteger(entry.subspan(36, 4))),
              static_cast<std::int64_t>(DecodeBcdInteger(entry.subspan(40, 4))),
              static_cast<std::int64_t>(DecodeBcdInteger(entry.subspan(44, 4))),
              DecodeSignedPrice(static_cast<char>(entry[54]),
                                entry.subspan(55, 5), locator),
              DecodeSignedPrice(static_cast<char>(entry[64]),
                                entry.subspan(65, 5), locator),
              DecodeSignedPrice(static_cast<char>(entry[70]),
                                entry.subspan(71, 5), locator));
        }
        if (recovery != snapshot_statistics_sequences.end()) {
          snapshot_statistics_sequences.erase(recovery);
        }
        offset += protocol::kSnapshotStatsSize;
      }
      return;
    }

    if (count > protocol::kMaximumSnapshotBookHeaders) {
      throw std::runtime_error(
          "TAIFEX I084 order book count exceeds protocol limit");
    }

    for (std::size_t i = 0; i < count; ++i) {
      if (offset + 26 > body.size()) {
        throw std::runtime_error("TAIFEX I084 order book header is truncated");
      }
      const auto symbol =
          NormalizeText(body.subspan(offset, 20), "snapshot symbol");
      const auto last_sequence =
          DecodeProductSequence(body.subspan(offset + 20, 5));
      const auto level_count = static_cast<std::size_t>(
          DecodeBcdInteger(body.subspan(offset + 25, 1)));
      if (level_count > protocol::kMaximumSnapshotBookLevels) {
        throw std::runtime_error(
            "TAIFEX I084 level count exceeds protocol limit");
      }
      offset += 26;
      const auto level_bytes = level_count * protocol::kBookLevelSize;
      if (offset + level_bytes > body.size()) {
        throw std::runtime_error("TAIFEX I084 order book levels are truncated");
      }
      auto *builder = FindBuilder(symbol, false);
      std::vector<BookLevel> levels;
      levels.reserve(level_count);
      for (std::size_t j = 0; j < level_count; ++j) {
        levels.push_back(DecodeBookLevel(
            body.subspan(offset + j * protocol::kBookLevelSize,
                         protocol::kBookLevelSize),
            builder != nullptr ? builder->decimal_locator() : 0));
      }
      if (builder != nullptr) {
        if (builder->Recover(last_sequence,
                             trading_day_start_ns + header.exchange_time_ns,
                             levels, emit)) {
          snapshot_statistics_sequences.insert_or_assign(symbol, last_sequence);
        }
      }
      offset += level_bytes;
    }
    RequireBodySize(body, offset, "I084 order book");
  }

  void Process(const MessageHeader &header, std::span<const std::uint8_t> body,
               const DepthCallback &emit) {
    if (header.body_length != body.size()) {
      throw std::runtime_error("TAIFEX header and body lengths do not match");
    }
    if (header.transmission_code == '0') {
      if (header.message_kind == '1') {
        RequireVersion(header, 1, "I001");
        RequireBodySize(body, 0, "I001");
      } else if (header.message_kind == '2') {
        RequireVersion(header, 1, "I002");
        RequireBodySize(body, 0, "I002");
        for (auto &[symbol, builder] : builders) {
          (void)symbol;
          builder.Reset();
        }
        snapshot_statistics_sequences.clear();
        ++decoder_stats.reset_messages;
      } else {
        ++decoder_stats.ignored_messages;
      }
      return;
    }
    if (header.transmission_code == '1') {
      if (header.message_kind == '1') {
        ProcessProductBasic(header, body);
      } else if (header.message_kind == '3') {
        ProcessContractBasic(header, body);
      } else {
        ++decoder_stats.ignored_messages;
      }
      return;
    }
    if (header.transmission_code != '2') {
      ++decoder_stats.ignored_messages;
      return;
    }
    switch (header.message_kind) {
    case 'A':
      ProcessIncremental(header, body, emit);
      break;
    case 'B':
      ProcessFullBook(header, body, emit);
      break;
    case 'C':
      ProcessSnapshot(header, body, emit);
      break;
    case 'D':
      ProcessTrade(header, body, emit);
      break;
    case 'E':
      ProcessHighLow(header, body, emit);
      break;
    default:
      ++decoder_stats.ignored_messages;
      break;
    }
  }

  void Finalize() const {
    std::size_t unresolved = 0;
    std::string first_symbol;
    for (const auto &[symbol, builder] : builders) {
      if (builder.waiting_for_snapshot()) {
        ++unresolved;
        if (first_symbol.empty()) {
          first_symbol = symbol;
        }
      }
    }
    if (unresolved != 0) {
      throw std::runtime_error(fmt::format(
          "TAIFEX dump ended with {} unresolved product sequence gap(s); "
          "first_symbol={}",
          unresolved, first_symbol));
    }
  }

  std::int32_t trading_day{};
  std::int64_t trading_day_start_ns{};
  DecoderStats decoder_stats;
  std::unordered_map<std::string, ProductInfo> products;
  std::unordered_map<std::string, ContractInfo> contracts;
  std::unordered_map<std::string, OrderBookBuilder> builders;
  std::unordered_set<std::string> observed_symbols;
  std::unordered_map<std::string, std::uint64_t> snapshot_statistics_sequences;
};

MessageHeader DecodeMessageHeader(std::span<const std::uint8_t> bytes) {
  if (bytes.size() != protocol::kHeaderSize) {
    throw std::runtime_error("TAIFEX header must contain 19 bytes");
  }
  if (bytes[0] != protocol::kEscape) {
    throw std::runtime_error("TAIFEX header has invalid escape byte");
  }
  const auto transmission = static_cast<char>(bytes[1]);
  if (transmission < '0' || transmission > '6') {
    throw std::runtime_error("TAIFEX transmission code is invalid");
  }
  const auto version = DecodeBcdInteger(bytes.subspan(16, 1));
  const auto body_length = DecodeBcdInteger(bytes.subspan(17, 2));
  const auto channel_sequence = DecodeBcdInteger(bytes.subspan(11, 5));
  if (channel_sequence > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("TAIFEX channel sequence exceeds protocol limit");
  }
  if (body_length > protocol::kMaximumBodySize) {
    throw std::runtime_error("TAIFEX body length is out of range");
  }
  return MessageHeader{
      .transmission_code = transmission,
      .message_kind = static_cast<char>(bytes[2]),
      .exchange_time_ns = DecodeBcdTimeNanoseconds(bytes.subspan(3, 6)),
      .channel_id = DecodeBcdInteger(bytes.subspan(9, 2)),
      .channel_sequence = channel_sequence,
      .version = static_cast<std::uint8_t>(version),
      .body_length = static_cast<std::size_t>(body_length),
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
    throw std::invalid_argument("invalid TAIFEX trading day");
  }
  const auto utc_time = std::chrono::sys_days{date} - std::chrono::hours{8};
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             utc_time.time_since_epoch())
      .count();
}

MessageDecoder::MessageDecoder(std::int32_t trading_day)
    : impl_(std::make_unique<Impl>(trading_day)) {}

MessageDecoder::~MessageDecoder() = default;
MessageDecoder::MessageDecoder(MessageDecoder &&) noexcept = default;
MessageDecoder &MessageDecoder::operator=(MessageDecoder &&) noexcept = default;

void MessageDecoder::Process(const MessageHeader &header,
                             std::span<const std::uint8_t> body,
                             const DepthCallback &emit) {
  impl_->Process(header, body, emit);
}

void MessageDecoder::Finalize() const { impl_->Finalize(); }

std::vector<BasicInfoRecord> MessageDecoder::BasicInfoRecords() const {
  std::vector<std::string> symbols;
  symbols.reserve(impl_->products.size() + impl_->observed_symbols.size());
  for (const auto &[symbol, product] : impl_->products) {
    (void)product;
    symbols.push_back(symbol);
  }
  for (const auto &symbol : impl_->observed_symbols) {
    if (!impl_->products.contains(symbol)) {
      symbols.push_back(symbol);
    }
  }
  std::sort(symbols.begin(), symbols.end());
  symbols.erase(std::unique(symbols.begin(), symbols.end()), symbols.end());

  std::vector<BasicInfoRecord> output;
  output.reserve(symbols.size());
  for (const auto &symbol : symbols) {
    const auto kind_id = KindId(symbol);
    const auto contract_iterator = impl_->contracts.find(kind_id);
    if (contract_iterator == impl_->contracts.end()) {
      throw std::runtime_error(
          fmt::format("missing TAIFEX I011 metadata for symbol {}", symbol));
    }
    const auto &contract = contract_iterator->second;
    BasicInfoRecord record{
        .trading_day = impl_->trading_day,
        .symbol = symbol,
        .kind_id = kind_id,
        .is_spread = symbol.find('/') != std::string::npos,
        .basic_source = "I011",
        .contract_type = contract.contract_subtype,
        .contract_subtype = contract.contract_subtype,
        .reference_price = std::nullopt,
        .decimal_locator = contract.decimal_locator,
        .strike_decimal_locator = contract.strike_decimal_locator,
        .listing_date = std::nullopt,
        .delisting_date = std::nullopt,
        .delivery_date = std::nullopt,
        .flow_group = std::nullopt,
        .dynamic_banding = std::nullopt,
        .multiplier = contract.multiplier(),
        .currency_code = contract.currency_code,
        .currency = Currency(contract.currency_code),
        .stock_id = contract.stock_id,
        .contract_status = contract.contract_status,
        .quote_flag = contract.quote_flag,
        .block_trade_flag = contract.block_trade_flag,
        .expiry_type = contract.expiry_type,
        .underlying_type = contract.underlying_type,
        .close_group = contract.close_group,
        .end_session = contract.end_session,
    };
    const auto product_iterator = impl_->products.find(symbol);
    if (product_iterator != impl_->products.end()) {
      const auto &product = product_iterator->second;
      if (product.decimal_locator != contract.decimal_locator) {
        throw std::runtime_error(fmt::format(
            "TAIFEX decimal locator mismatch for symbol {}", symbol));
      }
      record.basic_source = "I010+I011";
      record.contract_type = product.contract_type;
      record.reference_price = impl_->ProductReferencePrice(product);
      record.decimal_locator = product.decimal_locator;
      record.strike_decimal_locator = product.strike_decimal_locator;
      record.listing_date = product.listing_date;
      record.delisting_date = product.delisting_date;
      record.delivery_date = product.delivery_date;
      record.flow_group = product.flow_group;
      record.dynamic_banding = product.dynamic_banding;
    }
    output.push_back(std::move(record));
  }
  return output;
}

const DecoderStats &MessageDecoder::stats() const noexcept {
  return impl_->decoder_stats;
}

std::size_t MessageDecoder::symbol_count() const noexcept {
  return impl_->builders.size();
}

} // namespace aries::data::taifex
