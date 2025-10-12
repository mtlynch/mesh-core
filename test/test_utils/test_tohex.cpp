#include <gtest/gtest.h>
#include "Utils.h"

using namespace mesh;

// Test: Convert single byte to hex
TEST(UtilsToHex, SingleByte) {
    uint8_t input[] = {0xAB};
    char output[3];  // 2 hex chars + null terminator

    Utils::toHex(output, input, 1);

    EXPECT_STREQ("AB", output);
}

// Test: Convert multiple bytes to hex
TEST(UtilsToHex, MultipleBytes) {
    uint8_t input[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    char output[17];  // 16 hex chars + null terminator

    Utils::toHex(output, input, 8);

    EXPECT_STREQ("0123456789ABCDEF", output);
}

// Test: Convert zero byte
TEST(UtilsToHex, ZeroByte) {
    uint8_t input[] = {0x00};
    char output[3];

    Utils::toHex(output, input, 1);

    EXPECT_STREQ("00", output);
}

// Test: Convert max byte value
TEST(UtilsToHex, MaxByte) {
    uint8_t input[] = {0xFF};
    char output[3];

    Utils::toHex(output, input, 1);

    EXPECT_STREQ("FF", output);
}

// Test: Convert empty input (zero length)
TEST(UtilsToHex, EmptyInput) {
    uint8_t input[] = {0xAB};
    char output[10] = "XXXXXXXXX";  // Pre-fill with X's

    Utils::toHex(output, input, 0);

    // Should just null-terminate at position 0
    EXPECT_EQ('\0', output[0]);
}

// Test: Verify null termination
TEST(UtilsToHex, NullTermination) {
    uint8_t input[] = {0x12, 0x34};
    char output[10];

    Utils::toHex(output, input, 2);

    // Check that position 4 (after "1234") is null-terminated
    EXPECT_EQ('\0', output[4]);
}

// Test: Convert bytes with low nibbles
TEST(UtilsToHex, LowNibbles) {
    uint8_t input[] = {0x0F, 0xF0};
    char output[5];

    Utils::toHex(output, input, 2);

    EXPECT_STREQ("0FF0", output);
}

// Test: Verify uppercase output
TEST(UtilsToHex, Uppercase) {
    uint8_t input[] = {0xAB, 0xCD, 0xEF};
    char output[7];

    Utils::toHex(output, input, 3);

    // Verify all hex digits A-F are uppercase
    EXPECT_STREQ("ABCDEF", output);
    EXPECT_EQ('A', output[0]);
    EXPECT_EQ('B', output[1]);
    EXPECT_EQ('C', output[2]);
    EXPECT_EQ('D', output[3]);
    EXPECT_EQ('E', output[4]);
    EXPECT_EQ('F', output[5]);
}

// Test: Mixed values
TEST(UtilsToHex, MixedValues) {
    uint8_t input[] = {0x00, 0x10, 0xA5, 0xFF};
    char output[9];

    Utils::toHex(output, input, 4);

    EXPECT_STREQ("0010A5FF", output);
}

// Test: Longer sequence
TEST(UtilsToHex, LongerSequence) {
    uint8_t input[16];
    for (int i = 0; i < 16; i++) {
        input[i] = i * 16 + i;  // 0x00, 0x11, 0x22, ..., 0xFF
    }
    char output[33];

    Utils::toHex(output, input, 16);

    EXPECT_STREQ("00112233445566778899AABBCCDDEEFF", output);
}

// Google Test automatically discovers and runs all TEST() macros
// No need to manually register tests!

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
