#include "data/converter/taifex/dump_converter.h"

#include <unistd.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "tests/data/converter/taifex/test_message_builder.h"

namespace aries::data::taifex {
namespace {

class TaifexDumpConverterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() /
                 ("aries_taifex_converter_" + std::to_string(::getpid()) + "_" +
                  std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(directory_);
    dump_path_ = directory_ / "input.dump";
    output_path_ = directory_ / "orderbook.csv";
    basic_output_path_ = directory_ / "basic.csv";
  }

  void TearDown() override {
    std::filesystem::remove_all(directory_);
  }

  ConvertOptions Options() const {
    return {.dump_path = dump_path_,
            .output_path = output_path_,
            .basic_output_path = basic_output_path_,
            .trading_day = 20260707};
  }

  std::filesystem::path directory_;
  std::filesystem::path dump_path_;
  std::filesystem::path output_path_;
  std::filesystem::path basic_output_path_;
};

TEST_F(TaifexDumpConverterTest, ConvertsAndPublishesOrderbookAndBasicCsv) {
  std::vector<std::uint8_t> dump;
  const auto kind = test::MakeI011("TXF", 200.0);
  test::AppendFrame(dump, '1', '3', 4, kind);
  const auto basic = test::MakeI010("TXFG6", 22'000'00);
  test::AppendFrame(dump, '1', '1', 9, basic, 2);
  const auto trade = test::MakeI024("TXFG6", 1, 22'100'00, 3, 3, 2, 1);
  test::AppendFrame(dump, '2', 'D', 1, trade, 3);
  const auto update = test::MakeI081(
      "TXFG6", 2, '0',
      {.type = '0', .price = 22'099'00, .volume = 7, .level = 1});
  test::AppendFrame(dump, '2', 'A', 1, update, 4);
  test::WriteBinaryFile(dump_path_, dump);

  const auto stats = ConvertDump(Options());

  EXPECT_EQ(stats.messages_read, 4);
  EXPECT_EQ(stats.rows_written, 1);
  EXPECT_EQ(stats.basic_info_rows, 1);
  std::ifstream orderbook_input(output_path_, std::ios::binary);
  const std::string orderbook((std::istreambuf_iterator<char>(orderbook_input)),
                              {});
  const std::string expected_orderbook =
      "trading_day,market,symbol,symbol_id,exchtime,localtime,reference_price,"
      "open,high,low,last_price,trade_volume,total_volume,total_value,"
      "total_buy_count,total_sell_count,"
      "ask_price1,ask_volume1,bid_price1,bid_volume1,"
      "ask_price2,ask_volume2,bid_price2,bid_volume2,"
      "ask_price3,ask_volume3,bid_price3,bid_volume3,"
      "ask_price4,ask_volume4,bid_price4,bid_volume4,"
      "ask_price5,ask_volume5,bid_price5,bid_volume5,"
      "derived_ask_price,derived_ask_volume,derived_bid_price,"
      "derived_bid_volume,match_flag,build_type,orderbook_action,sequence\n"
      "20260707,TAIFEX,TXFG6,-1,1783386000000000000,1783386000000000000,"
      "22000.000000,22100.000000,22100.000000,22100.000000,22100.000000,"
      "3,3,13260000.000000,2,1,0.000000,0,22099.000000,7,0.000000,0,"
      "0.000000,0,0.000000,0,0.000000,0,0.000000,0,0.000000,0,"
      "0.000000,0,0.000000,0,0.000000,0,0.000000,0,0,0,1,2\n";
  EXPECT_EQ(orderbook, expected_orderbook);

  std::ifstream basic_input(basic_output_path_, std::ios::binary);
  const std::string basic_csv((std::istreambuf_iterator<char>(basic_input)),
                              {});
  const std::string expected_basic =
      "trading_day,market,symbol,kind_id,is_spread,basic_source,contract_type,"
      "contract_subtype,reference_price,decimal_locator,strike_decimal_locator,"
      "listing_date,delisting_date,delivery_date,flow_group,dynamic_banding,"
      "multiplier,currency_code,currency,stock_id,contract_status,quote_flag,"
      "block_trade_flag,expiry_type,underlying_type,close_group,end_session\n"
      "20260707,TAIFEX,TXFG6,TXF,0,I010+I011,I,I,22000.000000,2,0,"
      "2026-01-01,2026-12-31,2026-12-31,1,Y,200.0000,1,TWD,,N,Y,Y,S, ,1,0\n";
  EXPECT_EQ(basic_csv, expected_basic);
}

TEST_F(TaifexDumpConverterTest,
       StopsAfterDaySessionWithoutProcessingLaterFrames) {
  std::vector<std::uint8_t> dump;
  const auto kind = test::MakeI011("TXF", 200.0);
  test::AppendFrame(dump, '1', '3', 4, kind);
  const auto basic = test::MakeI010("TXFG6", 22'000'00);
  test::AppendFrame(dump, '1', '1', 9, basic, 2);
  const auto update = test::MakeI081(
      "TXFG6", 1, '0',
      {.type = '0', .price = 22'099'00, .volume = 7, .level = 1});
  test::AppendFrame(dump, '2', 'A', 1, update, 3, 134'559'999'999ULL);
  const auto after_hours_basic = test::MakeI010("TXFG6", 23'000'00);
  test::AppendFrame(dump, '1', '1', 9, after_hours_basic, 4,
                    144'000'000'000ULL);
  dump.insert(dump.end(), {0x1B, 0x31, 0x31});
  test::WriteBinaryFile(dump_path_, dump);

  const auto stats = ConvertDump(Options());

  EXPECT_EQ(stats.messages_read, 3);
  EXPECT_EQ(stats.rows_written, 1);
  EXPECT_EQ(stats.basic_info_rows, 1);
  EXPECT_TRUE(stats.day_session_cutoff_reached);
  EXPECT_EQ(stats.day_session_cutoff_offset, stats.bytes_read);
  EXPECT_LT(stats.bytes_read, dump.size());
  std::ifstream basic_input(basic_output_path_, std::ios::binary);
  const std::string basic_csv((std::istreambuf_iterator<char>(basic_input)),
                              {});
  EXPECT_NE(basic_csv.find(",22000.000000,"), std::string::npos);
  EXPECT_EQ(basic_csv.find(",23000.000000,"), std::string::npos);
}

TEST_F(TaifexDumpConverterTest, InvalidChecksumKeepsExistingOutputs) {
  std::vector<std::uint8_t> dump;
  const auto kind = test::MakeI011("TXF", 200.0);
  test::AppendFrame(dump, '1', '3', 4, kind);
  dump[dump.size() - 3] ^= 0x01;
  test::WriteBinaryFile(dump_path_, dump);
  {
    std::ofstream output(output_path_);
    output << "old-orderbook\n";
  }
  {
    std::ofstream output(basic_output_path_);
    output << "old-basic\n";
  }
  auto options = Options();
  options.overwrite = true;

  EXPECT_THROW((void)ConvertDump(options), std::runtime_error);

  std::ifstream orderbook_input(output_path_);
  std::string orderbook;
  std::getline(orderbook_input, orderbook);
  EXPECT_EQ(orderbook, "old-orderbook");
  std::ifstream basic_input(basic_output_path_);
  std::string basic;
  std::getline(basic_input, basic);
  EXPECT_EQ(basic, "old-basic");
}

TEST_F(TaifexDumpConverterTest, DryRunValidatesWithoutCreatingCsv) {
  std::vector<std::uint8_t> dump;
  const auto kind = test::MakeI011("TXF", 200.0);
  test::AppendFrame(dump, '1', '3', 4, kind);
  test::WriteBinaryFile(dump_path_, dump);
  auto options = Options();
  options.dry_run = true;

  const auto stats = ConvertDump(options);

  EXPECT_EQ(stats.messages_read, 1);
  EXPECT_FALSE(std::filesystem::exists(output_path_));
  EXPECT_FALSE(std::filesystem::exists(basic_output_path_));
}

TEST_F(TaifexDumpConverterTest, RejectsTruncatedTrailerWithoutPublishing) {
  std::vector<std::uint8_t> dump;
  const auto kind = test::MakeI011("TXF", 200.0);
  test::AppendFrame(dump, '1', '3', 4, kind);
  dump.pop_back();
  test::WriteBinaryFile(dump_path_, dump);

  EXPECT_THROW((void)ConvertDump(Options()), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(output_path_));
  EXPECT_FALSE(std::filesystem::exists(basic_output_path_));
}

TEST_F(TaifexDumpConverterTest, PublishesOutputsWithUnresolvedGapSummary) {
  std::vector<std::uint8_t> dump;
  const auto kind = test::MakeI011("TXF", 200.0);
  test::AppendFrame(dump, '1', '3', 4, kind);
  const auto basic = test::MakeI010("TXFG6", 22'000'00);
  test::AppendFrame(dump, '1', '1', 9, basic, 2);
  const auto update = test::MakeI081(
      "TXFG6", 2, '0',
      {.type = '0', .price = 22'000'00, .volume = 7, .level = 1});
  test::AppendFrame(dump, '2', 'A', 1, update, 3);
  test::WriteBinaryFile(dump_path_, dump);

  const auto stats = ConvertDump(Options());

  EXPECT_TRUE(std::filesystem::exists(output_path_));
  EXPECT_TRUE(std::filesystem::exists(basic_output_path_));
  EXPECT_EQ(stats.unresolved_sequence_gaps, 1);
  ASSERT_EQ(stats.issues.size(), 1);
  EXPECT_EQ(stats.issues.front().symbol, "TXFG6");
  EXPECT_FALSE(stats.issues.front().recovered);
}

TEST_F(TaifexDumpConverterTest, PublishesOutputsWhenMetadataNeverArrives) {
  std::vector<std::uint8_t> dump;
  const auto update = test::MakeI081(
      "TXFG6", 1, '0',
      {.type = '0', .price = 22'000'00, .volume = 7, .level = 1});
  test::AppendFrame(dump, '2', 'A', 1, update);
  test::WriteBinaryFile(dump_path_, dump);

  const auto stats = ConvertDump(Options());

  EXPECT_TRUE(std::filesystem::exists(output_path_));
  EXPECT_TRUE(std::filesystem::exists(basic_output_path_));
  EXPECT_EQ(stats.rows_written, 0);
  EXPECT_EQ(stats.basic_info_rows, 0);
  EXPECT_EQ(stats.metadata_missing_messages, 1);
  ASSERT_EQ(stats.issues.size(), 1);
  EXPECT_EQ(stats.issues.front().kind, IssueKind::kMetadataMissing);
  EXPECT_EQ(stats.issues.front().symbol, "TXFG6");
}

}  // namespace
}  // namespace aries::data::taifex
