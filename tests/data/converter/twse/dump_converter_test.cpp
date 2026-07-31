#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include "data/converter/twse/dump_converter.h"
#include "tests/data/converter/twse/test_message_builder.h"

namespace aries::data::twse {
namespace {

using test::Level;

class DumpConverterTest : public ::testing::Test {
protected:
  void SetUp() override {
    directory_ = std::filesystem::temp_directory_path() /
                 ("aries_twse_converter_" + std::to_string(::getpid()) + "_" +
                  std::to_string(reinterpret_cast<std::uintptr_t>(this)));
    std::filesystem::create_directories(directory_);
    dump_path_ = directory_ / "input.dump";
    output_path_ = directory_ / "output.csv";
    basic_output_path_ = directory_ / "basic_info.csv";
  }

  void TearDown() override { std::filesystem::remove_all(directory_); }

  std::vector<std::uint8_t> MakeStockDump() const {
    std::vector<std::uint8_t> dump;
    const auto basic = test::MakeStockBasic("2330", 900000, 990000, 810000);
    test::AppendMessage(dump, MessageType::kStockBasicInfo, basic);

    constexpr std::array<Level, 3> kLevels{{
        {.price = 950000, .volume = 10},
        {.price = 949000, .volume = 20},
        {.price = 951000, .volume = 30},
    }};
    const auto depth =
        test::MakeDepth("2330", 90000123456, 100, true, 1, 1, kLevels, 0x08);
    test::AppendMessage(dump, MessageType::kStockDepthV, depth, 123);
    return dump;
  }

  ConvertOptions MakeOptions() const {
    return ConvertOptions{
        .dump_path = dump_path_,
        .output_path = output_path_,
        .basic_output_path = basic_output_path_,
        .trading_day = 20260707,
        .symbol_filter_mode = SymbolFilterMode::kStock,
    };
  }

  std::filesystem::path directory_;
  std::filesystem::path dump_path_;
  std::filesystem::path output_path_;
  std::filesystem::path basic_output_path_;
};

TEST_F(DumpConverterTest, ConvertsDumpToLegacyCsvAndPublishesAtomically) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);

  const auto stats = ConvertDump(MakeOptions());

  EXPECT_EQ(stats.messages_read, 2);
  EXPECT_EQ(stats.rows_written, 1);
  EXPECT_EQ(stats.symbols_seen, 1);
  EXPECT_EQ(stats.basic_info_messages, 1);
  EXPECT_EQ(stats.basic_info_rows, 1);
  ASSERT_TRUE(std::filesystem::exists(output_path_));
  ASSERT_TRUE(std::filesystem::exists(basic_output_path_));

