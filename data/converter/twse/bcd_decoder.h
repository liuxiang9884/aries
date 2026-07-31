#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace aries::data::twse {

[[nodiscard]] std::uint64_t
DecodeBcdInteger(std::span<const std::uint8_t> bytes);

[[nodiscard]] double DecodeBcdDecimal(std::span<const std::uint8_t> bytes,
                                      std::size_t decimal_digits);

[[nodiscard]] std::int64_t
DecodeBcdTimeNanoseconds(std::span<const std::uint8_t> bytes);

} // namespace aries::data::twse
