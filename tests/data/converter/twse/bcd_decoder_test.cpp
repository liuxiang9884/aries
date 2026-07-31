#include <array>
#include <cstdint>
#include <stdexcept>

#include <gtest/gtest.h>

#include "data/converter/twse/bcd_decoder.h"

namespace aries::data::twse {
namespace {

TEST(BcdDecoderTest, DecodesIntegerAndScaledDecimal) {
  constexpr std::array<std::uint8_t, 2> kLength{0x01, 0x14};
  constexpr std::array<std::uint8_t, 5> kPrice{0x12, 0x34, 0x56, 0x78, 0x90};

  EXPECT_EQ(DecodeBcdInteger(kLength), 114);
  EXPECT_DOUBLE_EQ(DecodeBcdDecimal(kPrice, 4), 123456.789);
}

TEST(BcdDecoderTest, DecodesExchangeTimeToNanoseconds) {
  constexpr std::array<std::uint8_t, 6> kTime{0x08, 0x30, 0x01,
                                              0x23, 0x45, 0x67};

  EXPECT_EQ(DecodeBcdTimeNanoseconds(kTime), 30'601'234'567'000LL);
}

TEST(BcdDecoderTest, RejectsInvalidDigitAndTime) {
  constexpr std::array<std::uint8_t, 1> kInvalidDigit{0xFA};
  constexpr std::array<std::uint8_t, 6> kInvalidTime{0x24, 0x00, 0x00,
                                                     0x00, 0x00, 0x00};

  EXPECT_THROW((void)DecodeBcdInteger(kInvalidDigit), std::invalid_argument);
  EXPECT_THROW((void)DecodeBcdTimeNanoseconds(kInvalidTime),
               std::invalid_argument);
}

} // namespace
} // namespace aries::data::twse
