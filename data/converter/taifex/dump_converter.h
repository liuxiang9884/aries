#pragma once

#include <cstdint>
#include <filesystem>

namespace aries::data::taifex {

struct ConvertOptions {
  std::filesystem::path dump_path;
  std::filesystem::path output_path;
  std::filesystem::path basic_output_path;
  std::int32_t trading_day{};
  bool dry_run{};
  bool overwrite{};
};

struct ConvertStats {
  std::uint64_t messages_read{};
  std::uint64_t rows_written{};
  std::uint64_t symbols_seen{};
  std::uint64_t bytes_read{};
  std::uint64_t product_basic_messages{};
  std::uint64_t contract_basic_messages{};
  std::uint64_t basic_duplicates{};
  std::uint64_t basic_info_rows{};
  std::uint64_t ignored_messages{};
  std::uint64_t metadata_missing_messages{};
  std::uint64_t sequence_gaps{};
  std::uint64_t stale_messages{};
  std::uint64_t snapshot_recoveries{};
  std::uint64_t reset_messages{};
};

[[nodiscard]] ConvertStats ConvertDump(const ConvertOptions &options);

} // namespace aries::data::taifex
