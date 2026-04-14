#include <gtest/gtest.h>
#include "microsoft/dds/dds_reader.h"

#include <cstring>
#include <vector>

using namespace lu::assets;

namespace {

struct DdsBuilder {
    std::vector<uint8_t> data;
    void u32(uint32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void pad(size_t n) {
        data.insert(data.end(), n, 0);
    }
};

// Build a synthetic DDS with given dimensions and pixel format flags.
// The DDS header is always 128 bytes: 4 (magic) + 124 (header struct).
std::vector<uint8_t> build_dds(uint32_t width, uint32_t height,
                                uint32_t mip_count, uint32_t pf_flags,
                                uint32_t four_cc, uint32_t rgb_bits = 32) {
    DdsBuilder b;

    // Magic "DDS "
    b.u32(DDS_MAGIC);

    // DDS_HEADER (124 bytes)
    b.u32(124);        // size
    b.u32(0x1007);     // flags (CAPS | HEIGHT | WIDTH | PIXELFORMAT)
    b.u32(height);     // height
    b.u32(width);      // width
    b.u32(0);          // pitchOrLinearSize
    b.u32(0);          // depth
    b.u32(mip_count);  // mipMapCount

    // reserved1[11]
    b.pad(44);

    // DDS_PIXELFORMAT (32 bytes)
    b.u32(32);         // size
    b.u32(pf_flags);   // flags
    b.u32(four_cc);    // fourCC
    b.u32(rgb_bits);   // rgbBitCount
    b.u32(0x00FF0000); // rBitMask
    b.u32(0x0000FF00); // gBitMask
    b.u32(0x000000FF); // bBitMask
    b.u32(0xFF000000); // aBitMask

    // caps
    b.u32(0x1000);     // DDSCAPS_TEXTURE
    b.u32(0);          // caps2
    b.u32(0);          // caps3
    b.u32(0);          // caps4
    b.u32(0);          // reserved2

    return b.data;
}

} // anonymous namespace

TEST(DDS, ParseUncompressedRGBA) {
    auto data = build_dds(256, 128, 5, DDPF_RGB | DDPF_ALPHAPIXELS, 0, 32);
    auto dds = dds_parse_header(data);

    EXPECT_EQ(dds.width, 256u);
    EXPECT_EQ(dds.height, 128u);
    EXPECT_EQ(dds.mip_count, 5u);
    EXPECT_FALSE(dds.is_compressed);
    EXPECT_EQ(dds.bits_per_pixel, 32u);
    EXPECT_EQ(dds.data_offset, 128u);
}

TEST(DDS, ParseDXT1Compressed) {
    auto data = build_dds(512, 512, 10, DDPF_FOURCC, FOURCC_DXT1);
    auto dds = dds_parse_header(data);

    EXPECT_EQ(dds.width, 512u);
    EXPECT_EQ(dds.height, 512u);
    EXPECT_EQ(dds.mip_count, 10u);
    EXPECT_TRUE(dds.is_compressed);
    EXPECT_EQ(dds.four_cc, FOURCC_DXT1);
    EXPECT_EQ(dds.data_offset, 128u);
}

TEST(DDS, ParseDXT5Compressed) {
    auto data = build_dds(64, 64, 1, DDPF_FOURCC, FOURCC_DXT5);
    auto dds = dds_parse_header(data);

    EXPECT_TRUE(dds.is_compressed);
    EXPECT_EQ(dds.four_cc, FOURCC_DXT5);
}

TEST(DDS, ParseDX10ExtendedOffset) {
    auto data = build_dds(128, 128, 1, DDPF_FOURCC, FOURCC_DX10);
    // Append 20 bytes of DX10 extended header so the file is big enough
    data.insert(data.end(), 20, 0);

    auto dds = dds_parse_header(data);

    EXPECT_TRUE(dds.is_compressed);
    EXPECT_EQ(dds.four_cc, FOURCC_DX10);
    // DX10 extended header pushes data_offset to 128 + 20 = 148
    EXPECT_EQ(dds.data_offset, 148u);
}

TEST(DDS, MipCountZeroDefaultsToOne) {
    auto data = build_dds(32, 32, 0, DDPF_RGB, 0, 24);
    auto dds = dds_parse_header(data);

    // mip_map_count = 0 should result in mip_count = 1
    EXPECT_EQ(dds.mip_count, 1u);
}

TEST(DDS, InvalidMagicThrows) {
    DdsBuilder b;
    b.u32(0xDEADBEEF); // wrong magic
    b.pad(124);         // fill the rest of header

    EXPECT_THROW(dds_parse_header(b.data), DdsError);
}

TEST(DDS, TooSmallThrows) {
    // Less than 128 bytes
    std::vector<uint8_t> tiny(64, 0);
    EXPECT_THROW(dds_parse_header(tiny), DdsError);
}

TEST(DDS, MagicConstantValues) {
    EXPECT_EQ(DDS_MAGIC, 0x20534444u);
    EXPECT_EQ(FOURCC_DXT1, 0x31545844u);
    EXPECT_EQ(FOURCC_DXT3, 0x33545844u);
    EXPECT_EQ(FOURCC_DXT5, 0x35545844u);
}
