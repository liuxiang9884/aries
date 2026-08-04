#include "data/converter/twse/message_decoder.h"

#include <array>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include <gtest/gtest.h>

#include "tests/data/converter/twse/test_message_builder.h"

namespace aries::data::twse {
namespace {

using test::Level;

std::string_view Symbol(const Orderbook<5> &record) {
  return record.symbol;
}

void ApplyStockBasic(MessageDecoder &decoder, std::string_view symbol,
                     ServiceType service = ServiceType::kListed,
                     std::uint64_t sequence = 1) {
  const auto basic =
      test::MakeStockBasic(symbol, 900000, 990000, 810000, service);
  EXPECT_EQ(decoder.Process(test::MakeHeader(MessageType::kStockBasicInfo,
                                             basic.size(), sequence, service),
                            basic, 0),
            nullptr);
}

TEST(OrderbookTest, IsPodLikeAndTemplateControlsLevelCount) {
  static_assert(std::is_trivial_v<Orderbook<5>>);
  static_assert(std::is_standard_layout_v<Orderbook<5>>);
  static_assert(std::is_trivially_copyable_v<Orderbook<5>>);
  static_assert(std::extent_v<decltype(Orderbook<3>::ask_price)> == 3);
  static_assert(std::extent_v<decltype(Orderbook<3>::ask_volume)> == 3);
  static_assert(std::extent_v<decltype(Orderbook<3>::bid_price)> == 3);
  static_assert(std::extent_v<decltype(Orderbook<3>::bid_volume)> == 3);

  EXPECT_EQ(sizeof(Orderbook<5>), 272);
  EXPECT_EQ(alignof(Orderbook<5>), 8);
}

TEST(OrderbookTest, StateAccessorsDecodeRawProtocolBytes) {
  const DisclosureState disclosure{0xD5U};
  EXPECT_TRUE(disclosure.has_trade());
  EXPECT_EQ(disclosure.bid_level_count(), 5);
  EXPECT_EQ(disclosure.ask_level_count(), 2);
  EXPECT_TRUE(disclosure.disclosure_tag());

  const LimitState limit{0x6DU};
  EXPECT_EQ(limit.trade_limit(), PriceLimitState::kDownLimit);
  EXPECT_EQ(limit.best_bid_limit(), PriceLimitState::kUpLimit);
  EXPECT_EQ(limit.best_ask_limit(), PriceLimitState::kReserved);
  EXPECT_EQ(limit.instantaneous_trend(), InstantaneousTrend::kHeldDown);

  const SessionState session{0xFEU};
  EXPECT_TRUE(session.is_trial());
  EXPECT_TRUE(session.is_delayed_open());
  EXPECT_TRUE(session.is_delayed_close());
  EXPECT_EQ(session.matching_method(), MatchingMethod::kContinuous);
  EXPECT_TRUE(session.is_opening());
  EXPECT_TRUE(session.is_closing());
  EXPECT_EQ(session.reserved(), 2);
}

TEST(MessageDecoderTest, AppliesBasicInfoAndDecodesAtomicStockEvent) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  ApplyStockBasic(decoder, "2330");

