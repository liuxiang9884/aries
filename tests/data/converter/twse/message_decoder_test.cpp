#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

#include <gtest/gtest.h>

#include "data/converter/twse/message_decoder.h"
#include "tests/data/converter/twse/test_message_builder.h"

namespace aries::data::twse {
namespace {

using test::Level;

TEST(MessageDecoderTest, AppliesBasicInfoAndDecodesStockDepthState) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  const auto basic = test::MakeStockBasic("2330", 900000, 990000, 810000);

  EXPECT_EQ(
      decoder.Process(
          test::MakeHeader(MessageType::kStockBasicInfo, basic.size()), basic),
      nullptr);

  constexpr std::array<Level, 3> kLevels{{
      {.price = 950000, .volume = 10},
      {.price = 949000, .volume = 20},
      {.price = 951000, .volume = 30},
  }};
  const auto depth =
      test::MakeDepth("2330", 90000123456, 100, true, 1, 1, kLevels, 0x08);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, depth.size(), 123), depth);

  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->symbol, "2330");
  EXPECT_EQ(record->exchtime, 1'783'386'000'123'456'000LL);
  EXPECT_EQ(record->localtime, record->exchtime);
  EXPECT_DOUBLE_EQ(record->previous_close, 90.0);
  EXPECT_DOUBLE_EQ(record->high_limit, 99.0);
  EXPECT_DOUBLE_EQ(record->low_limit, 81.0);
  EXPECT_DOUBLE_EQ(record->last_price, 95.0);
  EXPECT_DOUBLE_EQ(record->open, 95.0);
  EXPECT_EQ(record->total_trade, 10);
  EXPECT_EQ(record->total_volume, 100);
  EXPECT_DOUBLE_EQ(record->total_value, 9500.0);
  EXPECT_DOUBLE_EQ(record->bid_price[0], 94.9);
  EXPECT_EQ(record->bid_volume[0], 20);
  EXPECT_DOUBLE_EQ(record->ask_price[0], 95.1);
  EXPECT_EQ(record->ask_volume[0], 30);
  EXPECT_EQ(record->status, 524434);
  EXPECT_EQ(record->sequence, 123);
  EXPECT_EQ(decoder.symbol_count(), 1);
}

TEST(MessageDecoderTest, AppliesTpexBasicInfoOffsets) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  const auto basic =
      test::MakeStockBasic("6488", 500000, 550000, 450000, ServiceType::kOtc);

  EXPECT_EQ(
      decoder.Process(test::MakeHeader(MessageType::kStockBasicInfo,
                                       basic.size(), 1, ServiceType::kOtc),
                      basic),
      nullptr);

  constexpr std::array<Level, 1> kLevels{{
      {.price = 510000, .volume = 3},
  }};
  const auto depth =
      test::MakeDepth("6488", 90000000000, 3, true, 0, 0, kLevels);
  const auto *record =
      decoder.Process(test::MakeHeader(MessageType::kStockDepthV, depth.size(),
                                       2, ServiceType::kOtc),
                      depth);

  ASSERT_NE(record, nullptr);
  EXPECT_DOUBLE_EQ(record->previous_close, 50.0);
  EXPECT_DOUBLE_EQ(record->high_limit, 55.0);
  EXPECT_DOUBLE_EQ(record->low_limit, 45.0);
  EXPECT_DOUBLE_EQ(record->last_price, 51.0);
}

TEST(MessageDecoderTest, DecodesAndValidatesMessageHeader) {
  constexpr std::array<std::uint8_t, protocol::kHeaderSize> kHeader{
      0x1B, 0x00, 0x32, 0x01, 0x06, 0x04, 0x00, 0x00, 0x01, 0x23};
  const auto header = DecodeMessageHeader(kHeader);

  EXPECT_EQ(header.message_length, 32);
  EXPECT_EQ(header.service_type, ServiceType::kListed);
  EXPECT_EQ(header.message_type, MessageType::kStockDepthV);
  EXPECT_EQ(header.format_version, 4);
  EXPECT_EQ(header.sequence, 123);

  auto invalid_header = kHeader;
  invalid_header[0] = 0;
  EXPECT_THROW((void)DecodeMessageHeader(invalid_header), std::runtime_error);

  invalid_header = kHeader;
  invalid_header[1] = 0x00;
  invalid_header[2] = 0x09;
  EXPECT_THROW((void)DecodeMessageHeader(invalid_header), std::runtime_error);

  invalid_header = kHeader;
  invalid_header[2] = 0xFA;
  EXPECT_THROW((void)DecodeMessageHeader(invalid_header),
               std::invalid_argument);

  invalid_header = kHeader;
  invalid_header[3] = 0x03;
  EXPECT_THROW((void)DecodeMessageHeader(invalid_header), std::runtime_error);
}

