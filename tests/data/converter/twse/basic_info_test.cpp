#include <cstdint>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "data/converter/twse/basic_info.h"
#include "tests/data/converter/twse/test_message_builder.h"

namespace aries::data::twse {
namespace {

TEST(BasicInfoDecoderTest, DecodesListedWarrantAndNormalizesUnits) {
  const test::BasicInfoFields fields{
      .symbol = "067288",
      .industry_code = "00",
      .security_type = "W2",
      .anomaly_code = 3,
      .board_code = '0',
      .reference_price = 16600,
      .high_limit = 25900,
      .low_limit = 7300,
      .abnormal_recommendation = 'Y',
      .special_abnormal = 'N',
      .day_trading_code = 'Y',
      .margin_short_exempt = 'S',
      .borrow_short_exempt = 'B',
      .matching_cycle_seconds = 20,
      .warrant_flag = 'Y',
      .strike_price = 79275,
      .previous_exercise_volume = 12,
      .previous_cancellation_volume = 3,
      .outstanding_volume = 2000,
      .exercise_ratio = 1400,
      .warrant_upper_price = 30000,
      .warrant_lower_price = 100,
      .maturity_date = 20261217,
      .foreign_stock_flag = 'N',
      .multiplier = 1000,
      .currency = "   ",
      .market_data_line = 2,
  };
  const auto body = test::MakeBasicInfo(fields);
  const auto decoded = DecodeBasicInfo(
      20260707, test::MakeHeader(MessageType::kStockBasicInfo, body.size(), 31),
      body);

  ASSERT_TRUE(decoded.record.has_value());
  const auto &record = *decoded.record;
  EXPECT_EQ(record.trading_day, 20260707);
  EXPECT_EQ(record.market, "TWSE");
  EXPECT_EQ(record.symbol, "067288");
  EXPECT_EQ(record.industry_code, "00");
  EXPECT_EQ(record.security_type, "W2");
  EXPECT_EQ(record.anomaly_code, 3);
  EXPECT_FALSE(record.stock_group_code.has_value());
  EXPECT_EQ(record.board_code, "0");
  EXPECT_DOUBLE_EQ(record.reference_price, 1.66);
  EXPECT_DOUBLE_EQ(record.high_limit, 2.59);
  EXPECT_DOUBLE_EQ(record.low_limit, 0.73);
  EXPECT_EQ(record.abnormal_recommendation, "Y");
  EXPECT_EQ(record.special_abnormal, "N");
  EXPECT_EQ(record.day_trading_code, "Y");
  EXPECT_EQ(record.margin_short_exempt, "S");
  EXPECT_EQ(record.borrow_short_exempt, "B");
  EXPECT_EQ(record.matching_cycle_seconds, 20);
  EXPECT_EQ(record.warrant_flag, "Y");
  ASSERT_TRUE(record.strike_price.has_value());
  EXPECT_DOUBLE_EQ(*record.strike_price, 7.9275);
  EXPECT_EQ(record.previous_exercise_volume, 12'000);
  EXPECT_EQ(record.previous_cancellation_volume, 3'000);
  EXPECT_EQ(record.outstanding_volume, 2'000'000);
  ASSERT_TRUE(record.exercise_ratio.has_value());
  EXPECT_DOUBLE_EQ(*record.exercise_ratio, 0.014);
  EXPECT_DOUBLE_EQ(*record.warrant_upper_price, 3.0);
  EXPECT_DOUBLE_EQ(*record.warrant_lower_price, 0.01);
  EXPECT_EQ(record.maturity_date, 20261217);
  EXPECT_EQ(record.foreign_stock_flag, "N");
  EXPECT_EQ(record.multiplier, 1000);
  EXPECT_EQ(record.currency, "TWD");
  EXPECT_EQ(record.market_data_line, 2);
  EXPECT_TRUE(IsWarrantSecurity(record));
}

TEST(BasicInfoDecoderTest, DecodesTpexLayoutAndLeavesInapplicableFieldsEmpty) {
  const test::BasicInfoFields fields{
      .symbol = "6488",
      .service_type = ServiceType::kOtc,
      .industry_code = "24",
      .security_type = "01",
      .stock_group_code = 'S',
      .board_code = 'R',
      .reference_price = 500000,
      .high_limit = 550000,
      .low_limit = 450000,
      .multiplier = 1000,
      .currency = "TWD",
  };
  const auto body = test::MakeBasicInfo(fields);
  const auto decoded =
      DecodeBasicInfo(20260707,
                      test::MakeHeader(MessageType::kStockBasicInfo,
                                       body.size(), 8, ServiceType::kOtc),
                      body);

  ASSERT_TRUE(decoded.record.has_value());
  const auto &record = *decoded.record;
  EXPECT_EQ(record.market, "TPEX");
  EXPECT_EQ(record.symbol, "6488");
  EXPECT_EQ(record.stock_group_code, "S");
  EXPECT_EQ(record.board_code, "R");
  EXPECT_DOUBLE_EQ(record.reference_price, 50.0);
  EXPECT_FALSE(record.strike_price.has_value());
  EXPECT_FALSE(record.previous_exercise_volume.has_value());
  EXPECT_FALSE(record.maturity_date.has_value());
  EXPECT_FALSE(record.foreign_stock_flag.has_value());
  EXPECT_FALSE(IsWarrantSecurity(record));
}

TEST(BasicInfoDecoderTest, RecognizesControlRecordsBeforeDecodingDataFields) {
  auto body = test::MakeBasicControl("000123", "AL");
  body[30] = 0xFA;
  const auto decoded = DecodeBasicInfo(
      20260707, test::MakeHeader(MessageType::kStockBasicInfo, body.size(), 9),
      body);

  EXPECT_FALSE(decoded.record.has_value());
  EXPECT_EQ(decoded.control_kind, BasicInfoControlKind::kAll);
  EXPECT_EQ(decoded.control_count, 123);
}

TEST(BasicInfoDecoderTest, RejectsCsvDelimiterInOutputCodes) {
  const test::BasicInfoFields fields{
      .industry_code = "0,",
  };
  const auto body = test::MakeBasicInfo(fields);

  EXPECT_THROW((void)DecodeBasicInfo(
                   20260707,
                   test::MakeHeader(MessageType::kStockBasicInfo, body.size()),
                   body),
               std::runtime_error);
}

TEST(BasicInfoCatalogTest, IgnoresIdenticalDuplicatesAndRejectsFieldChanges) {
  BasicInfoCatalog catalog(20260707);
  auto body = test::MakeBasicInfo(test::BasicInfoFields{});
  auto header = test::MakeHeader(MessageType::kStockBasicInfo, body.size(), 10);
  ASSERT_NE(catalog.Process(header, body, 100), nullptr);
  header.sequence = 11;
  ASSERT_NE(catalog.Process(header, body, 214), nullptr);
  EXPECT_EQ(catalog.records().size(), 1);
  EXPECT_EQ(catalog.identical_duplicates(), 1);

  test::PutBcd<5>(body, protocol::kListedPreviousCloseOffset, 910000);
  header.sequence = 12;
  try {
    (void)catalog.Process(header, body, 328);
    FAIL() << "expected a changed duplicate to fail";
  } catch (const std::runtime_error &error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("reference_price"), std::string::npos);
    EXPECT_NE(message.find("old_offset=100"), std::string::npos);
    EXPECT_NE(message.find("new_offset=328"), std::string::npos);
  }
}

TEST(BasicInfoCatalogTest, RecordsAllAndNewCycleCountMismatches) {
  BasicInfoCatalog catalog(20260707);
  const auto first = test::MakeBasicInfo(test::BasicInfoFields{});
  const auto second =
      test::MakeBasicInfo(test::BasicInfoFields{.symbol = "2317"});
  auto header = test::MakeHeader(MessageType::kStockBasicInfo, first.size(), 1);
  (void)catalog.Process(header, first, 0);
  header.sequence = 2;
  (void)catalog.Process(header, second, 114);
  const auto control = test::MakeBasicControl("000002", "AL");
  header.sequence = 3;
  EXPECT_EQ(catalog.Process(header, control, 228), nullptr);
  EXPECT_EQ(catalog.control_records(), 1);

  const auto bad_control = test::MakeBasicControl("000001", "NE");
  header.sequence = 4;
  EXPECT_EQ(catalog.Process(header, bad_control, 342), nullptr);
  EXPECT_EQ(catalog.control_records(), 2);
  ASSERT_EQ(catalog.cycle_mismatches().size(), 1);
  const auto &issue = catalog.cycle_mismatches().front();
  EXPECT_EQ(issue.service_type, ServiceType::kListed);
  EXPECT_EQ(issue.control_kind, BasicInfoControlKind::kNew);
  EXPECT_EQ(issue.expected, 1);
  EXPECT_EQ(issue.actual, 0);
  EXPECT_EQ(issue.offset, 342);
  EXPECT_EQ(issue.sequence, 4);
}

TEST(BasicInfoCatalogTest, FirstControlSynchronizesAPartialCapture) {
  BasicInfoCatalog catalog(20260707);
  const auto basic = test::MakeBasicInfo(test::BasicInfoFields{});
  auto header = test::MakeHeader(MessageType::kStockBasicInfo, basic.size(), 1);
  (void)catalog.Process(header, basic, 0);
  const auto first_control = test::MakeBasicControl("030488", "AL");
  header.sequence = 2;

  EXPECT_EQ(catalog.Process(header, first_control, 114), nullptr);
  EXPECT_EQ(catalog.control_records(), 1);
}

TEST(BasicInfoCatalogTest, ReturnsRecordsInPrimaryKeyOrder) {
  BasicInfoCatalog catalog(20260707);
  const auto twse =
      test::MakeBasicInfo(test::BasicInfoFields{.symbol = "2330"});
  const test::BasicInfoFields tpex_fields{
      .symbol = "6488",
      .service_type = ServiceType::kOtc,
  };
  const auto tpex = test::MakeBasicInfo(tpex_fields);
  auto header = test::MakeHeader(MessageType::kStockBasicInfo, twse.size(), 1);
  (void)catalog.Process(header, twse, 0);
  header = test::MakeHeader(MessageType::kStockBasicInfo, tpex.size(), 2,
                            ServiceType::kOtc);
  (void)catalog.Process(header, tpex, 114);

  const auto records = catalog.records();

  ASSERT_EQ(records.size(), 2);
  EXPECT_EQ(records[0].market, "TPEX");
  EXPECT_EQ(records[0].symbol, "6488");
  EXPECT_EQ(records[1].market, "TWSE");
  EXPECT_EQ(records[1].symbol, "2330");
}

} // namespace
} // namespace aries::data::twse