  constexpr std::array<Level, 3> kLevels{{
      {.price = 950000, .volume = 10},
      {.price = 949000, .volume = 20},
      {.price = 951000, .volume = 30},
  }};
  const auto depth = test::MakeDepth("2330", 90000123456, 100, true, 1, 1,
                                     kLevels, 0x10U, 0x6CU);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, depth.size(), 123), depth,
      1'783'386'000'123'999'000LL);

  ASSERT_NE(record, nullptr);
  EXPECT_EQ(Symbol(*record), "2330");
  EXPECT_EQ(record->exchange_ns, 1'783'386'000'123'456'000LL);
  EXPECT_EQ(record->local_ns, 1'783'386'000'123'999'000LL);
  EXPECT_EQ(record->symbol_id, 0);
  EXPECT_EQ(record->market, Market::kTwse);
  EXPECT_EQ(record->disclosure.value, 0x92U);
  EXPECT_EQ(record->limit_state.value, 0x6CU);
  EXPECT_EQ(record->session_state.value, 0x10U);
  EXPECT_EQ(record->trade_side, TradeSide::kUnknown);
  EXPECT_EQ(record->trade_count, 1);
  EXPECT_DOUBLE_EQ(record->last_price, 95.0);
  EXPECT_DOUBLE_EQ(record->open, 95.0);
  EXPECT_DOUBLE_EQ(record->high, 95.0);
  EXPECT_DOUBLE_EQ(record->low, 95.0);
  EXPECT_EQ(record->trade_volume, 10);
  EXPECT_EQ(record->total_volume, 100);
  EXPECT_DOUBLE_EQ(record->total_value, 9'500'000.0);
  EXPECT_DOUBLE_EQ(record->bid_price[0], 94.9);
  EXPECT_EQ(record->bid_volume[0], 20);
  EXPECT_DOUBLE_EQ(record->ask_price[0], 95.1);
  EXPECT_EQ(record->ask_volume[0], 30);
  EXPECT_EQ(record->source_sequence, 123);
  EXPECT_EQ(decoder.symbol_count(), 1);
  EXPECT_EQ(decoder.stats().match_groups, 1);
  EXPECT_EQ(decoder.stats().value_imputations.size(), 1);
  EXPECT_EQ(decoder.stats().value_imputations.front().missing_volume, 90);
}

TEST(MessageDecoderTest, BuffersMultiTradeGroupAcrossInterleavedSymbols) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  ApplyStockBasic(decoder, "2330", ServiceType::kListed, 1);
  ApplyStockBasic(decoder, "2317", ServiceType::kListed, 2);

  constexpr std::array<Level, 2> kPreBook{{
      {.price = 949000, .volume = 20},
      {.price = 950000, .volume = 30},
  }};
  const auto pre_book =
      test::MakeDepth("2330", 90000000000, 0, false, 1, 1, kPreBook, 0x10U);
  ASSERT_NE(decoder.Process(test::MakeHeader(MessageType::kStockDepthV,
                                             pre_book.size(), 10),
                            pre_book, 1000),
            nullptr);

  constexpr std::array<Level, 1> kFirstTrade{{
      {.price = 951000, .volume = 2},
  }};
  const auto first_trade = test::MakeDepth("2330", 90000123456, 2, true, 0, 0,
                                           kFirstTrade, 0x10U, 0, true);
  EXPECT_EQ(decoder.Process(test::MakeHeader(MessageType::kStockDepthV,
                                             first_trade.size(), 11),
                            first_trade, 1100),
            nullptr);

  constexpr std::array<Level, 1> kTrialTrade{{
      {.price = 999000, .volume = 99},
  }};
  const auto trial =
      test::MakeDepth("2330", 90000999999, 99, true, 0, 0, kTrialTrade, 0x90U);
  EXPECT_EQ(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, trial.size(), 12),
                trial, 1150),
            nullptr);

  constexpr std::array<Level, 0> kNoLevels{};
  const auto other =
      test::MakeDepth("2317", 90000120000, 0, false, 0, 0, kNoLevels, 0x10U);
  const auto *other_record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, other.size(), 13), other,
      1200);
  ASSERT_NE(other_record, nullptr);
  EXPECT_EQ(Symbol(*other_record), "2317");

  constexpr std::array<Level, 3> kFinal{{
      {.price = 952000, .volume = 3},
      {.price = 948000, .volume = 25},
      {.price = 953000, .volume = 35},
  }};
  const auto final =
      test::MakeDepth("2330", 90000123456, 5, true, 1, 1, kFinal, 0x10U);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, final.size(), 14), final,
      1300);

  ASSERT_NE(record, nullptr);
  EXPECT_EQ(Symbol(*record), "2330");
  EXPECT_EQ(record->trade_side, TradeSide::kBuy);
  EXPECT_EQ(record->trade_count, 2);
  EXPECT_EQ(record->trade_volume, 5);
  EXPECT_DOUBLE_EQ(record->last_price, 95.2);
  EXPECT_DOUBLE_EQ(record->open, 95.1);
  EXPECT_DOUBLE_EQ(record->high, 95.2);
  EXPECT_DOUBLE_EQ(record->low, 95.1);
  EXPECT_EQ(record->total_volume, 5);
  EXPECT_DOUBLE_EQ(record->total_value, 475'800.0);
  EXPECT_DOUBLE_EQ(record->bid_price[0], 94.8);
  EXPECT_EQ(record->bid_volume[0], 25);
  EXPECT_DOUBLE_EQ(record->ask_price[0], 95.3);
  EXPECT_EQ(record->ask_volume[0], 35);
  EXPECT_EQ(record->local_ns, 1300);
  EXPECT_EQ(record->source_sequence, 14);
  EXPECT_EQ(decoder.stats().actual_trade_payloads, 2);
  EXPECT_EQ(decoder.stats().match_groups, 1);
  EXPECT_EQ(decoder.stats().multi_trade_groups, 1);
  EXPECT_EQ(decoder.stats().trades_in_multi_groups, 2);
  EXPECT_EQ(decoder.stats().buy_groups, 1);
  EXPECT_TRUE(decoder.stats().value_imputations.empty());
}

