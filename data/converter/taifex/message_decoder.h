#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "data/converter/taifex/protocol.h"
#include "data/converter/taifex/records.h"

namespace aries::data::taifex {

struct DecoderStats {
  std::uint64_t product_basic_messages{};
  std::uint64_t contract_basic_messages{};
  std::uint64_t identical_basic_duplicates{};
  std::uint64_t ignored_messages{};
  std::uint64_t metadata_missing_messages{};
  std::uint64_t sequence_gaps{};
  std::uint64_t stale_messages{};
  std::uint64_t snapshot_recoveries{};
  std::uint64_t reset_messages{};
};

using DepthCallback = std::function<void(const DepthRecord &)>;

[[nodiscard]] MessageHeader
DecodeMessageHeader(std::span<const std::uint8_t> bytes);

[[nodiscard]] std::int64_t TradingDayStartNanoseconds(std::int32_t trading_day);

class MessageDecoder {
public:
  explicit MessageDecoder(std::int32_t trading_day);
  ~MessageDecoder();

  MessageDecoder(const MessageDecoder &) = delete;
  MessageDecoder &operator=(const MessageDecoder &) = delete;
  MessageDecoder(MessageDecoder &&) noexcept;
  MessageDecoder &operator=(MessageDecoder &&) noexcept;

  void Process(const MessageHeader &header, std::span<const std::uint8_t> body,
               const DepthCallback &emit);

  void Finalize() const;

  [[nodiscard]] std::vector<BasicInfoRecord> BasicInfoRecords() const;
  [[nodiscard]] const DecoderStats &stats() const noexcept;
  [[nodiscard]] std::size_t symbol_count() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace aries::data::taifex
