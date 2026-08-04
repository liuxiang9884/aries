#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "data/converter/twse/basic_info.h"
#include "data/converter/twse/message_decoder.h"

namespace aries::data::twse {

struct ConvertOptions {
  std::filesystem::path dump_path;
  std::filesystem::path output_path;
  std::filesystem::path basic_output_path;
  std::int32_t trading_day{};
  SymbolFilterMode symbol_filter_mode{SymbolFilterMode::kStock};
  bool dry_run{false};
  bool overwrite{false};
};

struct FrameRecoveryIssue {
  std::uint64_t offset{};
  std::uint64_t skipped_bytes{};
  std::optional<std::uint64_t> recovered_offset;
  std::optional<std::uint8_t> service_type;
  std::optional<std::uint8_t> message_type;
  std::optional<std::uint8_t> format_version;
  std::optional<std::uint64_t> sequence;
  std::optional<std::string> affected_symbol;
  std::string error;
};

struct ConvertStats {
  std::uint64_t messages_read{};
  std::uint64_t rows_written{};
  std::uint64_t symbols_seen{};
  std::uint64_t bytes_read{};
  std::uint64_t basic_info_messages{};
  std::uint64_t basic_info_controls{};
  std::uint64_t basic_info_duplicates{};
  std::uint64_t basic_info_rows{};
  std::uint64_t source_actual_trade_payloads{};
  std::uint64_t actual_trade_payloads{};
  std::uint64_t match_groups{};
  std::uint64_t multi_trade_groups{};
  std::uint64_t trades_in_multi_groups{};
  std::uint64_t held_ended_groups{};
  std::uint64_t buy_groups{};
  std::uint64_t sell_groups{};
  std::uint64_t unknown_groups{};
  std::uint64_t missing_multiplier_messages{};
  std::uint64_t invalidated_symbol_messages{};
  std::vector<MarketSymbolIssue> missing_multiplier_symbols;
  std::vector<SequenceGapIssue> sequence_gaps;
  std::vector<IncompleteMatchGroupIssue> incomplete_match_groups;
  std::vector<ValueImputationIssue> value_imputations;
  std::vector<BasicInfoCycleMismatch> basic_info_cycle_mismatches;
  std::vector<FrameRecoveryIssue> frame_recovery_issues;

  [[nodiscard]] bool has_issues() const noexcept {
    return !basic_info_cycle_mismatches.empty() ||
           !frame_recovery_issues.empty() ||
           !missing_multiplier_symbols.empty() || !sequence_gaps.empty() ||
           !incomplete_match_groups.empty() || !value_imputations.empty();
  }
};

[[nodiscard]] ConvertStats ConvertDump(const ConvertOptions &options);

}  // namespace aries::data::twse