TEST(MessageDecoderTest, InfersSellAndKeepsMarketLevelAmbiguous) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  ApplyStockBasic(decoder, "2330", ServiceType::kListed, 1);
  ApplyStockBasic(decoder, "2317", ServiceType::kListed, 2);

  constexpr std::array<Level, 2> kNormalBook{{
      {.price = 949000, .volume = 20},
      {.price = 951000, .volume = 30},
  }};
  const auto normal_book =
      test::MakeDepth("2330", 90000000000, 0, false, 1, 1, kNormalBook, 0x10U);
  ASSERT_NE(decoder.Process(test::MakeHeader(MessageType::kStockDepthV,
                                             normal_book.size(), 1),
                            normal_book, 100),
            nullptr);

  constexpr std::array<Level, 1> kSellTrade{{
      {.price = 948000, .volume = 1},
  }};
  const auto sell_trade =
      test::MakeDepth("2330", 90000100000, 1, true, 0, 0, kSellTrade, 0x10U);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, sell_trade.size(), 2),
      sell_trade, 200);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->trade_side, TradeSide::kSell);

  constexpr std::array<Level, 2> kMarketBook{{
      {.price = 949000, .volume = 20},
      {.price = 0, .volume = 30},
  }};
  const auto market_book =
      test::MakeDepth("2317", 90000000000, 0, false, 1, 1, kMarketBook, 0x10U);
  ASSERT_NE(decoder.Process(test::MakeHeader(MessageType::kStockDepthV,
                                             market_book.size(), 3),
                            market_book, 300),
            nullptr);

  constexpr std::array<Level, 1> kAmbiguousTrade{{
      {.price = 951000, .volume = 1},
  }};
  const auto ambiguous_trade = test::MakeDepth("2317", 90000100000, 1, true, 0,
                                               0, kAmbiguousTrade, 0x10U);
  record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, ambiguous_trade.size(), 4),
      ambiguous_trade, 400);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->trade_side, TradeSide::kUnknown);
  EXPECT_EQ(decoder.stats().sell_groups, 1);
  EXPECT_EQ(decoder.stats().unknown_groups, 1);
}

