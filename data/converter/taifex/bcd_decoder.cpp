#include "data/converter/taifex/bcd_decoder.h"

#include <array>
#include <limits>
#include <stdexcept>

namespace aries::data::taifex {

std::uint64_t DecodeBcdInteger(std::span<const std::uint8_t> bytes) {
  std::uint64_t value = 0;
  for (const auto byte : bytes) {
    const auto high = static_cast<std::uint8_t>(byte >> 4U);
    const auto low = static_cast<std::uint8_t>(byte & 0x0FU);
    if (high > 9 || low > 9) {
      throw std::invalid_argument("TAIFEX BCD field contains an invalid digit");
    }
    if (value >
        (std::numeric_limits<std::uint64_t>::max() - high * 10U - low) / 100U) {
      throw std::overflow_error("TAIFEX BCD field overflows uint64");
    }
    value = value * 100U + high * 10U + low;
  }
  return value;
}

double DecodeBcdDecimal(std::span<const std::uint8_t> bytes,
                        std::uint8_t decimal_locator) {
  if (decimal_locator > 9) {
    throw std::invalid_argument("TAIFEX decimal locator is out of range");
  }
  constexpr std::array<double, 10> kScales{
      1.0,     0.1,      0.01,      0.001,      0.0001,
      0.00001, 0.000001, 0.0000001, 0.00000001, 0.000000001,
  };
  return static_cast<double>(DecodeBcdInteger(bytes)) *
         kScales[decimal_locator];
}

std::int64_t DecodeBcdTimeNanoseconds(std::span<const std::uint8_t> bytes) {
  if (bytes.size() != 6) {
    throw std::invalid_argument("TAIFEX time must contain six BCD bytes");
  }
  const auto value = DecodeBcdInteger(bytes);
  const auto hour = value / 10'000'000'000ULL;
  const auto minute = value / 100'000'000ULL % 100ULL;
  const auto second = value / 1'000'000ULL % 100ULL;
  const auto millisecond = value / 1'000ULL % 1'000ULL;
  const auto microsecond = value % 1'000ULL;
  if (hour > 23 || minute > 59 || second > 59) {
    throw std::invalid_argument("TAIFEX time is out of range");
  }
  return static_cast<std::int64_t>(
      ((hour * 3'600ULL + minute * 60ULL + second) * 1'000'000'000ULL) +
      millisecond * 1'000'000ULL + microsecond * 1'000ULL);
}

} // namespace aries::data::taifex
