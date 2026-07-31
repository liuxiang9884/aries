#include <cstdint>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "nova/utils/log.h"
#include <CLI/CLI.hpp>

#include "data/converter/twse/dump_converter.h"
#include "data/converter/twse/message_decoder.h"

int main(int argc, char **argv) {
  nova::LogConfig log_config;
  log_config.set_file_sink_name("");
  log_config.set_console_sink_name("twse_dump_converter_console");
  nova::InitializeLogging(log_config);
  struct LoggingGuard {
    ~LoggingGuard() { nova::StopLogging(); }
  } logging_guard;

  CLI::App app{
      "Convert a TWSE multicast dump to depth and basic-info CSV files"};
  std::filesystem::path dump_path;
  std::filesystem::path output_path;
  std::filesystem::path basic_output_path;
  std::int32_t trading_day = 0;
  std::string symbol_filter_mode = "stock";
  bool dry_run = false;
  bool overwrite = false;

  app.add_option("-d,--dump", dump_path, "TWSE dump file")
      ->required()
      ->check(CLI::ExistingFile);
  app.add_option("-o,--output", output_path, "Final CSV output file");
  app.add_option("--basic-output", basic_output_path,
                 "Final basic-info CSV output file");
  app.add_option("--trading-day", trading_day, "Trading day in YYYYMMDD")
      ->required();
  app.add_option("--symbol-filter-mode", symbol_filter_mode,
                 "all, etf, warrant, odd_lot, or stock")
      ->check(CLI::IsMember({"all", "etf", "warrant", "odd_lot", "stock"}));
  app.add_flag("--dry-run", dry_run,
               "Decode and validate without creating a CSV");
  app.add_flag("--overwrite", overwrite,
               "Atomically replace an existing output after success");
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &error) {
    const auto exit_code = app.exit(error);
    if (exit_code != 0) {
      NOVA_ERROR("TWSE converter CLI parse failed: {}", error.what());
    }
    return exit_code;
  }

  try {
    if (!dry_run && output_path.empty()) {
      throw std::invalid_argument("--output is required without --dry-run");
    }
    if (!dry_run && basic_output_path.empty()) {
      throw std::invalid_argument(
          "--basic-output is required without --dry-run");
    }
    const auto mode =
        aries::data::twse::ParseSymbolFilterMode(symbol_filter_mode);
    const auto stats =
        aries::data::twse::ConvertDump(aries::data::twse::ConvertOptions{
            .dump_path = std::move(dump_path),
            .output_path = std::move(output_path),
            .basic_output_path = std::move(basic_output_path),
            .trading_day = trading_day,
            .symbol_filter_mode = mode,
            .dry_run = dry_run,
            .overwrite = overwrite,
        });
    NOVA_INFO("TWSE conversion complete: mode={} messages={} depth_rows={} "
              "depth_symbols={} basic_messages={} basic_controls={} "
              "basic_duplicates={} basic_rows={} bytes={} dry_run={}",
              aries::data::twse::ToString(mode), stats.messages_read,
              stats.rows_written, stats.symbols_seen, stats.basic_info_messages,
              stats.basic_info_controls, stats.basic_info_duplicates,
              stats.basic_info_rows, stats.bytes_read, dry_run);
    return 0;
  } catch (const std::exception &error) {
    NOVA_ERROR("TWSE conversion failed: {}", error.what());
    return 1;
  }
}