TEST(MessageDecoderTest, HeldTerminatesGroupAndInvalidatesBook) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  ApplyStockBasic(decoder, "2330");

  constexpr std::array<Level, 2> kPreBook{{
      {.price = 949000, .volume = 20},
      {.price = 950000, .volume = 30},
  }};
  const auto pre_book =
      test::MakeDepth("2330", 90000000000, 0, false, 1, 1, kPreBook, 0x10U);
  ASSERT_NE(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, pre_book.size(), 1),
                pre_book, 100),
            nullptr);

  constexpr std::array<Level, 1> kTrade{{
      {.price = 951000, .volume = 2},
  }};
  const auto trade = test::MakeDepth("2330", 90000123456, 2, true, 0, 0, kTrade,
                                     0x10U, 0, true);
  EXPECT_EQ(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, trade.size(), 2),
                trade, 200),
            nullptr);

  constexpr std::array<Level, 1> kHeldPayload{{
      {.price = 951000, .volume = 0},
  }};
  const auto held = test::MakeDepth("2330", 90000123456, 2, true, 0, 0,
                                    kHeldPayload, 0, 0x01U, true);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, held.size(), 3), held, 300);

  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->trade_side, TradeSide::kBuy);
  EXPECT_EQ(record->trade_count, 1);
  EXPECT_EQ(record->trade_volume, 2);
  EXPECT_DOUBLE_EQ(record->last_price, 95.1);
  EXPECT_EQ(record->limit_state.instantaneous_trend(),
            InstantaneousTrend::kHeldDown);
  for (std::size_t index = 0; index < 5; ++index) {
    EXPECT_DOUBLE_EQ(record->ask_price[index], 0.0);
    EXPECT_EQ(record->ask_volume[index], 0);
    EXPECT_DOUBLE_EQ(record->bid_price[index], 0.0);
    EXPECT_EQ(record->bid_volume[index], 0);
  }
  EXPECT_EQ(record->source_sequence, 3);
  EXPECT_EQ(decoder.stats().held_ended_groups, 1);

  constexpr std::array<Level, 3> kNext{{
      {.price = 952000, .volume = 1},
      {.price = 950000, .volume = 10},
      {.price = 953000, .volume = 11},
  }};
  const auto next =
      test::MakeDepth("2330", 90000200000, 3, true, 1, 1, kNext, 0x10U);
  record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, next.size(), 4), next, 400);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->trade_side, TradeSide::kUnknown);
}

TEST(MessageDecoderTest, TrialDoesNotMutateActualStateOrPendingGroup) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  ApplyStockBasic(decoder, "2330");

  constexpr std::array<Level, 0> kNoLevels{};
  const auto initial =
      test::MakeDepth("2330", 90000000000, 0, false, 0, 0, kNoLevels, 0x10U);
  ASSERT_NE(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, initial.size(), 1),
                initial, 100),
            nullptr);

  constexpr std::array<Level, 1> kTrialTrade{{
      {.price = 999000, .volume = 100},
  }};
  const auto trial =
      test::MakeDepth("2330", 90000100000, 100, true, 0, 0, kTrialTrade, 0x90U);
  EXPECT_EQ(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, trial.size(), 2),
                trial, 200),
            nullptr);

  const auto final =
      test::MakeDepth("2330", 90000200000, 0, false, 0, 0, kNoLevels, 0x10U);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, final.size(), 3), final, 300);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->trade_count, 0);
  EXPECT_EQ(record->trade_volume, 0);
  EXPECT_EQ(record->total_volume, 0);
  EXPECT_DOUBLE_EQ(record->total_value, 0.0);
  EXPECT_DOUBLE_EQ(record->last_price, 0.0);
  EXPECT_DOUBLE_EQ(record->open, 0.0);
  EXPECT_EQ(decoder.stats().actual_trade_payloads, 0);
}

TEST(MessageDecoderTest, PublishesZeroVolumeBookBeforeMultiplierArrives) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  constexpr std::array<Level, 2> kBook{{
      {.price = 949000, .volume = 20},
      {.price = 950000, .volume = 30},
  }};
  const auto book =
      test::MakeDepth("2330", 90000000000, 0, false, 1, 1, kBook, 0x10U);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, book.size(), 1), book, 100);
  ASSERT_NE(record, nullptr);
  EXPECT_DOUBLE_EQ(record->bid_price[0], 94.9);
  EXPECT_DOUBLE_EQ(record->ask_price[0], 95.0);
  EXPECT_EQ(decoder.stats().missing_multiplier_messages, 1);
  ASSERT_EQ(decoder.stats().missing_multiplier_symbols.size(), 1);

  ApplyStockBasic(decoder, "2330", ServiceType::kListed, 2);
  constexpr std::array<Level, 1> kTrade{{
      {.price = 951000, .volume = 1},
  }};
  const auto trade =
      test::MakeDepth("2330", 90000100000, 1, true, 0, 0, kTrade, 0x10U);
  record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, trade.size(), 2), trade, 200);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->trade_side, TradeSide::kBuy);
  EXPECT_DOUBLE_EQ(record->total_value, 95'100.0);
}

