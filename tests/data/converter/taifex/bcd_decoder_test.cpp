#include <array>
#include <cstdint>
#include <stdexcept>

#include <gtest/gtest.h>

#include "data/converter/taifex/bcd_decoder.h"

namespace aries::data::taifex {
namespace {

TEST(TaifexBcdDecoderTest, DecodesIntegerScaledDecimalAndTime) {
  constexpr std::array<std::uint8_t, 5> kPrice{0x00, 0x00, 0x10, 0x10, 0x50};
  constexpr std::array<std::uint8_t, 6> kTime{0x09, 0x00, 0x00,
                                              0x01, 0x23, 0x45};

  EXPECT_EQ(DecodeBcdInteger(kPrice), 1'010'50);
  EXPECT_DOUBLE_EQ(DecodeBcdDecimal(kPrice, 2), 1010.5);
  EXPECT_EQ(DecodeBcdTimeNanoseconds(kTime), 32'400'012'345'000LL);
}

TEST(TaifexBcdDecoderTest, RejectsInvalidBcdAndTime) {
  constexpr std::array<std::uint8_t, 1> kInvalidBcd{0x1A};
  constexpr std::array<std::uint8_t, 6> kInvalidTime{0x25, 0x00, 0x00,
                                                     0x00, 0x00, 0x00};
  EXPECT_THROW((void)DecodeBcdInteger(kInvalidBcd), std::invalid_argument);
  EXPECT_THROW((void)DecodeBcdTimeNanoseconds(kInvalidTime),
               std::invalid_argument);
}

} // namespace
} // namespace aries::data::taifex
