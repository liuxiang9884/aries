#pragma once

#include <filesystem>
#include <memory>

#include "data/converter/twse/basic_info.h"
#include "data/converter/twse/depth_record.h"

namespace aries::data::twse {

class DepthCsvWriter {
public:
  DepthCsvWriter(const std::filesystem::path &output_path, bool overwrite);
  ~DepthCsvWriter();

  DepthCsvWriter(const DepthCsvWriter &) = delete;
  DepthCsvWriter &operator=(const DepthCsvWriter &) = delete;
  DepthCsvWriter(DepthCsvWriter &&) noexcept;
  DepthCsvWriter &operator=(DepthCsvWriter &&) noexcept;

  void Write(const DepthRecord &record);

private:
  friend class CsvOutputTransaction;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class BasicInfoCsvWriter {
public:
  BasicInfoCsvWriter(const std::filesystem::path &output_path, bool overwrite);
  ~BasicInfoCsvWriter();

  BasicInfoCsvWriter(const BasicInfoCsvWriter &) = delete;
  BasicInfoCsvWriter &operator=(const BasicInfoCsvWriter &) = delete;
  BasicInfoCsvWriter(BasicInfoCsvWriter &&) noexcept;
  BasicInfoCsvWriter &operator=(BasicInfoCsvWriter &&) noexcept;

  void Write(const BasicInfoRecord &record);

private:
  friend class CsvOutputTransaction;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class CsvOutputTransaction {
public:
  static void Commit(DepthCsvWriter &depth_writer,
                     BasicInfoCsvWriter &basic_info_writer);
};

} // namespace aries::data::twse
