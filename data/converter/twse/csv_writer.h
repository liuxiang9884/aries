#pragma once

#include <filesystem>
#include <memory>

#include "data/converter/twse/depth_record.h"

namespace aries::data::twse {

class LegacyCsvWriter {
public:
  LegacyCsvWriter(const std::filesystem::path &output_path, bool overwrite);
  ~LegacyCsvWriter();

  LegacyCsvWriter(const LegacyCsvWriter &) = delete;
  LegacyCsvWriter &operator=(const LegacyCsvWriter &) = delete;
  LegacyCsvWriter(LegacyCsvWriter &&) noexcept;
  LegacyCsvWriter &operator=(LegacyCsvWriter &&) noexcept;

  void Write(const DepthRecord &record);
  void Commit();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace aries::data::twse