  std::ifstream input(output_path_, std::ios::binary);
  const std::string csv((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
  const std::string expected =
      "symbol,symbol_id,exchtime,localtime,high_limit,low_limit,last_price,"
      "ask_price1,bid_price1,ask_price2,bid_price2,ask_price3,bid_price3,"
      "ask_price4,bid_price4,ask_price5,bid_price5,open,total_trade,"
      "total_volume,total_value,status,sequence\n"
      "2330,-1,1783386000123456000,1783386000123456000,99.00,81.00,95.00,"
      "95.10,94.90,0.00,0.00,0.00,0.00,0.00,0.00,0.00,0.00,95.00,10,"
      "100,9500.00,524434,123\n";
  EXPECT_EQ(csv, expected);

  std::ifstream basic_input(basic_output_path_, std::ios::binary);
  const std::string basic_csv((std::istreambuf_iterator<char>(basic_input)),
                              std::istreambuf_iterator<char>());
  const std::string basic_expected =
      "trading_day,market,symbol,industry_code,security_type,anomaly_code,"
      "stock_group_code,board_code,reference_price,high_limit,low_limit,"
      "abnormal_recommendation,special_abnormal,day_trading_code,"
      "margin_short_exempt,borrow_short_exempt,matching_cycle_seconds,"
      "warrant_flag,strike_price,previous_exercise_volume,"
      "previous_cancellation_volume,outstanding_volume,exercise_ratio,"
      "warrant_upper_price,warrant_lower_price,maturity_date,"
      "foreign_stock_flag,multiplier,currency,market_data_line\n"
      "20260707,TWSE,2330,01,00,0,,0,90.0000,99.0000,81.0000,,,,,,0,"
      ",,,,,,,,,,1000,TWD,1\n";
  EXPECT_EQ(basic_csv, basic_expected);

  for (const auto &entry : std::filesystem::directory_iterator(directory_)) {
    EXPECT_EQ(entry.path().filename().string().find(".partial."),
              std::string::npos);
  }
}

TEST_F(DumpConverterTest, ConvertsTpexMessagesWithOtcBasicInfoLayout) {
  std::vector<std::uint8_t> dump;
  const auto basic =
      test::MakeStockBasic("6488", 500000, 550000, 450000, ServiceType::kOtc);
  test::AppendMessage(dump, MessageType::kStockBasicInfo, basic, 1,
                      ServiceType::kOtc);
  constexpr std::array<Level, 1> kLevels{{
      {.price = 510000, .volume = 3},
  }};
  const auto depth =
      test::MakeDepth("6488", 90000000000, 3, true, 0, 0, kLevels);
  test::AppendMessage(dump, MessageType::kStockDepthV, depth, 2,
                      ServiceType::kOtc);
  test::WriteBinaryFile(dump_path_, dump);

  const auto stats = ConvertDump(MakeOptions());

  EXPECT_EQ(stats.messages_read, 2);
  EXPECT_EQ(stats.rows_written, 1);
  EXPECT_EQ(stats.basic_info_rows, 1);
  std::ifstream input(output_path_, std::ios::binary);
  const std::string csv((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
  EXPECT_NE(csv.find("6488,-1,1783386000000000000,1783386000000000000,"
                     "55.00,45.00,51.00"),
            std::string::npos);
}

TEST_F(DumpConverterTest, WritesWarrantBasicInfoWithStableUnitsAndFormatting) {
  const test::BasicInfoFields fields{
      .symbol = "067288",
      .industry_code = "00",
      .security_type = "W2",
      .anomaly_code = 3,
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
      .currency = "   ",
      .market_data_line = 2,
  };
  std::vector<std::uint8_t> dump;
  const auto basic = test::MakeBasicInfo(fields);
  test::AppendMessage(dump, MessageType::kStockBasicInfo, basic, 1);
  test::WriteBinaryFile(dump_path_, dump);

  const auto stats = ConvertDump(MakeOptions());

  EXPECT_EQ(stats.rows_written, 0);
  EXPECT_EQ(stats.basic_info_rows, 1);
  std::ifstream input(basic_output_path_, std::ios::binary);
  const std::string csv((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
  EXPECT_NE(csv.find("20260707,TWSE,067288,00,W2,3,,0,1.6600,2.5900,0.7300,"
                     "Y,N,Y,S,B,20,Y,7.9275,12000,3000,2000000,0.01400,3.0000,"
                     "0.0100,2026-12-17,N,1000,TWD,2\n"),
            std::string::npos);
}

TEST_F(DumpConverterTest, RequiresTwoDistinctOutputPaths) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);
  auto options = MakeOptions();
  options.basic_output_path.clear();
  EXPECT_THROW((void)ConvertDump(options), std::invalid_argument);

  options = MakeOptions();
  options.basic_output_path = options.output_path;
  EXPECT_THROW((void)ConvertDump(options), std::invalid_argument);
  EXPECT_FALSE(std::filesystem::exists(output_path_));
}

TEST_F(DumpConverterTest, DryRunDoesNotCreateOutput) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);
  auto options = MakeOptions();
  options.dry_run = true;

  const auto stats = ConvertDump(options);

  EXPECT_EQ(stats.rows_written, 1);
  EXPECT_FALSE(std::filesystem::exists(output_path_));
  EXPECT_FALSE(std::filesystem::exists(basic_output_path_));
}

TEST_F(DumpConverterTest, IgnoresEndOfStreamControlSymbolInAllMode) {
  constexpr std::array<Level, 0> kNoLevels{};
  const auto sentinel =
      test::MakeDepth("000000", 999999999999, 0, false, 0, 0, kNoLevels);
  std::vector<std::uint8_t> dump;
  test::AppendMessage(dump, MessageType::kStockDepthV, sentinel, 19905605);
  test::WriteBinaryFile(dump_path_, dump);
  auto options = MakeOptions();
  options.symbol_filter_mode = SymbolFilterMode::kAll;
  options.dry_run = true;

  const auto stats = ConvertDump(options);

  EXPECT_EQ(stats.messages_read, 1);
  EXPECT_EQ(stats.rows_written, 0);
  EXPECT_EQ(stats.symbols_seen, 0);
}

TEST_F(DumpConverterTest, TruncatedDumpDoesNotPublishOutputOrLeavePartial) {
  auto dump = MakeStockDump();
  dump.pop_back();
  test::WriteBinaryFile(dump_path_, dump);

  EXPECT_THROW((void)ConvertDump(MakeOptions()), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(output_path_));
  EXPECT_FALSE(std::filesystem::exists(basic_output_path_));
  for (const auto &entry : std::filesystem::directory_iterator(directory_)) {
    EXPECT_EQ(entry.path().filename().string().find(".partial."),
              std::string::npos);
  }
}

TEST_F(DumpConverterTest, RejectsInvalidMessageChecksum) {
  auto dump = MakeStockDump();
  dump[dump.size() - protocol::kMessageTrailerSize] ^= 0xFFU;
  test::WriteBinaryFile(dump_path_, dump);

  EXPECT_THROW((void)ConvertDump(MakeOptions()), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(output_path_));
  EXPECT_FALSE(std::filesystem::exists(basic_output_path_));
}

TEST_F(DumpConverterTest, RefusesToOverwriteByDefault) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);
  {
    std::ofstream existing(output_path_);
    existing << "keep\n";
  }

