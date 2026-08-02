#pragma once

#include <filesystem>
#include <memory>

#include "data/converter/twse/basic_info.h"
#include "data/converter/twse/orderbook.h"

namespace aries::data::twse {

class OrderbookCsvWriter {
public:
  OrderbookCsvWriter(const std::filesystem::path &output_path, bool overwrite);
  ~OrderbookCsvWriter();

  OrderbookCsvWriter(const OrderbookCsvWriter &) = delete;
  OrderbookCsvWriter &operator=(const OrderbookCsvWriter &) = delete;
  OrderbookCsvWriter(OrderbookCsvWriter &&) noexcept;
  OrderbookCsvWriter &operator=(OrderbookCsvWriter &&) noexcept;

  void Write(const Orderbook<5> &record);

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
  static void Commit(OrderbookCsvWriter &orderbook_writer,
                     BasicInfoCsvWriter &basic_info_writer);
};

} // namespace aries::data::twse