TEST(MessageDecoderTest, TimestampChangeInvalidatesStalePreMatchBook) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  ApplyStockBasic(decoder, "2330");
  constexpr std::array<Level, 2> kBook{{
      {.price = 949000, .volume = 20},
      {.price = 950000, .volume = 30},
  }};
  const auto book =
      test::MakeDepth("2330", 90000000000, 0, false, 1, 1, kBook, 0x10U);
  ASSERT_NE(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, book.size(), 1),
                book, 100),
            nullptr);

  constexpr std::array<Level, 1> kFirstTrade{{
      {.price = 951000, .volume = 1},
  }};
  const auto first = test::MakeDepth("2330", 90000100000, 1, true, 0, 0,
                                     kFirstTrade, 0x10U, 0, true);
  EXPECT_EQ(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, first.size(), 2),
                first, 200),
            nullptr);

  constexpr std::array<Level, 3> kLaterFinal{{
      {.price = 952000, .volume = 1},
      {.price = 950000, .volume = 10},
      {.price = 953000, .volume = 11},
  }};
  const auto later =
      test::MakeDepth("2330", 90000200000, 2, true, 1, 1, kLaterFinal, 0x10U);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, later.size(), 3), later, 300);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->trade_count, 1);
  EXPECT_EQ(record->trade_side, TradeSide::kUnknown);
  ASSERT_EQ(decoder.stats().incomplete_match_groups.size(), 1);
  EXPECT_EQ(decoder.stats().incomplete_match_groups.front().reason,
            MatchGroupIssueReason::kTimestampChanged);
}

TEST(MessageDecoderTest, SourceFormatChangeCannotJoinPendingMatchGroup) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kWarrant);
  ApplyStockBasic(decoder, "12345P");

  constexpr std::array<Level, 1> kFirstTrade{{
      {.price = 100000, .volume = 1},
  }};
  const auto first = test::MakeDepth("12345P", 90000100000, 1, true, 0, 0,
                                     kFirstTrade, 0x10U, 0, true);
  EXPECT_EQ(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, first.size(), 1),
                first, 100),
            nullptr);

  constexpr std::array<Level, 1> kFinalTrade{{
      {.price = 101000, .volume = 1},
  }};
  const auto final =
      test::MakeDepth("12345P", 90000100000, 2, true, 0, 0, kFinalTrade, 0x10U);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kWarrantDepthV, final.size(), 1), final,
      200);

  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->trade_count, 1);
  EXPECT_EQ(record->trade_volume, 1);
  EXPECT_EQ(record->trade_side, TradeSide::kUnknown);
  ASSERT_EQ(decoder.stats().incomplete_match_groups.size(), 1);
  EXPECT_EQ(decoder.stats().incomplete_match_groups.front().reason,
            MatchGroupIssueReason::kSourceFormatChanged);
}

TEST(MessageDecoderTest, MissingVolumeUsesOnlyLastPriceForDifference) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  ApplyStockBasic(decoder, "2330");

  constexpr std::array<Level, 1> kTrade{{
      {.price = 1000000, .volume = 3},
  }};
  const auto depth =
      test::MakeDepth("2330", 90000000000, 5, true, 0, 0, kTrade, 0x10U);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, depth.size(), 1), depth, 100);

  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->trade_count, 1);
  EXPECT_EQ(record->trade_volume, 3);
  EXPECT_EQ(record->total_volume, 5);
  EXPECT_DOUBLE_EQ(record->total_value, 500'000.0);
  ASSERT_EQ(decoder.stats().value_imputations.size(), 1);
  EXPECT_EQ(decoder.stats().value_imputations.front().missing_volume, 2);
  EXPECT_DOUBLE_EQ(decoder.stats().value_imputations.front().price, 100.0);
}

