#include <gtest/gtest.h>
#include "microsoft/tga/tga_reader.h"

#include <vector>

using namespace lu::assets;

TEST(TGA, EmptyDataThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(tga_load(empty), TgaError);
}

TEST(TGA, InvalidDataThrows) {
    // Random bytes that are not a valid TGA
    std::vector<uint8_t> garbage = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03};
    EXPECT_THROW(tga_load(garbage), TgaError);
}

TEST(TGA, StructDefaults) {
    TgaImage img;
    EXPECT_EQ(img.width, 0);
    EXPECT_EQ(img.height, 0);
    EXPECT_EQ(img.channels, 0);
    EXPECT_TRUE(img.pixel_data.empty());
}
