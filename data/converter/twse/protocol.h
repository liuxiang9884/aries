#pragma once

#include <cstddef>
#include <cstdint>

namespace aries::data::twse {

enum class MessageType : std::uint8_t {
  kUnknown = 0,
  kStockBasicInfo = 1,
  kStockTradeStats = 2,
  kStockIndexStats = 3,
  kStockOrderStats = 4,
  kMarketAnnouncement = 5,
  kStockDepthV = 6,
  kStockFixedPriceTradeStats = 7,
  kStockFixedPriceOrderStats = 8,
  kStockFixedPriceTrade = 9,
  kUpdatedStockIndexInfo = 10,
  kStockOpenCloseInfo = 12,
  kStockOddLotTrade = 13,
  kStockWarrantInfo = 14,
  kStockSuspensionInfo = 15,
  kHeartBeat = 16,
  kWarrantDepthV = 17,
  kWarrantCloseInfo = 18,
  kStockSuspendedOrResumed = 19,
  kStockSnapshot = 20,
  kStockIndexInfo = 21,
  kStockOddLotBasicInfo = 22,
  kStockOddLotDepthV = 23,
};

enum class ServiceType : std::uint8_t {
  kListed = 1,
  kOtc = 2,
};

struct MessageHeader {
  std::size_t message_length{};
  ServiceType service_type{ServiceType::kListed};
  MessageType message_type{MessageType::kUnknown};
  std::uint8_t format_version{};
  std::uint64_t sequence{};
};

namespace protocol {

inline constexpr std::uint8_t kEscape = 0x1B;
inline constexpr std::size_t kHeaderSize = 10;
inline constexpr std::size_t kMaximumMessageSize = 9'999;
inline constexpr std::size_t kSymbolSize = 6;
inline constexpr std::size_t kMessageTrailerSize = 3;

inline constexpr std::size_t kStockBasicBodySize = 104;
inline constexpr std::size_t kListedPreviousCloseOffset = 30;
inline constexpr std::size_t kListedHighLimitOffset = 35;
inline constexpr std::size_t kListedLowLimitOffset = 40;
inline constexpr std::size_t kOtcPreviousCloseOffset = 31;
inline constexpr std::size_t kOtcHighLimitOffset = 36;
inline constexpr std::size_t kOtcLowLimitOffset = 41;

inline constexpr std::size_t kOddLotBasicBodySize = 50;
inline constexpr std::size_t kOddLotPreviousCloseOffset = 25;
inline constexpr std::size_t kOddLotHighLimitOffset = 30;
inline constexpr std::size_t kOddLotLowLimitOffset = 35;

inline constexpr std::size_t kDepthTimeOffset = 6;
inline constexpr std::size_t kDepthDataFlagOffset = 12;
inline constexpr std::size_t kDepthLimitFlagOffset = 13;
inline constexpr std::size_t kDepthStatusOffset = 14;
inline constexpr std::size_t kStockTotalVolumeOffset = 15;
inline constexpr std::size_t kOddLotTotalVolumeOffset = 15;
inline constexpr std::size_t kStockDepthInfoSize = 19;
inline constexpr std::size_t kOddLotDepthInfoSize = 21;
inline constexpr std::size_t kStockLevelSize = 9;
inline constexpr std::size_t kOddLotLevelSize = 11;
inline constexpr std::size_t kMaximumDepthLevels = 5;

} // namespace protocol

} // namespace aries::data::twse
