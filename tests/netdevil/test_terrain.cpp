#include <gtest/gtest.h>
#include "netdevil/zone/terrain/terrain_reader.h"

#include <cstring>
#include <vector>

using namespace lu::assets;

namespace {

struct TerrainBuilder {
    std::vector<uint8_t> data;
    void u8(uint8_t v) { data.push_back(v); }
    void u16(uint16_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 2);
    }
    void u32(uint32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void f32(float v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
};

// Build a minimal .raw terrain file (version 31, pre-v32 format) with one 4x4 chunk.
// Version < 32 has a shader ID field and color_map_res = chunk.width.
std::vector<uint8_t> build_one_chunk_terrain() {
    TerrainBuilder b;

    // Header
    b.u16(31);     // version
    b.u8(0);       // dev byte
    b.u32(1);      // chunk_count
    b.u32(1);      // chunks_width
    b.u32(1);      // chunks_height

    // Chunk 0
    b.u32(0);      // chunk_id
    b.u32(4);      // width
    b.u32(4);      // height
    b.f32(100.0f); // offset_x
    b.f32(200.0f); // offset_z

    // version < 32: shader ID
    b.u32(0);

    // 4 texture IDs (16 bytes)
    b.u32(0); b.u32(0); b.u32(0); b.u32(0);

    b.f32(2.5f);   // scale

    // Heightmap: 4x4 = 16 floats, fill with row*4+col values
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            b.f32(static_cast<float>(row * 4 + col));
        }
    }

    // Color map: version < 32 uses color_map_res = width = 4
    // 4*4*4 = 64 bytes of RGBA
    for (int i = 0; i < 4 * 4; ++i) {
        b.u8(255); b.u8(0); b.u8(0); b.u8(255); // red RGBA
    }

    // Light map size = 0
    b.u32(0);

    // No version >= 32 extra data

    // Flair count = 0
    b.u32(0);

    return b.data;
}

} // anonymous namespace

TEST(Terrain, ParseOneChunk) {
    auto data = build_one_chunk_terrain();
    auto terrain = terrain_parse(data);

    EXPECT_EQ(terrain.version, 31u);
    EXPECT_EQ(terrain.chunk_count, 1u);
    EXPECT_EQ(terrain.chunks_width, 1u);
    EXPECT_EQ(terrain.chunks_height, 1u);

    ASSERT_EQ(terrain.chunks.size(), 1u);
    const auto& chunk = terrain.chunks[0];

    EXPECT_EQ(chunk.chunk_id, 0u);
    EXPECT_EQ(chunk.width, 4u);
    EXPECT_EQ(chunk.height, 4u);
    EXPECT_FLOAT_EQ(chunk.offset_x, 100.0f);
    EXPECT_FLOAT_EQ(chunk.offset_z, 200.0f);
    EXPECT_FLOAT_EQ(chunk.scale, 2.5f);

    ASSERT_EQ(chunk.heightmap.size(), 16u);
    EXPECT_FLOAT_EQ(chunk.heightmap[0], 0.0f);
    EXPECT_FLOAT_EQ(chunk.heightmap[5], 5.0f);  // row 1, col 1
    EXPECT_FLOAT_EQ(chunk.heightmap[15], 15.0f); // last element
}

TEST(Terrain, HeightmapCornerValues) {
    auto data = build_one_chunk_terrain();
    auto terrain = terrain_parse(data);

    ASSERT_EQ(terrain.chunks.size(), 1u);
    const auto& hm = terrain.chunks[0].heightmap;

    // Top-left
    EXPECT_FLOAT_EQ(hm[0], 0.0f);
    // Top-right
    EXPECT_FLOAT_EQ(hm[3], 3.0f);
    // Bottom-left
    EXPECT_FLOAT_EQ(hm[12], 12.0f);
    // Bottom-right
    EXPECT_FLOAT_EQ(hm[15], 15.0f);
}

TEST(Terrain, EmptyDataThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(terrain_parse(empty), std::out_of_range);
}
