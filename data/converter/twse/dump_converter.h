#pragma once

#include <cstdint>
#include <filesystem>

#include "data/converter/twse/message_decoder.h"

namespace aries::data::twse {

struct ConvertOptions {
  std::filesystem::path dump_path;
  std::filesystem::path output_path;
  std::int32_t trading_day{};
  SymbolFilterMode symbol_filter_mode{SymbolFilterMode::kStock};
  bool dry_run{false};
  bool overwrite{false};
};

struct ConvertStats {
  std::uint64_t messages_read{};
  std::uint64_t rows_written{};
  std::uint64_t symbols_seen{};
  std::uint64_t bytes_read{};
};

[[nodiscard]] ConvertStats ConvertDump(const ConvertOptions &options);

} // namespace aries::data::twse
