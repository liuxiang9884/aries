#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "data/converter/taifex/message_decoder.h"
#include "tests/data/converter/taifex/test_message_builder.h"

namespace aries::data::taifex {
namespace {

MessageHeader Header(char transmission, char kind, std::uint8_t version,
                     std::size_t body_size) {
  return MessageHeader{.transmission_code = transmission,
                       .message_kind = kind,
                       .exchange_time_ns = 32'400'000'000'000LL,
                       .channel_id = 1,
                       .channel_sequence = 1,
                       .version = version,
                       .body_length = body_size};
}

TEST(TaifexOrderbookTest, TemplateControlsLevelCount) {
  const Orderbook<3> orderbook;

  EXPECT_EQ(orderbook.ask_price.size(), 3);
  EXPECT_EQ(orderbook.ask_volume.size(), 3);
  EXPECT_EQ(orderbook.bid_price.size(), 3);
  EXPECT_EQ(orderbook.bid_volume.size(), 3);
}

TEST(TaifexMessageDecoderTest, BuildsTradeAndIncrementalBookWithMultiplier) {
  MessageDecoder decoder(20260707);
  std::vector<Orderbook<5>> rows;
  const auto emit = [&](const Orderbook<5> &record) { rows.push_back(record); };

  const auto kind = test::MakeI011("TXF", 200.0, 2, 'I');
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  const auto basic = test::MakeI010("TXFG6", 22'000'00, 2, 'I');
  decoder.Process(Header('1', '1', 9, basic.size()), basic, emit);
  constexpr std::array<test::Trade, 1> kExtraTrades{{
      {.price = 22'101'00, .volume = 2},
  }};
  const auto trade =
      test::MakeI024("TXFG6", 1, 22'100'00, 3, 5, 2, 1, '0', kExtraTrades);
  decoder.Process(Header('2', 'D', 1, trade.size()), trade, emit);
  const auto high_low = test::MakeI025("TXFG6", 2, 22'150'00, 22'050'00);
  decoder.Process(Header('2', 'E', 1, high_low.size()), high_low, emit);
  const auto update = test::MakeI081(
      "TXFG6", 3, '0',
      {.type = '0', .price = 22'099'00, .volume = 7, .level = 1});
  decoder.Process(Header('2', 'A', 1, update.size()), update, emit);

  ASSERT_EQ(rows.size(), 1);
  const auto &row = rows.front();
  EXPECT_EQ(row.symbol, "TXFG6");
  EXPECT_DOUBLE_EQ(row.reference_price, 22000.0);
  EXPECT_DOUBLE_EQ(row.open, 22100.0);
  EXPECT_DOUBLE_EQ(row.high, 22150.0);
  EXPECT_DOUBLE_EQ(row.low, 22050.0);
  EXPECT_DOUBLE_EQ(row.last_price, 22101.0);
  EXPECT_EQ(row.trade_volume, 5);
  EXPECT_EQ(row.total_volume, 5);
  EXPECT_DOUBLE_EQ(row.total_value, 22'100'400.0);
  EXPECT_EQ(row.total_buy_count, 2);
  EXPECT_EQ(row.total_sell_count, 1);
  EXPECT_DOUBLE_EQ(row.bid_price[0], 22099.0);
  EXPECT_EQ(row.bid_volume[0], 7);
  EXPECT_EQ(row.match_flag, 0);
  EXPECT_EQ(row.build_type, 0);
  EXPECT_EQ(row.orderbook_action, 1);
  EXPECT_EQ(row.sequence, 3);
}

TEST(TaifexMessageDecoderTest, AccumulatesAbsoluteValueForNegativeSpreadTrade) {
  MessageDecoder decoder(20260707);
  std::vector<Orderbook<5>> rows;
  const auto emit = [&](const Orderbook<5> &record) { rows.push_back(record); };
  const auto kind = test::MakeI011("TXF", 200.0);
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  const auto trade =
      test::MakeI024("TXFG6/H6", 1, 125, 4, 4, 1, 0, '0', {}, '-');
  decoder.Process(Header('2', 'D', 1, trade.size()), trade, emit);
  const auto update = test::MakeI081(
      "TXFG6/H6", 2, '0', {.type = '0', .price = 100, .volume = 3, .level = 1});
  decoder.Process(Header('2', 'A', 1, update.size()), update, emit);

  ASSERT_EQ(rows.size(), 1);
  EXPECT_DOUBLE_EQ(rows.front().last_price, -1.25);
  EXPECT_DOUBLE_EQ(rows.front().total_value, 1'000.0);
}

TEST(TaifexMessageDecoderTest,
     ParsesPriceLimitsWithoutChangingOpeningReferencePrice) {
  MessageDecoder decoder(20260707);
  std::vector<Orderbook<5>> rows;
  const auto emit = [&](const Orderbook<5> &record) { rows.push_back(record); };
  const auto kind = test::MakeI011("TXF", 200.0);
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  const auto basic = test::MakeI010("TXFG6", 22'000'00);
  decoder.Process(Header('1', '1', 9, basic.size()), basic, emit);
  constexpr std::array<test::PriceLimit, 2> kRaise{{
      {.level = 1, .price = 24'200'00},
      {.level = 2, .price = 26'400'00},
  }};
  constexpr std::array<test::PriceLimit, 2> kFall{{
      {.level = 1, .price = 19'800'00},
      {.level = 2, .price = 17'600'00},
  }};
  const auto limits = test::MakeI012("TXFG6", kRaise, kFall);
  decoder.Process(Header('1', 'A', 1, limits.size()), limits, emit);
  decoder.Process(Header('1', 'A', 1, limits.size()), limits, emit);
  const auto update = test::MakeI081(
      "TXFG6", 1, '0',
      {.type = '0', .price = 22'000'00, .volume = 3, .level = 1});
  decoder.Process(Header('2', 'A', 1, update.size()), update, emit);

  ASSERT_EQ(rows.size(), 1);
  EXPECT_DOUBLE_EQ(rows.front().reference_price, 22'000.0);
  EXPECT_EQ(decoder.stats().price_limit_messages, 2);
  EXPECT_EQ(decoder.stats().identical_price_limit_duplicates, 1);
  EXPECT_EQ(decoder.stats().ignored_messages, 0);
}

TEST(TaifexMessageDecoderTest,
     RecordsConflictingPriceLimitsAndKeepsConverting) {
  MessageDecoder decoder(20260707);
  const auto emit = [](const Orderbook<5> &) {};
  constexpr std::array<test::PriceLimit, 1> kRaise{{
      {.level = 1, .price = 24'200'00},
  }};
  constexpr std::array<test::PriceLimit, 1> kFall{{
      {.level = 1, .price = 19'800'00},
  }};
  constexpr std::array<test::PriceLimit, 1> kChangedFall{{
      {.level = 1, .price = 19'700'00},
  }};
  const auto first = test::MakeI012("TXFG6", kRaise, kFall);
  decoder.Process(Header('1', 'A', 1, first.size()), first, emit);
  const auto changed = test::MakeI012("TXFG6", kRaise, kChangedFall);
  auto changed_header = Header('1', 'A', 1, changed.size());
  changed_header.channel_sequence = 2;
  decoder.Process(changed_header, changed, emit);

  EXPECT_EQ(decoder.stats().price_limit_messages, 2);
  EXPECT_EQ(decoder.stats().price_limit_conflicts, 1);
  ASSERT_EQ(decoder.issues().size(), 1);
  EXPECT_EQ(decoder.issues().front().kind, IssueKind::kPriceLimitConflict);
  EXPECT_EQ(decoder.issues().front().symbol, "TXFG6");
  EXPECT_EQ(decoder.issues().front().expected_sequence, 1);
  EXPECT_EQ(decoder.issues().front().actual_sequence, 2);
}

TEST(TaifexMessageDecoderTest, RejectsPriceLimitsWithoutBothSides) {
  MessageDecoder decoder(20260707);
  const auto emit = [](const Orderbook<5> &) {};
  constexpr std::array<test::PriceLimit, 1> kFall{{
      {.level = 1, .price = 19'800'00},
  }};
  const auto limits = test::MakeI012("TXFG6", {}, kFall);

  EXPECT_THROW(
      decoder.Process(Header('1', 'A', 1, limits.size()), limits, emit),
      std::runtime_error);
}

TEST(TaifexMessageDecoderTest, PreservesZeroOpeningPriceForCalendarSpread) {
  MessageDecoder decoder(20260707);
  std::vector<Orderbook<5>> rows;
  const auto emit = [&](const Orderbook<5> &record) { rows.push_back(record); };

  const auto kind = test::MakeI011("TXF", 200.0);
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  const auto zero_trade = test::MakeI024("TXFG6/H6", 1, 0, 1, 1, 1, 0);
  decoder.Process(Header('2', 'D', 1, zero_trade.size()), zero_trade, emit);
  const auto positive_trade = test::MakeI024("TXFG6/H6", 2, 100, 1, 2, 2, 0);
  decoder.Process(Header('2', 'D', 1, positive_trade.size()), positive_trade,
                  emit);
  const auto update = test::MakeI081(
      "TXFG6/H6", 3, '0', {.type = '0', .price = 50, .volume = 3, .level = 1});
  decoder.Process(Header('2', 'A', 1, update.size()), update, emit);

  ASSERT_EQ(rows.size(), 1);
  EXPECT_DOUBLE_EQ(rows.front().open, 0.0);
  EXPECT_DOUBLE_EQ(rows.front().high, 1.0);
  EXPECT_DOUBLE_EQ(rows.front().low, 0.0);
}

TEST(TaifexMessageDecoderTest, DecodesHeaderAndRejectsInvalidEscape) {
  std::vector<std::uint8_t> frame;
  const std::vector<std::uint8_t> empty;
  test::AppendFrame(frame, '0', '1', 1, empty, 42);
  const auto header =
      DecodeMessageHeader(std::span(frame).first(protocol::kHeaderSize));
  EXPECT_EQ(header.transmission_code, '0');
  EXPECT_EQ(header.message_kind, '1');
  EXPECT_EQ(header.channel_sequence, 42);
  EXPECT_EQ(header.body_length, 0);

  frame[0] = 0;
  EXPECT_THROW(
      (void)DecodeMessageHeader(std::span(frame).first(protocol::kHeaderSize)),
      std::runtime_error);
}

TEST(TaifexMessageDecoderTest, ResetClearsBookAndRestartsProductSequence) {
  MessageDecoder decoder(20260707);
  std::vector<Orderbook<5>> rows;
  const auto emit = [&](const Orderbook<5> &record) { rows.push_back(record); };
  const auto kind = test::MakeI011("TXF", 200.0);
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  const auto basic = test::MakeI010("TXFG6", 22'000'00);
  decoder.Process(Header('1', '1', 9, basic.size()), basic, emit);
  const auto first = test::MakeI081(
      "TXFG6", 1, '0',
      {.type = '0', .price = 22'000'00, .volume = 5, .level = 1});
  decoder.Process(Header('2', 'A', 1, first.size()), first, emit);

  const std::vector<std::uint8_t> empty;
  decoder.Process(Header('0', '2', 1, 0), empty, emit);
  const auto after_reset = test::MakeI081(
      "TXFG6", 1, '0',
      {.type = '1', .price = 22'001'00, .volume = 6, .level = 1});
  decoder.Process(Header('2', 'A', 1, after_reset.size()), after_reset, emit);

  ASSERT_EQ(rows.size(), 2);
  EXPECT_DOUBLE_EQ(rows.back().bid_price[0], 0.0);
  EXPECT_DOUBLE_EQ(rows.back().ask_price[0], 22001.0);
  EXPECT_EQ(rows.back().sequence, 1);
  EXPECT_EQ(decoder.stats().reset_messages, 1);
}

TEST(TaifexMessageDecoderTest, FullSnapshotClearsOrdinaryAndDerivedLevels) {
  MessageDecoder decoder(20260707);
  std::vector<Orderbook<5>> rows;
  const auto emit = [&](const Orderbook<5> &record) { rows.push_back(record); };
  const auto kind = test::MakeI011("TXF", 200.0);
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  const auto basic = test::MakeI010("TXFG6", 22'000'00);
  decoder.Process(Header('1', '1', 9, basic.size()), basic, emit);

  const auto bid = test::MakeI081(
      "TXFG6", 1, '0',
      {.type = '0', .price = 22'000'00, .volume = 5, .level = 1});
  decoder.Process(Header('2', 'A', 1, bid.size()), bid, emit);
  const auto derived = test::MakeI081(
      "TXFG6", 2, '5',
      {.type = 'E', .price = 21'999'00, .volume = 4, .level = 1});
  decoder.Process(Header('2', 'A', 1, derived.size()), derived, emit);
  constexpr std::array<test::BookLevel, 1> kAsk{{
      {.type = '1', .price = 22'001'00, .volume = 6, .level = 1},
  }};
  const auto snapshot = test::MakeI083("TXFG6", 3, kAsk);
  decoder.Process(Header('2', 'B', 1, snapshot.size()), snapshot, emit);

  ASSERT_EQ(rows.size(), 3);
  const auto &row = rows.back();
  EXPECT_DOUBLE_EQ(row.ask_price[0], 22001.0);
  EXPECT_DOUBLE_EQ(row.bid_price[0], 0.0);
  EXPECT_DOUBLE_EQ(row.derived_bid_price, 0.0);
  EXPECT_EQ(row.build_type, 3);
}

TEST(TaifexMessageDecoderTest, RecoversGapFromI084AndReplaysCachedUpdate) {
  MessageDecoder decoder(20260707);
  std::vector<Orderbook<5>> rows;
  const auto emit = [&](const Orderbook<5> &record) { rows.push_back(record); };
  const auto kind = test::MakeI011("TXF", 200.0);
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  const auto basic = test::MakeI010("TXFG6", 22'000'00);
  decoder.Process(Header('1', '1', 9, basic.size()), basic, emit);
  const auto first = test::MakeI081(
      "TXFG6", 1, '0',
      {.type = '0', .price = 22'000'00, .volume = 5, .level = 1});
  decoder.Process(Header('2', 'A', 1, first.size()), first, emit);
  const auto after_gap = test::MakeI081(
      "TXFG6", 3, '1',
      {.type = '0', .price = 22'002'00, .volume = 8, .level = 1});
  decoder.Process(Header('2', 'A', 1, after_gap.size()), after_gap, emit);
  constexpr std::array<test::BookLevel, 1> kRecovered{{
      {.type = '0', .price = 22'001'00, .volume = 6, .level = 1},
  }};
  const auto recovery = test::MakeI084Orderbook("TXFG6", 2, kRecovered);
  decoder.Process(Header('2', 'C', 3, recovery.size()), recovery, emit);
  const auto after_recovery = test::MakeI081(
      "TXFG6", 4, '1',
      {.type = '0', .price = 22'003'00, .volume = 9, .level = 1});
  decoder.Process(Header('2', 'A', 1, after_recovery.size()), after_recovery,
                  emit);

  ASSERT_EQ(rows.size(), 3);
  EXPECT_EQ(rows.back().bid_price[0], 22003.0);
  EXPECT_EQ(rows.back().bid_volume[0], 9);
  EXPECT_EQ(rows.back().sequence, 4);
  EXPECT_EQ(decoder.stats().sequence_gaps, 1);
  EXPECT_EQ(decoder.stats().snapshot_recoveries, 1);
  ASSERT_EQ(decoder.issues().size(), 1);
  EXPECT_EQ(decoder.issues().front().kind, IssueKind::kSequenceGap);
  EXPECT_TRUE(decoder.issues().front().recovered);
  EXPECT_EQ(decoder.issues().front().recovery_sequence, 2);
}

TEST(TaifexMessageDecoderTest, UsesKindMetadataForSpread) {
  MessageDecoder decoder(20260707);
  std::vector<Orderbook<5>> rows;
  const auto emit = [&](const Orderbook<5> &record) { rows.push_back(record); };
  const auto kind = test::MakeI011("TXF", 200.0);
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  const auto spread = test::MakeI081(
      "TXFG6/H6", 1, '0', {.type = '0', .price = 125, .volume = 4, .level = 1});
  decoder.Process(Header('2', 'A', 1, spread.size()), spread, emit);

  ASSERT_EQ(rows.size(), 1);
  EXPECT_DOUBLE_EQ(rows.front().bid_price[0], 1.25);
  const auto basic = decoder.BasicInfoRecords();
  const auto iterator =
      std::find_if(basic.begin(), basic.end(), [](const auto &record) {
        return record.symbol == "TXFG6/H6";
      });
  ASSERT_NE(iterator, basic.end());
  EXPECT_TRUE(iterator->is_spread);
  EXPECT_EQ(iterator->basic_source, "I011");
  EXPECT_DOUBLE_EQ(iterator->multiplier, 200.0);
}

TEST(TaifexMessageDecoderTest,
     DoesNotRollBackReplayedEventsWithOlderSnapshotStatistics) {
  MessageDecoder decoder(20260707);
  std::vector<Orderbook<5>> rows;
  const auto emit = [&](const Orderbook<5> &record) { rows.push_back(record); };
  const auto kind = test::MakeI011("TXF", 200.0);
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  const auto basic = test::MakeI010("TXFG6", 22'000'00);
  decoder.Process(Header('1', '1', 9, basic.size()), basic, emit);
  const auto first = test::MakeI081(
      "TXFG6", 1, '0',
      {.type = '0', .price = 22'000'00, .volume = 5, .level = 1});
  decoder.Process(Header('2', 'A', 1, first.size()), first, emit);
  const auto cached_trade = test::MakeI024("TXFG6", 3, 22'100'00, 2, 2, 1, 1);
  decoder.Process(Header('2', 'D', 1, cached_trade.size()), cached_trade, emit);
  constexpr std::array<test::BookLevel, 1> kRecovered{{
      {.type = '0', .price = 22'050'00, .volume = 6, .level = 1},
  }};
  const auto recovery = test::MakeI084Orderbook("TXFG6", 2, kRecovered);
  decoder.Process(Header('2', 'C', 3, recovery.size()), recovery, emit);
  const auto stale_statistics =
      test::MakeI084Statistics("TXFG6", 10'000'00, 1, 1, 1, 0);
  decoder.Process(Header('2', 'C', 3, stale_statistics.size()),
                  stale_statistics, emit);
  const auto after = test::MakeI081(
      "TXFG6", 4, '1',
      {.type = '0', .price = 22'099'00, .volume = 7, .level = 1});
  decoder.Process(Header('2', 'A', 1, after.size()), after, emit);

  ASSERT_EQ(rows.size(), 2);
  EXPECT_DOUBLE_EQ(rows.back().last_price, 22100.0);
  EXPECT_DOUBLE_EQ(rows.back().open, 10000.0);
  EXPECT_DOUBLE_EQ(rows.back().high, 22100.0);
  EXPECT_DOUBLE_EQ(rows.back().low, 10000.0);
  EXPECT_EQ(rows.back().trade_volume, 2);
  EXPECT_EQ(rows.back().total_volume, 2);
}

TEST(TaifexMessageDecoderTest,
     AppliesSnapshotStatisticsAfterReplayingOnlyOrderBookEvents) {
  MessageDecoder decoder(20260707);
  std::vector<Orderbook<5>> rows;
  const auto emit = [&](const Orderbook<5> &record) { rows.push_back(record); };
  const auto kind = test::MakeI011("TXF", 200.0);
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  const auto basic = test::MakeI010("TXFG6", 22'000'00);
  decoder.Process(Header('1', '1', 9, basic.size()), basic, emit);
  const auto first = test::MakeI081(
      "TXFG6", 1, '0',
      {.type = '0', .price = 22'000'00, .volume = 5, .level = 1});
  decoder.Process(Header('2', 'A', 1, first.size()), first, emit);
  const auto cached_book = test::MakeI081(
      "TXFG6", 3, '1',
      {.type = '0', .price = 22'050'00, .volume = 6, .level = 1});
  decoder.Process(Header('2', 'A', 1, cached_book.size()), cached_book, emit);
  constexpr std::array<test::BookLevel, 1> kRecovered{{
      {.type = '0', .price = 22'025'00, .volume = 4, .level = 1},
  }};
  const auto recovery = test::MakeI084Orderbook("TXFG6", 2, kRecovered);
  decoder.Process(Header('2', 'C', 3, recovery.size()), recovery, emit);
  const auto statistics =
      test::MakeI084Statistics("TXFG6", 22'010'00, 2, 7, 4, 3);
  decoder.Process(Header('2', 'C', 3, statistics.size()), statistics, emit);
  const auto after = test::MakeI081(
      "TXFG6", 4, '1',
      {.type = '0', .price = 22'075'00, .volume = 7, .level = 1});
  decoder.Process(Header('2', 'A', 1, after.size()), after, emit);

  ASSERT_EQ(rows.size(), 3);
  EXPECT_DOUBLE_EQ(rows.back().last_price, 22010.0);
  EXPECT_DOUBLE_EQ(rows.back().open, 22010.0);
  EXPECT_DOUBLE_EQ(rows.back().high, 22010.0);
  EXPECT_DOUBLE_EQ(rows.back().low, 22010.0);
  EXPECT_EQ(rows.back().total_volume, 7);
  EXPECT_EQ(rows.back().total_buy_count, 4);
  EXPECT_EQ(rows.back().total_sell_count, 3);
}

TEST(TaifexMessageDecoderTest, ReportsUnresolvedGapAtEndOfDump) {
  MessageDecoder decoder(20260707);
  const auto emit = [](const Orderbook<5> &) {};
  const auto kind = test::MakeI011("TXF", 200.0);
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  const auto basic = test::MakeI010("TXFG6", 22'000'00);
  decoder.Process(Header('1', '1', 9, basic.size()), basic, emit);
  const auto after_gap = test::MakeI081(
      "TXFG6", 2, '0',
      {.type = '0', .price = 22'000'00, .volume = 5, .level = 1});
  decoder.Process(Header('2', 'A', 1, after_gap.size()), after_gap, emit);

  EXPECT_NO_THROW(decoder.Finalize());
  EXPECT_EQ(decoder.stats().unresolved_sequence_gaps, 1);
  ASSERT_EQ(decoder.issues().size(), 1);
  EXPECT_EQ(decoder.issues().front().kind, IssueKind::kSequenceGap);
  EXPECT_EQ(decoder.issues().front().symbol, "TXFG6");
  EXPECT_EQ(decoder.issues().front().expected_sequence, 1);
  EXPECT_EQ(decoder.issues().front().actual_sequence, 2);
  EXPECT_FALSE(decoder.issues().front().recovered);
}

TEST(TaifexMessageDecoderTest, ReportsMetadataMissingAndIgnoredMessageTypes) {
  MessageDecoder decoder(20260707);
  const auto emit = [](const Orderbook<5> &) {};
  const auto update = test::MakeI081(
      "TXFG6", 7, '0',
      {.type = '0', .price = 22'000'00, .volume = 5, .level = 1});
  decoder.Process(Header('2', 'A', 1, update.size()), update, emit);
  const std::vector<std::uint8_t> ignored;
  decoder.Process(Header('1', '2', 1, ignored.size()), ignored, emit);

  ASSERT_EQ(decoder.issues().size(), 1);
  EXPECT_EQ(decoder.issues().front().kind, IssueKind::kMetadataMissing);
  EXPECT_EQ(decoder.issues().front().symbol, "TXFG6");
  EXPECT_EQ(decoder.issues().front().actual_sequence, 7);
  const auto counts = decoder.IgnoredMessageCounts();
  ASSERT_EQ(counts.size(), 1);
  EXPECT_EQ(counts.front().transmission_code, '1');
  EXPECT_EQ(counts.front().message_kind, '2');
  EXPECT_EQ(counts.front().count, 1);
}

TEST(TaifexMessageDecoderTest,
     ReportsProductWhoseContractMetadataNeverArrives) {
  MessageDecoder decoder(20260707);
  const auto emit = [](const Orderbook<5> &) {};
  const auto basic = test::MakeI010("TXFG6", 22'000'00);
  decoder.Process(Header('1', '1', 9, basic.size()), basic, emit);

  decoder.Finalize();

  EXPECT_TRUE(decoder.BasicInfoRecords().empty());
  ASSERT_EQ(decoder.issues().size(), 1);
  EXPECT_EQ(decoder.issues().front().kind, IssueKind::kMetadataMissing);
  EXPECT_EQ(decoder.issues().front().symbol, "TXFG6");
  EXPECT_EQ(decoder.issues().front().transmission_code, '1');
  EXPECT_EQ(decoder.issues().front().message_kind, '3');
}

TEST(TaifexMessageDecoderTest, DisablesOnlyOverflowingSymbolAndReportsIssue) {
  MessageDecoder decoder(20260707);
  std::vector<Orderbook<5>> rows;
  const auto emit = [&](const Orderbook<5> &record) { rows.push_back(record); };
  const auto kind = test::MakeI011("TXF", 200.0);
  decoder.Process(Header('1', '3', 4, kind.size()), kind, emit);
  for (std::uint64_t sequence = 2;
       sequence <= protocol::kMaximumCachedEventsPerSymbol + 2; ++sequence) {
    const auto update = test::MakeI081(
        "TXFG6", sequence, '0',
        {.type = '0', .price = 22'000'00, .volume = 5, .level = 1});
    decoder.Process(Header('2', 'A', 1, update.size()), update, emit);
  }
  decoder.Finalize();

  EXPECT_TRUE(rows.empty());
  EXPECT_EQ(decoder.stats().sequence_gaps, 1);
  EXPECT_EQ(decoder.stats().unresolved_sequence_gaps, 1);
  EXPECT_EQ(decoder.stats().gap_cache_overflows, 1);
  ASSERT_EQ(decoder.issues().size(), 2);
  EXPECT_EQ(decoder.issues()[0].kind, IssueKind::kSequenceGap);
  EXPECT_EQ(decoder.issues()[1].kind, IssueKind::kGapCacheOverflow);
  EXPECT_EQ(decoder.issues()[1].symbol, "TXFG6");
}

} // namespace
} // namespace aries::data::taifex
