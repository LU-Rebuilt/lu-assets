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

// ---- Structural container parse + round-trip (tga_parse / tga_write) ----

#include "microsoft/tga/tga_reader.h"
#include "microsoft/tga/tga_writer.h"

namespace {

std::vector<uint8_t> build_tga_container() {
    // 2x1 uncompressed 24-bit truecolor, 1-byte image ID, no palette
    std::vector<uint8_t> d = {
        1,          // id_length
        0,          // color_map_type
        2,          // image_type: uncompressed truecolor
        0, 0,       // color_map_first
        0, 0,       // color_map_length
        0,          // color_map_depth
        0, 0,       // x_origin
        0, 0,       // y_origin
        2, 0,       // width
        1, 0,       // height
        24,         // bits_per_pixel
        0x20,       // descriptor (top-left origin)
        0x42,       // image ID byte
        10, 20, 30, // pixel 0 BGR
        40, 50, 60, // pixel 1 BGR
    };
    return d;
}

} // anonymous namespace

TEST(TGA, StructuralParseFields) {
    auto data = build_tga_container();
    auto tga = tga_parse(data);
    EXPECT_EQ(tga.image_type, 2);
    EXPECT_EQ(tga.width, 2);
    EXPECT_EQ(tga.height, 1);
    EXPECT_EQ(tga.bits_per_pixel, 24);
    EXPECT_EQ(tga.descriptor, 0x20);
    EXPECT_EQ(tga.payload.size(), 1u + 6u); // image ID + pixels
}

TEST(TGA, RoundTripByteIdentical) {
    auto data = build_tga_container();
    auto tga = tga_parse(data);
    EXPECT_EQ(tga_write(tga), data);
}

TEST(TGA, StructuralParseTooSmallThrows) {
    std::vector<uint8_t> tiny(10, 0);
    EXPECT_THROW(tga_parse(tiny), TgaError);
}