TEST(MessageDecoderTest, KeepsMarketIdentityAndDenseSymbolIdsSeparate) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  ApplyStockBasic(decoder, "2330", ServiceType::kListed, 1);
  ApplyStockBasic(decoder, "2330", ServiceType::kOtc, 1);

  constexpr std::array<Level, 1> kListedTrade{{
      {.price = 950000, .volume = 1},
  }};
  const auto listed =
      test::MakeDepth("2330", 90000000000, 1, true, 0, 0, kListedTrade, 0x10U);
  const auto *record =
      decoder.Process(test::MakeHeader(MessageType::kStockDepthV, listed.size(),
                                       1, ServiceType::kListed),
                      listed, 100);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->market, Market::kTwse);
  const auto listed_id = record->symbol_id;

  constexpr std::array<Level, 1> kOtcTrade{{
      {.price = 500000, .volume = 1},
  }};
  const auto otc =
      test::MakeDepth("2330", 90000000000, 1, true, 0, 0, kOtcTrade, 0x10U);
  record = decoder.Process(test::MakeHeader(MessageType::kStockDepthV,
                                            otc.size(), 1, ServiceType::kOtc),
                           otc, 101);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->market, Market::kTpex);
  EXPECT_NE(record->symbol_id, listed_id);
  EXPECT_EQ(decoder.symbol_count(), 2);
}

TEST(MessageDecoderTest, KeepsWarrantMetadataScopedToMarket) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kWarrant);
  const auto listed_basic = test::MakeBasicInfo(test::BasicInfoFields{
      .symbol = "1234P",
      .service_type = ServiceType::kListed,
      .security_type = "W2",
      .warrant_flag = 'Y',
      .maturity_date = 20261231,
      .market_data_line = 2,
  });
  EXPECT_EQ(decoder.Process(
                test::MakeHeader(MessageType::kStockBasicInfo,
                                 listed_basic.size(), 1, ServiceType::kListed),
                listed_basic, 0),
            nullptr);

  constexpr std::array<Level, 1> kTrade{{
      {.price = 12300, .volume = 1},
  }};
  const auto depth =
      test::MakeDepth("1234P", 90000000000, 1, true, 0, 0, kTrade, 0x10U);
  EXPECT_EQ(
      decoder.Process(test::MakeHeader(MessageType::kWarrantDepthV,
                                       depth.size(), 1, ServiceType::kOtc),
                      depth, 100),
      nullptr);
  const auto *record =
      decoder.Process(test::MakeHeader(MessageType::kWarrantDepthV,
                                       depth.size(), 1, ServiceType::kListed),
                      depth, 101);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->market, Market::kTwse);
}

TEST(MessageDecoderTest, OddLotReservedTagDoesNotStartMatchGroup) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kOddLot);
  constexpr std::array<Level, 1> kTrade{{
      {.price = 12500, .volume = 3'000'000'000ULL},
  }};
  const auto depth =
      test::MakeOddLotDepth("2330", 90000000000, 3'000'000'000ULL, true, 0, 0,
                            kTrade, 0x10U, 0, true);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kStockOddLotDepthV, depth.size(), 1), depth,
      100);

  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->trade_count, 1);
  EXPECT_EQ(record->trade_volume, 3'000'000'000LL);
  EXPECT_EQ(record->total_volume, 3'000'000'000LL);
  EXPECT_DOUBLE_EQ(record->total_value, 3'750'000'000.0);
  EXPECT_EQ(record->trade_side, TradeSide::kUnknown);
  EXPECT_TRUE(decoder.stats().incomplete_match_groups.empty());
}

TEST(MessageDecoderTest, SequenceGapDropsPendingGroupAndRecoversAtNextBook) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  ApplyStockBasic(decoder, "2330");

  constexpr std::array<Level, 2> kPreBook{{
      {.price = 949000, .volume = 20},
      {.price = 950000, .volume = 30},
  }};
  const auto pre_book =
      test::MakeDepth("2330", 90000000000, 0, false, 1, 1, kPreBook, 0x10U);
  ASSERT_NE(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, pre_book.size(), 1),
                pre_book, 100),
            nullptr);

  constexpr std::array<Level, 1> kTrade{{
      {.price = 951000, .volume = 2},
  }};
  const auto trade = test::MakeDepth("2330", 90000123456, 2, true, 0, 0, kTrade,
                                     0x10U, 0, true);
  EXPECT_EQ(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, trade.size(), 2),
                trade, 200),
            nullptr);

  constexpr std::array<Level, 3> kFinal{{
      {.price = 952000, .volume = 1},
      {.price = 949000, .volume = 10},
      {.price = 953000, .volume = 11},
  }};
  const auto final =
      test::MakeDepth("2330", 90000123456, 3, true, 1, 1, kFinal, 0x10U);
  EXPECT_EQ(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, final.size(), 4),
                final, 300),
            nullptr);
  ASSERT_EQ(decoder.stats().sequence_gaps.size(), 1);
  ASSERT_EQ(decoder.stats().incomplete_match_groups.size(), 1);
  EXPECT_EQ(decoder.stats().incomplete_match_groups.front().reason,
            MatchGroupIssueReason::kSequenceGap);

  constexpr std::array<Level, 2> kRecoveredBook{{
      {.price = 950000, .volume = 8},
      {.price = 954000, .volume = 9},
  }};
  const auto recovered = test::MakeDepth("2330", 90000200000, 3, false, 1, 1,
                                         kRecoveredBook, 0x10U);
  const auto *record = decoder.Process(
      test::MakeHeader(MessageType::kStockDepthV, recovered.size(), 5),
      recovered, 400);
  ASSERT_NE(record, nullptr);
  EXPECT_EQ(record->trade_count, 0);
  EXPECT_DOUBLE_EQ(record->last_price, 95.2);
  EXPECT_EQ(record->total_volume, 3);
  EXPECT_DOUBLE_EQ(record->total_value, 285'400.0);
}

