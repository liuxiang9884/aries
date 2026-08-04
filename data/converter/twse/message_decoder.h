#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "data/converter/twse/basic_info.h"
#include "data/converter/twse/orderbook.h"
#include "data/converter/twse/protocol.h"

namespace aries::data::twse {

enum class SymbolFilterMode {
  kAll,
  kETF,
  kWarrant,
  kOddLot,
  kStock,
};

enum class MatchGroupIssueReason : std::uint8_t {
  kTimestampChanged,
  kSourceFormatChanged,
  kSequenceGap,
  kEndOfFile,
  kFrameCorruption,
};

struct MarketSymbolIssue {
  Market market;
  std::string symbol;
};

struct SequenceGapIssue {
  Market market;
  MessageType source_format;
  std::uint64_t expected_sequence;
  std::uint64_t actual_sequence;
};

struct IncompleteMatchGroupIssue {
  std::int32_t trading_day;
  Market market;
  std::string symbol;
  MessageType source_format;
  std::uint64_t first_sequence;
  std::uint64_t last_sequence;
  std::uint32_t trade_count;
  std::int64_t trade_volume;
  MatchGroupIssueReason reason;
};

struct ValueImputationIssue {
  Market market;
  std::string symbol;
  std::uint64_t source_sequence;
  std::int64_t volume_difference;
  std::int64_t observed_volume;
  std::int64_t missing_volume;
  double price;
};

struct DecoderStats {
  std::uint64_t source_actual_trade_payloads{};
  std::uint64_t actual_trade_payloads{};
  std::uint64_t published_rows{};
  std::uint64_t match_groups{};
  std::uint64_t multi_trade_groups{};
  std::uint64_t trades_in_multi_groups{};
  std::uint64_t held_ended_groups{};
  std::uint64_t buy_groups{};
  std::uint64_t sell_groups{};
  std::uint64_t unknown_groups{};
  std::uint64_t missing_multiplier_messages{};
  std::uint64_t invalidated_symbol_messages{};
  std::vector<MarketSymbolIssue> missing_multiplier_symbols;
  std::vector<SequenceGapIssue> sequence_gaps;
  std::vector<IncompleteMatchGroupIssue> incomplete_match_groups;
  std::vector<ValueImputationIssue> value_imputations;

  [[nodiscard]] bool has_issues() const noexcept {
    return !missing_multiplier_symbols.empty() || !sequence_gaps.empty() ||
           !incomplete_match_groups.empty() || !value_imputations.empty();
  }
};

[[nodiscard]] SymbolFilterMode ParseSymbolFilterMode(std::string_view mode);

[[nodiscard]] std::string_view ToString(SymbolFilterMode mode);

[[nodiscard]] std::string_view ToString(MatchGroupIssueReason reason);

[[nodiscard]] bool MatchesSymbol(SymbolFilterMode mode,
                                 std::string_view exchange_symbol);

[[nodiscard]] MessageHeader DecodeMessageHeader(
    std::span<const std::uint8_t> bytes);

[[nodiscard]] std::int64_t TradingDayStartNanoseconds(std::int32_t trading_day);

class MessageDecoder {
 public:
  MessageDecoder(std::int32_t trading_day, SymbolFilterMode mode);

  [[nodiscard]] const Orderbook<5> *Process(const MessageHeader &header,
                                            std::span<const std::uint8_t> body,
                                            std::int64_t local_ns);

  void ApplyBasicInfo(const BasicInfoRecord &basic_info);

  void InvalidateSymbol(ServiceType service_type, std::string symbol);

  void Finalize();

  [[nodiscard]] std::size_t symbol_count() const noexcept;

  [[nodiscard]] const DecoderStats &stats() const noexcept {
    return stats_;
  }

 private:
  struct BookState {
    std::array<double, 5> ask_price{};
    std::array<std::int64_t, 5> ask_volume{};
    std::array<double, 5> bid_price{};
    std::array<std::int64_t, 5> bid_volume{};
    bool valid{};
  };

  struct PendingGroup {
    bool active{};
    bool has_gap{};
    bool side_eligible{};
    MessageType source_format{MessageType::kUnknown};
    std::int64_t exchange_ns{};
    std::uint64_t first_sequence{};
    std::uint64_t last_sequence{};
    std::uint32_t trade_count{};
    std::int64_t trade_volume{};
    double first_price{};
    BookState pre_book;
  };

  struct StockState {
    std::string symbol;
    std::int32_t symbol_id{};
    Market market{Market::kTwse};
    std::uint64_t multiplier{};
    bool multiplier_known{};
    bool invalidated{};

    double last_price{};
    double open{};
    double high{};
    double low{};
    std::int64_t accepted_total_volume{};
    double total_value{};
    std::int64_t interval_observed_volume{};
    double interval_observed_value{};

    BookState book;
    PendingGroup pending;
    Orderbook<5> output;
  };

  struct SequenceState {
    bool initialized{};
    std::uint64_t last_sequence{};
  };

  [[nodiscard]] StockState *FindOrCreate(
      ServiceType service_type, std::span<const std::uint8_t> exchange_symbol);

  void ProcessBasicInfo(const MessageHeader &header,
                        std::span<const std::uint8_t> body);

  void ProcessOddLotBasicInfo(const MessageHeader &header,
                              std::span<const std::uint8_t> body);

  void AuditSequence(const MessageHeader &header);

  void RecordIncomplete(StockState &state, MatchGroupIssueReason reason);

  [[nodiscard]] const Orderbook<5> *ProcessDepth(
      const MessageHeader &header, std::span<const std::uint8_t> body,
      std::int64_t local_ns, bool odd_lot, bool emit);

  std::int64_t trading_day_start_ns_;
  std::int32_t trading_day_;
  SymbolFilterMode mode_;
  std::array<std::unordered_map<std::string, StockState>, 2> states_;
  std::array<std::unordered_set<std::string>, 2> warrant_symbols_;
  std::array<std::unordered_set<std::string>, 2> invalidated_symbols_;
  std::array<std::unordered_set<std::string>, 2> missing_multiplier_symbols_;
  std::array<std::array<SequenceState, 24>, 2> sequences_{};
  std::int32_t next_symbol_id_{};
  bool finalized_{};
  DecoderStats stats_;
};

}  // namespace aries::data::twse
