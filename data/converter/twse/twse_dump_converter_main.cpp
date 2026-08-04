#include <cstdint>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <CLI/CLI.hpp>

#include "data/converter/twse/dump_converter.h"
#include "data/converter/twse/message_decoder.h"
#include "nova/utils/log.h"

int main(int argc, char **argv) {
  nova::LogConfig log_config;
  log_config.set_file_sink_name("");
  log_config.set_console_sink_name("twse_dump_converter_console");
  nova::InitializeLogging(log_config);
  struct LoggingGuard {
    ~LoggingGuard() {
      nova::StopLogging();
    }
  } logging_guard;

  CLI::App app{
      "Convert a TWSE multicast dump to orderbook and basic-info CSV files"};
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
    for (const auto &issue : stats.basic_info_cycle_mismatches) {
      NOVA_WARNING(
          "TWSE conversion issue: kind=format1_cycle_mismatch "
          "trading_day={} service={} control={} expected={} actual={} "
          "missing={} offset={} sequence={}",
          trading_day, static_cast<unsigned>(issue.service_type),
          issue.control_kind == aries::data::twse::BasicInfoControlKind::kAll
              ? "AL"
              : "NE",
          issue.expected, issue.actual,
          issue.expected > issue.actual ? issue.expected - issue.actual : 0,
          issue.offset, issue.sequence);
    }
    for (const auto &issue : stats.frame_recovery_issues) {
      NOVA_WARNING(
          "TWSE conversion issue: kind=frame_corruption trading_day={} "
          "offset={} skipped_bytes={} recovered_offset={} service={} "
          "format={} version={} sequence={} affected_symbol={} "
          "error={}",
          trading_day, issue.offset, issue.skipped_bytes,
          issue.recovered_offset.has_value()
              ? std::to_string(*issue.recovered_offset)
              : "none",
          issue.service_type.has_value() ? std::to_string(*issue.service_type)
                                         : "unknown",
          issue.message_type.has_value() ? std::to_string(*issue.message_type)
                                         : "unknown",
          issue.format_version.has_value()
              ? std::to_string(*issue.format_version)
              : "unknown",
          issue.sequence.has_value() ? std::to_string(*issue.sequence)
                                     : "unknown",
          issue.affected_symbol.value_or("unknown"), issue.error);
    }
    for (const auto &issue : stats.missing_multiplier_symbols) {
      NOVA_WARNING(
          "TWSE conversion issue: kind=missing_multiplier "
          "trading_day={} market={} symbol={}",
          trading_day, static_cast<unsigned>(issue.market), issue.symbol);
    }
    for (const auto &issue : stats.sequence_gaps) {
      NOVA_WARNING(
          "TWSE conversion issue: kind=sequence_gap trading_day={} "
          "market={} format={} expected_sequence={} actual_sequence={}",
          trading_day, static_cast<unsigned>(issue.market),
          static_cast<unsigned>(issue.source_format), issue.expected_sequence,
          issue.actual_sequence);
    }
    for (const auto &issue : stats.incomplete_match_groups) {
      NOVA_WARNING(
          "TWSE conversion issue: kind=incomplete_match_group "
          "trading_day={} market={} symbol={} format={} first_sequence={} "
          "last_sequence={} trade_count={} trade_volume={} reason={}",
          issue.trading_day, static_cast<unsigned>(issue.market), issue.symbol,
          static_cast<unsigned>(issue.source_format), issue.first_sequence,
          issue.last_sequence, issue.trade_count, issue.trade_volume,
          aries::data::twse::ToString(issue.reason));
    }
    for (const auto &issue : stats.value_imputations) {
      NOVA_WARNING(
          "TWSE conversion issue: kind=missing_trade_volume "
          "trading_day={} market={} symbol={} sequence={} volume_diff={} "
          "observed_volume={} missing_volume={} imputation_price={:.4f}",
          trading_day, static_cast<unsigned>(issue.market), issue.symbol,
          issue.source_sequence, issue.volume_difference, issue.observed_volume,
          issue.missing_volume, issue.price);
    }
    const std::string_view result_status =
        dry_run
            ? (stats.has_issues() ? std::string_view{"validated_with_issues"}
                                  : std::string_view{"validated"})
            : (stats.has_issues() ? std::string_view{"published_partial"}
                                  : std::string_view{"published_complete"});
    NOVA_INFO(
        "TWSE conversion complete: status={} schema=twse-orderbook-v2 "
        "local_time_source=exchange_fallback mode={} messages={} "
        "orderbook_rows={} source_actual_trades={} actual_trades={} "
        "match_groups={} multi_trade_groups={} trades_in_multi_groups={} "
        "held_ended_groups={} "
        "buy_groups={} sell_groups={} unknown_groups={} "
        "orderbook_symbols={} basic_messages={} basic_controls={} "
        "basic_duplicates={} basic_rows={} cycle_mismatches={} "
        "frame_errors={} sequence_gaps={} incomplete_groups={} "
        "value_imputations={} missing_multiplier_messages={} "
        "invalidated_symbol_messages={} bytes={} dry_run={}",
        result_status, aries::data::twse::ToString(mode), stats.messages_read,
        stats.rows_written, stats.source_actual_trade_payloads,
        stats.actual_trade_payloads, stats.match_groups,
        stats.multi_trade_groups, stats.trades_in_multi_groups,
        stats.held_ended_groups, stats.buy_groups, stats.sell_groups,
        stats.unknown_groups, stats.symbols_seen, stats.basic_info_messages,
        stats.basic_info_controls, stats.basic_info_duplicates,
        stats.basic_info_rows, stats.basic_info_cycle_mismatches.size(),
        stats.frame_recovery_issues.size(), stats.sequence_gaps.size(),
        stats.incomplete_match_groups.size(), stats.value_imputations.size(),
        stats.missing_multiplier_messages, stats.invalidated_symbol_messages,
        stats.bytes_read, dry_run);
    return 0;
  } catch (const std::exception &error) {
    NOVA_ERROR("TWSE conversion failed: {}", error.what());
    return 1;
  }
}
