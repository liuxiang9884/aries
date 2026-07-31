#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include <CLI/CLI.hpp>
#include <fmt/base.h>

#include "data/converter/twse/dump_converter.h"
#include "data/converter/twse/message_decoder.h"

int main(int argc, char **argv) {
  CLI::App app{"Convert a TWSE multicast dump to the legacy Orion CSV schema"};
  std::filesystem::path dump_path;
  std::filesystem::path output_path;
  std::int32_t trading_day = 0;
  std::string symbol_filter_mode = "stock";
  bool dry_run = false;
  bool overwrite = false;

  app.add_option("-d,--dump", dump_path, "TWSE dump file")
      ->required()
      ->check(CLI::ExistingFile);
  app.add_option("-o,--output", output_path, "Final CSV output file");
  app.add_option("--trading-day", trading_day, "Trading day in YYYYMMDD")
      ->required();
  app.add_option("--symbol-filter-mode", symbol_filter_mode,
                 "all, etf, warrant, odd_lot, or stock")
      ->check(CLI::IsMember({"all", "etf", "warrant", "odd_lot", "stock"}));
  app.add_flag("--dry-run", dry_run,
               "Decode and validate without creating a CSV");
  app.add_flag("--overwrite", overwrite,
               "Atomically replace an existing output after success");
  CLI11_PARSE(app, argc, argv);

  try {
    if (!dry_run && output_path.empty()) {
      throw std::invalid_argument("--output is required without --dry-run");
    }
    const auto mode =
        aries::data::twse::ParseSymbolFilterMode(symbol_filter_mode);
    const auto stats =
        aries::data::twse::ConvertDump(aries::data::twse::ConvertOptions{
            .dump_path = std::move(dump_path),
            .output_path = std::move(output_path),
            .trading_day = trading_day,
            .symbol_filter_mode = mode,
            .dry_run = dry_run,
            .overwrite = overwrite,
        });
    fmt::print(
        "TWSE conversion complete: mode={}, messages={}, rows={}, symbols={}, "
        "bytes={}, dry_run={}\n",
        aries::data::twse::ToString(mode), stats.messages_read,
        stats.rows_written, stats.symbols_seen, stats.bytes_read, dry_run);
    return 0;
  } catch (const std::exception &error) {
    fmt::print(stderr, "TWSE conversion failed: {}\n", error.what());
    return 1;
  }
}
