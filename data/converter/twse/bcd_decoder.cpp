#include "data/converter/twse/bcd_decoder.h"

#include <limits>
#include <stdexcept>

namespace aries::data::twse {
namespace {

std::uint8_t DecodeNibble(std::uint8_t value) {
  if (value > 9) {
    throw std::invalid_argument("invalid BCD digit");
  }
  return value;
}

} // namespace

std::uint64_t DecodeBcdInteger(std::span<const std::uint8_t> bytes) {
  std::uint64_t result = 0;
  for (const auto byte : bytes) {
    for (const auto digit :
         {DecodeNibble(static_cast<std::uint8_t>(byte >> 4U)),
          DecodeNibble(static_cast<std::uint8_t>(byte & 0x0FU))}) {
      if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
        throw std::overflow_error("BCD integer overflow");
      }
      result = result * 10U + digit;
    }
  }
  return result;
}

double DecodeBcdDecimal(std::span<const std::uint8_t> bytes,
                        std::size_t decimal_digits) {
  if (decimal_digits > bytes.size() * 2U) {
    throw std::invalid_argument("BCD decimal scale exceeds field width");
  }

  std::uint64_t scale = 1;
  for (std::size_t i = 0; i < decimal_digits; ++i) {
    scale *= 10U;
  }
  return static_cast<double>(DecodeBcdInteger(bytes)) /
         static_cast<double>(scale);
}

std::int64_t DecodeBcdTimeNanoseconds(std::span<const std::uint8_t> bytes) {
  if (bytes.size() != 6) {
    throw std::invalid_argument("TWSE exchange time must contain 6 bytes");
  }

  const auto hour = DecodeBcdInteger(bytes.subspan(0, 1));
  const auto minute = DecodeBcdInteger(bytes.subspan(1, 1));
  const auto second = DecodeBcdInteger(bytes.subspan(2, 1));

  const auto millisecond =
      static_cast<std::uint64_t>(DecodeNibble(bytes[3] >> 4U)) * 100U +
      static_cast<std::uint64_t>(DecodeNibble(bytes[3] & 0x0FU)) * 10U +
      DecodeNibble(bytes[4] >> 4U);
  const auto microsecond =
      static_cast<std::uint64_t>(DecodeNibble(bytes[4] & 0x0FU)) * 100U +
      static_cast<std::uint64_t>(DecodeNibble(bytes[5] >> 4U)) * 10U +
      DecodeNibble(bytes[5] & 0x0FU);

  if (hour > 23 || minute > 59 || second > 59 || millisecond > 999 ||
      microsecond > 999) {
    throw std::invalid_argument("invalid TWSE exchange time");
  }

  constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000LL;
  return static_cast<std::int64_t>(hour * 3'600U + minute * 60U + second) *
             kNanosecondsPerSecond +
         static_cast<std::int64_t>(millisecond) * 1'000'000LL +
         static_cast<std::int64_t>(microsecond) * 1'000LL;
}

} // namespace aries::data::twse
