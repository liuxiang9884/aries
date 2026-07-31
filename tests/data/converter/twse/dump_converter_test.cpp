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
        .trading_day = 20260707,
        .symbol_filter_mode = SymbolFilterMode::kStock,
    };
  }

  std::filesystem::path directory_;
  std::filesystem::path dump_path_;
  std::filesystem::path output_path_;
};

TEST_F(DumpConverterTest, ConvertsDumpToLegacyCsvAndPublishesAtomically) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);

  const auto stats = ConvertDump(MakeOptions());

  EXPECT_EQ(stats.messages_read, 2);
  EXPECT_EQ(stats.rows_written, 1);
  EXPECT_EQ(stats.symbols_seen, 1);
  ASSERT_TRUE(std::filesystem::exists(output_path_));

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

  for (const auto &entry : std::filesystem::directory_iterator(directory_)) {
    EXPECT_EQ(entry.path().filename().string().find(".partial."),
              std::string::npos);
  }
}

TEST_F(DumpConverterTest, DryRunDoesNotCreateOutput) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);
  auto options = MakeOptions();
  options.dry_run = true;

  const auto stats = ConvertDump(options);

  EXPECT_EQ(stats.rows_written, 1);
  EXPECT_FALSE(std::filesystem::exists(output_path_));
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
  for (const auto &entry : std::filesystem::directory_iterator(directory_)) {
    EXPECT_EQ(entry.path().filename().string().find(".partial."),
              std::string::npos);
  }
}

TEST_F(DumpConverterTest, RefusesToOverwriteByDefault) {
  const auto dump = MakeStockDump();
  test::WriteBinaryFile(dump_path_, dump);
  {
    std::ofstream existing(output_path_);
    existing << "keep\n";
  }

  EXPECT_THROW((void)ConvertDump(MakeOptions()), std::runtime_error);
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
  auto options = MakeOptions();
  options.overwrite = true;

  const auto stats = ConvertDump(options);

  EXPECT_EQ(stats.rows_written, 1);
  std::ifstream input(output_path_);
  std::string header;
  std::getline(input, header);
  EXPECT_TRUE(header.starts_with("symbol,symbol_id,exchtime"));
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
  EXPECT_FALSE(std::filesystem::exists(symlink_target));
  EXPECT_TRUE(std::filesystem::is_symlink(partial_path));
}

} // namespace
} // namespace aries::data::twse
