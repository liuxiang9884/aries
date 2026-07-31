#pragma once

#include <cstdint>
#include <span>

namespace aries::data::taifex {

[[nodiscard]] std::uint64_t
DecodeBcdInteger(std::span<const std::uint8_t> bytes);

[[nodiscard]] double DecodeBcdDecimal(std::span<const std::uint8_t> bytes,
                                      std::uint8_t decimal_locator);

[[nodiscard]] std::int64_t
DecodeBcdTimeNanoseconds(std::span<const std::uint8_t> bytes);

} // namespace aries::data::taifex