TEST(MessageDecoderTest, SupportsOddLotAndWarrantOutputModes) {
  MessageDecoder odd_lot_decoder(20260707, SymbolFilterMode::kOddLot);
  const auto odd_basic = test::MakeOddLotBasic("2330", 900000, 990000, 810000);
  EXPECT_EQ(odd_lot_decoder.Process(
                test::MakeHeader(MessageType::kStockOddLotBasicInfo,
                                 odd_basic.size()),
                odd_basic),
            nullptr);

  constexpr std::array<Level, 1> kOddLevels{{
      {.price = 951000, .volume = 1'234'567'890},
  }};
  const auto odd_depth = test::MakeOddLotDepth(
      "2330", 90000000000, 1'234'567'890, false, 0, 1, kOddLevels);
  const auto *odd_record = odd_lot_decoder.Process(
      test::MakeHeader(MessageType::kStockOddLotDepthV, odd_depth.size(), 9),
      odd_depth);
  ASSERT_NE(odd_record, nullptr);
  EXPECT_EQ(odd_record->total_volume, 1'234'567'890);
  EXPECT_EQ(odd_record->ask_volume[0], 1'234'567'890);

  MessageDecoder warrant_decoder(20260707, SymbolFilterMode::kWarrant);
  constexpr std::array<Level, 1> kWarrantLevels{{
      {.price = 12300, .volume = 7},
  }};
  const auto warrant_depth =
      test::MakeDepth("12345P", 90000000000, 7, true, 0, 0, kWarrantLevels);
  EXPECT_EQ(warrant_decoder.Process(test::MakeHeader(MessageType::kStockDepthV,
                                                     warrant_depth.size(), 10),
                                    warrant_depth),
            nullptr);
  const auto *warrant_record = warrant_decoder.Process(
      test::MakeHeader(MessageType::kWarrantDepthV, warrant_depth.size(), 11),
      warrant_depth);
  ASSERT_NE(warrant_record, nullptr);
  EXPECT_EQ(warrant_record->symbol, "12345P");
  EXPECT_EQ(warrant_record->sequence, 11);
}

TEST(MessageDecoderTest, UsesFormat1MetadataForNonstandardWarrantSymbol) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kWarrant);
  const test::BasicInfoFields fields{
      .symbol = "1234P",
      .security_type = "W2",
      .warrant_flag = 'Y',
      .strike_price = 10000,
      .exercise_ratio = 100,
      .maturity_date = 20261231,
      .market_data_line = 2,
  };
  const auto basic = test::MakeBasicInfo(fields);
  EXPECT_EQ(
      decoder.Process(
          test::MakeHeader(MessageType::kStockBasicInfo, basic.size()), basic),
      nullptr);

  constexpr std::array<Level, 1> kLevels{{
      {.price = 12300, .volume = 7},
  }};
  const auto depth =
      test::MakeDepth("1234P", 90000000000, 7, true, 0, 0, kLevels);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kWarrantDepthV, depth.size(), 12), depth);

  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->symbol, "1234P");
  EXPECT_EQ(record->sequence, 12);
}

TEST(MessageDecoderTest, PreservesOrionSymbolFilterSemantics) {
  EXPECT_TRUE(MatchesSymbol(SymbolFilterMode::kStock, "2330  "));
  EXPECT_TRUE(MatchesSymbol(SymbolFilterMode::kStock, "0050  "));
  EXPECT_FALSE(MatchesSymbol(SymbolFilterMode::kStock, "00878 "));
  EXPECT_TRUE(MatchesSymbol(SymbolFilterMode::kETF, "0050  "));
  EXPECT_TRUE(MatchesSymbol(SymbolFilterMode::kETF, "00878 "));
  EXPECT_TRUE(MatchesSymbol(SymbolFilterMode::kETF, "00919B"));
  EXPECT_FALSE(MatchesSymbol(SymbolFilterMode::kETF, "2330  "));
  EXPECT_TRUE(MatchesSymbol(SymbolFilterMode::kWarrant, "12345P"));
  EXPECT_FALSE(MatchesSymbol(SymbolFilterMode::kWarrant, "1234P "));
  EXPECT_TRUE(MatchesSymbol(SymbolFilterMode::kAll, "ABCDEF"));
  EXPECT_FALSE(MatchesSymbol(SymbolFilterMode::kWarrant, "000000"));
  EXPECT_FALSE(MatchesSymbol(SymbolFilterMode::kAll, "000000"));
}

TEST(MessageDecoderTest, RejectsOversizedLevelCountAndShortBody) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kAll);
  constexpr std::array<Level, 6> kLevels{{
      {.price = 1, .volume = 1},
      {.price = 2, .volume = 1},
      {.price = 3, .volume = 1},
      {.price = 4, .volume = 1},
      {.price = 5, .volume = 1},
      {.price = 6, .volume = 1},
  }};
  const auto oversized =
      test::MakeDepth("2330", 90000000000, 0, false, 6, 0, kLevels);
  EXPECT_THROW((void)decoder.Process(test::MakeHeader(MessageType::kStockDepthV,
                                                      oversized.size()),
                                     oversized),
               std::runtime_error);

  constexpr std::array<std::uint8_t, 3> kShortBody{};
  EXPECT_THROW((void)decoder.Process(test::MakeHeader(MessageType::kStockDepthV,
                                                      kShortBody.size()),
                                     kShortBody),
               std::runtime_error);
}

TEST(MessageDecoderTest, RejectsUnsupportedFormatVersion) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  constexpr std::array<Level, 0> kNoLevels{};
  const auto depth =
      test::MakeDepth("2330", 90000000000, 0, false, 0, 0, kNoLevels);
  auto header = test::MakeHeader(MessageType::kStockDepthV, depth.size(), 1);
  header.format_version = 5;

  EXPECT_THROW((void)decoder.Process(header, depth), std::runtime_error);
}

TEST(MessageDecoderTest, RejectsInvalidMessageTrailer) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  auto basic = test::MakeStockBasic("2330", 900000, 990000, 810000);
  basic.back() = 0;

  EXPECT_THROW(
      (void)decoder.Process(
          test::MakeHeader(MessageType::kStockBasicInfo, basic.size()), basic),
      std::runtime_error);
}

TEST(MessageDecoderTest, RejectsInvalidTradingDay) {
  EXPECT_THROW(MessageDecoder(20260230, SymbolFilterMode::kStock),
               std::invalid_argument);
  EXPECT_THROW((void)ParseSymbolFilterMode("future"), std::invalid_argument);
}

} // namespace
} // namespace aries::data::twse
