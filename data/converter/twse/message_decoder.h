#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "data/converter/twse/basic_info.h"
#include "data/converter/twse/depth_record.h"
#include "data/converter/twse/protocol.h"

namespace aries::data::twse {

enum class SymbolFilterMode {
  kAll,
  kETF,
  kWarrant,
  kOddLot,
  kStock,
};

[[nodiscard]] SymbolFilterMode ParseSymbolFilterMode(std::string_view mode);

[[nodiscard]] std::string_view ToString(SymbolFilterMode mode);

[[nodiscard]] bool MatchesSymbol(SymbolFilterMode mode,
                                 std::string_view exchange_symbol);

[[nodiscard]] MessageHeader
DecodeMessageHeader(std::span<const std::uint8_t> bytes);

[[nodiscard]] std::int64_t TradingDayStartNanoseconds(std::int32_t trading_day);

class MessageDecoder {
public:
  MessageDecoder(std::int32_t trading_day, SymbolFilterMode mode);

  [[nodiscard]] const DepthRecord *Process(const MessageHeader &header,
                                           std::span<const std::uint8_t> body);

  void ApplyBasicInfo(const BasicInfoRecord &basic_info);

  [[nodiscard]] std::size_t symbol_count() const noexcept {
    return records_.size();
  }

private:
  [[nodiscard]] DepthRecord *
  FindOrCreate(std::span<const std::uint8_t> exchange_symbol);

  void ProcessBasicInfo(const MessageHeader &header,
                        std::span<const std::uint8_t> body);

  void ProcessOddLotBasicInfo(std::span<const std::uint8_t> body);

  [[nodiscard]] const DepthRecord *
  ProcessDepth(const MessageHeader &header, std::span<const std::uint8_t> body,
               bool odd_lot, bool emit);

  std::int64_t trading_day_start_ns_;
  std::int32_t trading_day_;
  SymbolFilterMode mode_;
  std::unordered_map<std::string, DepthRecord> records_;
  std::unordered_set<std::string> warrant_symbols_;
};

} // namespace aries::data::twse
