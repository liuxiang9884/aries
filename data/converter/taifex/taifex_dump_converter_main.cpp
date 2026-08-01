#include <cstdint>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include <CLI/CLI.hpp>

#include "nova/utils/log.h"

#include "data/converter/taifex/dump_converter.h"

int main(int argc, char **argv) {
  nova::LogConfig log_config;
  log_config.set_file_sink_name("");
  log_config.set_console_sink_name("taifex_dump_converter_console");
  nova::InitializeLogging(log_config);
  struct LoggingGuard {
    ~LoggingGuard() { nova::StopLogging(); }
  } logging_guard;

  CLI::App app{
      "Convert a TAIFEX futures dump to orderbook and basic-info CSV files"};
  std::filesystem::path dump_path;
  std::filesystem::path output_path;
  std::filesystem::path basic_output_path;
  std::int32_t trading_day = 0;
  bool dry_run = false;
  bool overwrite = false;

  app.add_option("-d,--dump", dump_path, "TAIFEX uncompressed dump file")
      ->required()
      ->check(CLI::ExistingFile);
  app.add_option("-o,--output", output_path, "Final orderbook CSV output file");
  app.add_option("--basic-output", basic_output_path,
                 "Final basic-info CSV output file");
  app.add_option("--trading-day", trading_day, "Trading day in YYYYMMDD")
      ->required();
  app.add_flag("--dry-run", dry_run,
               "Decode and validate without creating CSV files");
  app.add_flag("--overwrite", overwrite,
               "Atomically replace existing outputs after success");
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &error) {
    const auto exit_code = app.exit(error);
    if (exit_code != 0) {
      NOVA_ERROR("TAIFEX converter CLI parse failed: {}", error.what());
    }
    return exit_code;
  }

  try {
    const auto stats = aries::data::taifex::ConvertDump(
        {.dump_path = std::move(dump_path),
         .output_path = std::move(output_path),
         .basic_output_path = std::move(basic_output_path),
         .trading_day = trading_day,
         .dry_run = dry_run,
         .overwrite = overwrite});
    NOVA_INFO(
        "TAIFEX conversion complete: messages={} orderbook_rows={} symbols={} "
        "i010={} i011={} i012={} basic_duplicates={} i012_duplicates={} "
        "i012_conflicts={} basic_rows={} ignored={} metadata_missing={} "
        "sequence_gaps={} unresolved_gaps={} stale={} recoveries={} "
        "cache_overflows={} resets={} bytes={} day_cutoff_reached={} "
        "day_cutoff_offset={} dry_run={}",
        stats.messages_read, stats.rows_written, stats.symbols_seen,
        stats.product_basic_messages, stats.contract_basic_messages,
        stats.price_limit_messages, stats.basic_duplicates,
        stats.price_limit_duplicates, stats.price_limit_conflicts,
        stats.basic_info_rows, stats.ignored_messages,
        stats.metadata_missing_messages, stats.sequence_gaps,
        stats.unresolved_sequence_gaps, stats.stale_messages,
        stats.snapshot_recoveries, stats.gap_cache_overflows,
        stats.reset_messages, stats.bytes_read,
        stats.day_session_cutoff_reached, stats.day_session_cutoff_offset,
        dry_run);
    for (const auto &count : stats.ignored_message_counts) {
      NOVA_INFO(
          "TAIFEX ignored message summary: trading_day={} transmission={} "
          "kind={} count={}",
          trading_day, count.transmission_code, count.message_kind,
          count.count);
    }
    for (const auto &issue : stats.issues) {
      NOVA_WARNING(
          "TAIFEX conversion issue: trading_day={} type={} symbol={} "
          "transmission={} kind={} expected_sequence={} actual_sequence={} "
          "recovered={} recovery_sequence={}",
          trading_day, aries::data::taifex::ToString(issue.kind), issue.symbol,
          issue.transmission_code, issue.message_kind, issue.expected_sequence,
          issue.actual_sequence, issue.recovered, issue.recovery_sequence);
    }
    return 0;
  } catch (const std::exception &error) {
    NOVA_ERROR("TAIFEX conversion failed: {}", error.what());
    return 1;
  }
}
