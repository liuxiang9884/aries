#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include "data/converter/taifex/dump_converter.h"
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

  void TearDown() override { std::filesystem::remove_all(directory_); }

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
  EXPECT_NE(orderbook.find("trading_day,market,symbol,symbol_id"),
            std::string::npos);
  EXPECT_NE(orderbook.find("20260707,TAIFEX,TXFG6,-1,"), std::string::npos);
  EXPECT_NE(orderbook.find(",22100.000000,3,3,13260000.000000,2,1,"),
            std::string::npos);
  EXPECT_EQ(orderbook.find("continuous_flag"), std::string::npos);
  const auto header_end = orderbook.find('\n');
  ASSERT_NE(header_end, std::string::npos);
  EXPECT_EQ(std::count(orderbook.begin(), orderbook.begin() + header_end, ','),
            43);

  std::ifstream basic_input(basic_output_path_, std::ios::binary);
  const std::string basic_csv((std::istreambuf_iterator<char>(basic_input)),
                              {});
  EXPECT_NE(basic_csv.find("trading_day,market,symbol,kind_id,is_spread"),
            std::string::npos);
  EXPECT_NE(basic_csv.find("20260707,TAIFEX,TXFG6,TXF,0,I010+I011"),
            std::string::npos);
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

} // namespace
} // namespace aries::data::taifex
