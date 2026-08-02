#pragma once

#include <cstddef>
#include <cstdint>

namespace aries::data::taifex {

namespace protocol {

inline constexpr std::uint8_t kEscape = 0x1B;
inline constexpr std::size_t kHeaderSize = 19;
inline constexpr std::size_t kTrailerSize = 3;
inline constexpr std::size_t kMaximumBodySize = 9'999;
inline constexpr std::size_t kSymbolSize = 20;
inline constexpr std::size_t kProductBasicBodySize = 40;
inline constexpr std::size_t kContractBasicBodySize = 65;
inline constexpr std::size_t kPriceLimitSymbolSize = 10;
inline constexpr std::size_t kPriceLimitEntrySize = 6;
inline constexpr std::size_t kMaximumPriceLimitLevels = 99;
inline constexpr std::size_t kTradeHeaderSize = 43;
inline constexpr std::size_t kTradeEntrySize = 8;
inline constexpr std::size_t kTradeSummarySize = 12;
inline constexpr std::size_t kHighLowBodySize = 43;
inline constexpr std::size_t kIncrementalHeaderSize = 26;
inline constexpr std::size_t kIncrementalLevelSize = 13;
inline constexpr std::size_t kFullBookHeaderSize = 27;
inline constexpr std::size_t kBookLevelSize = 12;
inline constexpr std::size_t kSnapshotStatsSize = 92;
inline constexpr std::size_t kSnapshotStatusSize = 32;
inline constexpr std::size_t kMaximumIncrementalEntries = 64;
inline constexpr std::size_t kMaximumFullBookEntries = 16;
inline constexpr std::size_t kMaximumTradeEntries = 70;
inline constexpr std::size_t kMaximumSnapshotBookHeaders = 32;
inline constexpr std::size_t kMaximumSnapshotBookLevels = 64;
inline constexpr std::size_t kMaximumSnapshotStatsEntries = 8;
inline constexpr std::size_t kMaximumSnapshotStatusEntries = 32;
inline constexpr std::size_t kMaximumCachedEventsPerSymbol = 8'192;
inline constexpr std::int64_t kDaySessionEndExclusiveNanoseconds =
    49'560'000'000'000LL;

} // namespace protocol

struct MessageHeader {
  char transmission_code{};
  char message_kind{};
  std::int64_t exchange_time_ns{};
  std::uint64_t channel_id{};
  std::uint64_t channel_sequence{};
  std::uint8_t version{};
  std::size_t body_length{};
};

} // namespace aries::data::taifex