  EXPECT_THROW((void)ConvertDump(MakeOptions()), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(basic_output_path_));
  std::ifstream input(output_path_);
  std::string content;
  std::getline(input, content);
  EXPECT_EQ(content, "keep");
}

TEST_F(DumpConverterTest, OverwritesOnlyWhenExplicitlyRequested) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);
  {
    std::ofstream existing(output_path_);
    existing << "old\n";
  }
  {
    std::ofstream existing(basic_output_path_);
    existing << "old-basic\n";
  }
  auto options = MakeOptions();
  options.overwrite = true;

  const auto stats = ConvertDump(options);

  EXPECT_EQ(stats.rows_written, 1);
  std::ifstream input(output_path_);
  std::string header;
  std::getline(input, header);
  EXPECT_TRUE(header.starts_with("symbol,symbol_id,exchtime"));
  std::ifstream basic_input(basic_output_path_);
  std::string basic_header;
  std::getline(basic_input, basic_header);
  EXPECT_TRUE(basic_header.starts_with("trading_day,market,symbol"));
}

TEST_F(DumpConverterTest, RefusesDanglingPartialSymlink) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);
  auto partial_path = output_path_;
  partial_path += ".partial." + std::to_string(::getpid());
  const auto symlink_target = directory_ / "outside.csv";
  std::filesystem::create_symlink(symlink_target, partial_path);

  EXPECT_THROW((void)ConvertDump(MakeOptions()), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(output_path_));
  EXPECT_FALSE(std::filesystem::exists(basic_output_path_));
  EXPECT_FALSE(std::filesystem::exists(symlink_target));
  EXPECT_TRUE(std::filesystem::is_symlink(partial_path));
}

TEST_F(DumpConverterTest, RefusesDanglingBasicInfoPartialSymlink) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);
  auto partial_path = basic_output_path_;
  partial_path += ".partial." + std::to_string(::getpid());
  const auto symlink_target = directory_ / "outside-basic.csv";
  std::filesystem::create_symlink(symlink_target, partial_path);

  EXPECT_THROW((void)ConvertDump(MakeOptions()), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(output_path_));
  EXPECT_FALSE(std::filesystem::exists(basic_output_path_));
  EXPECT_FALSE(std::filesystem::exists(symlink_target));
  EXPECT_TRUE(std::filesystem::is_symlink(partial_path));
  auto depth_partial_path = output_path_;
  depth_partial_path += ".partial." + std::to_string(::getpid());
  EXPECT_FALSE(std::filesystem::exists(depth_partial_path));
}

TEST_F(DumpConverterTest, OddLotModeDoesNotCreateStateFromFormat1) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);
  auto options = MakeOptions();
  options.symbol_filter_mode = SymbolFilterMode::kOddLot;
  options.dry_run = true;

  const auto stats = ConvertDump(options);

  EXPECT_EQ(stats.basic_info_rows, 1);
  EXPECT_EQ(stats.symbols_seen, 0);
  EXPECT_EQ(stats.rows_written, 0);
}

TEST_F(DumpConverterTest, ChangedBasicInfoKeepsBothExistingOutputs) {
  std::vector<std::uint8_t> dump;
  auto basic = test::MakeStockBasic("2330", 900000, 990000, 810000);
  test::AppendMessage(dump, MessageType::kStockBasicInfo, basic, 1);
  basic = test::MakeStockBasic("2330", 910000, 990000, 810000);
  test::AppendMessage(dump, MessageType::kStockBasicInfo, basic, 2);
  test::WriteBinaryFile(dump_path_, dump);
  {
    std::ofstream existing(output_path_);
    existing << "old-depth\n";
  }
  {
    std::ofstream existing(basic_output_path_);
    existing << "old-basic\n";
  }
  auto options = MakeOptions();
  options.overwrite = true;

  EXPECT_THROW((void)ConvertDump(options), std::runtime_error);

  std::ifstream depth_input(output_path_);
  std::string depth_content;
  std::getline(depth_input, depth_content);
  EXPECT_EQ(depth_content, "old-depth");
  std::ifstream basic_input(basic_output_path_);
  std::string basic_content;
  std::getline(basic_input, basic_content);
  EXPECT_EQ(basic_content, "old-basic");
}

TEST_F(DumpConverterTest, NeverReplacesAnOutputDirectory) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);
  std::filesystem::create_directory(basic_output_path_);
  auto options = MakeOptions();
  options.overwrite = true;

  EXPECT_THROW((void)ConvertDump(options), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(output_path_));
  EXPECT_TRUE(std::filesystem::is_directory(basic_output_path_));
  for (const auto &entry : std::filesystem::directory_iterator(directory_)) {
    EXPECT_EQ(entry.path().filename().string().find(".partial."),
              std::string::npos);
  }
}

} // namespace
} // namespace aries::data::twse
