#include <gtest/gtest.h>
#include "common/binary_reader/binary_reader.h"

using namespace lu::assets;

TEST(BinaryReader, ReadU8) {
    uint8_t data[] = {0x42, 0xFF};
    BinaryReader r(data);
    EXPECT_EQ(r.read_u8(), 0x42);
    EXPECT_EQ(r.read_u8(), 0xFF);
    EXPECT_TRUE(r.eof());
}

TEST(BinaryReader, ReadU16LittleEndian) {
    uint8_t data[] = {0x34, 0x12};
    BinaryReader r(data);
    EXPECT_EQ(r.read_u16(), 0x1234);
}

TEST(BinaryReader, ReadU32LittleEndian) {
    uint8_t data[] = {0x78, 0x56, 0x34, 0x12};
    BinaryReader r(data);
    EXPECT_EQ(r.read_u32(), 0x12345678u);
}

TEST(BinaryReader, ReadU64LittleEndian) {
    uint8_t data[] = {0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12};
    BinaryReader r(data);
    EXPECT_EQ(r.read_u64(), 0x1234567890ABCDEFull);
}

TEST(BinaryReader, ReadF32) {
    // IEEE 754: 1.0f = 0x3F800000
    uint8_t data[] = {0x00, 0x00, 0x80, 0x3F};
    BinaryReader r(data);
    EXPECT_FLOAT_EQ(r.read_f32(), 1.0f);
}

TEST(BinaryReader, ReadString8) {
    uint8_t data[] = {3, 'a', 'b', 'c'};
    BinaryReader r(data);
    EXPECT_EQ(r.read_string8(), "abc");
}

TEST(BinaryReader, ReadString32) {
    uint8_t data[] = {2, 0, 0, 0, 'h', 'i'};
    BinaryReader r(data);
    EXPECT_EQ(r.read_string32(), "hi");
}

TEST(BinaryReader, SeekAndSkip) {
    uint8_t data[] = {0, 1, 2, 3, 4, 5};
    BinaryReader r(data);
    r.seek(3);
    EXPECT_EQ(r.pos(), 3u);
    EXPECT_EQ(r.read_u8(), 3);
    r.skip(1);
    EXPECT_EQ(r.read_u8(), 5);
}

TEST(BinaryReader, BoundsCheckThrows) {
    uint8_t data[] = {0};
    BinaryReader r(data);
    EXPECT_THROW(r.read_u32(), std::out_of_range);
}

TEST(BinaryReader, SeekPastEndThrows) {
    uint8_t data[] = {0};
    BinaryReader r(data);
    EXPECT_THROW(r.seek(100), std::out_of_range);
}

TEST(BinaryReader, PeekU32At) {
    uint8_t data[] = {0, 0, 0, 0, 0x78, 0x56, 0x34, 0x12};
    BinaryReader r(data);
    EXPECT_EQ(r.peek_u32_at(4), 0x12345678u);
    EXPECT_EQ(r.pos(), 0u); // Didn't advance
}