TEST(MessageDecoderTest, FinalizeReportsUnclosedMatchGroup) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  ApplyStockBasic(decoder, "2330");
  constexpr std::array<Level, 1> kTrade{{
      {.price = 950000, .volume = 2},
  }};
  const auto trade = test::MakeDepth("2330", 90000123456, 2, true, 0, 0, kTrade,
                                     0x10U, 0, true);
  EXPECT_EQ(decoder.Process(
                test::MakeHeader(MessageType::kStockDepthV, trade.size(), 1),
                trade, 100),
            nullptr);

  decoder.Finalize();

  ASSERT_EQ(decoder.stats().incomplete_match_groups.size(), 1);
  EXPECT_EQ(decoder.stats().incomplete_match_groups.front().reason,
            MatchGroupIssueReason::kEndOfFile);
  decoder.Finalize();
  EXPECT_EQ(decoder.stats().incomplete_match_groups.size(), 1);
}

TEST(MessageDecoderTest, PreservesSymbolFilterSemantics) {
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
                                     oversized, 0),
               std::runtime_error);

  constexpr std::array<std::uint8_t, 3> kShortBody{};
  EXPECT_THROW((void)decoder.Process(test::MakeHeader(MessageType::kStockDepthV,
                                                      kShortBody.size()),
                                     kShortBody, 0),
               std::runtime_error);
}

TEST(MessageDecoderTest, RejectsUnsupportedFormatAndInvalidTrailer) {
  MessageDecoder decoder(20260707, SymbolFilterMode::kStock);
  constexpr std::array<Level, 0> kNoLevels{};
  const auto depth =
      test::MakeDepth("2330", 90000000000, 0, false, 0, 0, kNoLevels);
  auto header = test::MakeHeader(MessageType::kStockDepthV, depth.size(), 1);
  header.format_version = 5;
  EXPECT_THROW((void)decoder.Process(header, depth, 0), std::runtime_error);

  auto basic = test::MakeStockBasic("2330", 900000, 990000, 810000);
  basic.back() = 0;
  EXPECT_THROW((void)decoder.Process(
                   test::MakeHeader(MessageType::kStockBasicInfo, basic.size()),
                   basic, 0),
               std::runtime_error);
}

TEST(MessageDecoderTest, RejectsInvalidTradingDayAndMode) {
  EXPECT_THROW(MessageDecoder(20260230, SymbolFilterMode::kStock),
               std::invalid_argument);
  EXPECT_THROW((void)ParseSymbolFilterMode("future"), std::invalid_argument);
}

}  // namespace
}  // namespace aries::data::twse
