#pragma once

#include <filesystem>
#include <memory>
#include <span>

#include "data/converter/taifex/records.h"

namespace aries::data::taifex {

class CsvWriter {
public:
  CsvWriter(const std::filesystem::path &depth_path,
            const std::filesystem::path &basic_path, bool overwrite);
  ~CsvWriter();

  CsvWriter(const CsvWriter &) = delete;
  CsvWriter &operator=(const CsvWriter &) = delete;
  CsvWriter(CsvWriter &&) noexcept;
  CsvWriter &operator=(CsvWriter &&) noexcept;

  void WriteDepth(const DepthRecord &record);
  void Commit(std::span<const BasicInfoRecord> basic_records);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace aries::data::taifex
