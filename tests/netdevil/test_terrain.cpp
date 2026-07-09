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

    // Color map: version < 32 has no on-disk resolution field — the client reads
    // width*width BGRA texels directly (color_map_res = width - 1 = 3, inner-cropped).
    for (int i = 0; i < 4 * 4; ++i) {
        b.u8(0); b.u8(0); b.u8(255); b.u8(255); // BGRA on disk -> RGBA {255,0,0,255} in view
    }

    // No light map for version < 32 (RAWReadColorandLightMaps gates it on version >= 32).

    // Texture map: tex_map_res(u32) + tex_map_res^2 BGRA texels (no crop, unlike color map).
    b.u32(2);
    for (int i = 0; i < 2 * 2; ++i) {
        b.u8(10); b.u8(20); b.u8(30); b.u8(255); // BGRA -> RGBA {30,20,10,255}
    }
    // No texture_settings/blend_map for version < 32.

    // Flair count = 0
    b.u32(0);

    // Scene map (version == 31): (color_map_res+1)^2 = 4^2 = 16 bytes on disk, inner
    // 3x3=9 kept in the cropped view.
    for (int i = 0; i < 4 * 4; ++i) {
        b.u8(static_cast<uint8_t>(i));
    }

    // No mesh LOD section for version < 32.

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

// ---- Round-trip (terrain_write) ----

#include "netdevil/zone/terrain/terrain_writer.h"

TEST(Terrain, RoundTripByteIdentical) {
    auto data = build_one_chunk_terrain();
    auto terrain = terrain_parse(data);
    EXPECT_EQ(terrain_write(terrain), data);
}

TEST(Terrain, V31ColorMapFullPreservesCroppedTexels) {
    // version < 32 stores width*width BGRA texels; the client's in-memory view crops to
    // (width-1)^2 and swizzles to RGBA. color_map_full must keep every on-disk byte.
    auto data = build_one_chunk_terrain();
    auto terrain = terrain_parse(data);
    ASSERT_EQ(terrain.chunks.size(), 1u);
    const auto& chunk = terrain.chunks[0];
    EXPECT_EQ(chunk.color_map_full.size(), 4u * 4u * 4u); // full width^2 grid
    EXPECT_EQ(chunk.color_map.size(), 3u * 3u * 4u);      // cropped view
    // Builder wrote BGRA {0,0,255,255} -> view is RGBA {255,0,0,255}
    EXPECT_EQ(chunk.color_map_full[0], 0);   // B on disk
    EXPECT_EQ(chunk.color_map[0], 255);      // R in view
    EXPECT_EQ(chunk.color_map[2], 0);        // B in view
}

TEST(Terrain, V31TextureMapNoCropUnlikeColorMap) {
    // Unlike the color map, texturemapsize is the direct read/store size (no -1 crop).
    auto data = build_one_chunk_terrain();
    auto terrain = terrain_parse(data);
    ASSERT_EQ(terrain.chunks.size(), 1u);
    const auto& chunk = terrain.chunks[0];
    EXPECT_EQ(chunk.tex_map_res, 2u);
    ASSERT_EQ(chunk.texture_map.size(), 2u * 2u * 4u);
    // Builder wrote BGRA {10,20,30,255} -> RGBA {30,20,10,255}
    EXPECT_EQ(chunk.texture_map[0], 30);
    EXPECT_EQ(chunk.texture_map[1], 20);
    EXPECT_EQ(chunk.texture_map[2], 10);
    EXPECT_EQ(chunk.texture_map[3], 255);
}

TEST(Terrain, V31HasNoLightMapOrMeshSection) {
    // version < 32 omits the light map and mesh LOD sections entirely (not just empty —
    // absent from the byte stream, per RAWReadColorandLightMaps/RAWReadMeshData version
    // gates in legouniverse.exe).
    auto data = build_one_chunk_terrain();
    auto terrain = terrain_parse(data);
    ASSERT_EQ(terrain.chunks.size(), 1u);
    const auto& chunk = terrain.chunks[0];
    EXPECT_TRUE(chunk.light_map.empty());
    EXPECT_EQ(chunk.mesh_vert_count, 0u);
}

TEST(Terrain, V31SceneMapCroppedWithBorderPreserved) {
    // version == 31: scene map is (color_map_res+1)^2 on disk, client keeps only the
    // inner color_map_res^2 texels — scene_map_full must retain the border for
    // byte-identical round-trips even though the client discards it.
    auto data = build_one_chunk_terrain();
    auto terrain = terrain_parse(data);
    ASSERT_EQ(terrain.chunks.size(), 1u);
    const auto& chunk = terrain.chunks[0];
    EXPECT_EQ(chunk.color_map_res, 3u);
    ASSERT_EQ(chunk.scene_map_full.size(), 4u * 4u);
    ASSERT_EQ(chunk.scene_map.size(), 3u * 3u);
    // Builder wrote scene_map_full[i] = i for a row-major 4x4 grid; the cropped view
    // keeps rows/cols 0-2, i.e. indices {0,1,2, 4,5,6, 8,9,10}.
    EXPECT_EQ(chunk.scene_map_full[3], 3);   // border column, dropped from the view
    EXPECT_EQ(chunk.scene_map[0], 0);
    EXPECT_EQ(chunk.scene_map[2], 2);
    EXPECT_EQ(chunk.scene_map[8], 10);       // view[2][2] == full[2][2] == full row2*4+2
}

// Version < 31: no scene map, just a single skipped byte (RAWReadSceneMap's oldest branch).
std::vector<uint8_t> build_v30_chunk_terrain() {
    TerrainBuilder b;
    b.u16(30); b.u8(0);
    b.u32(1); b.u32(1); b.u32(1);
    b.u32(0); b.u32(2); b.u32(2);
    b.f32(0.0f); b.f32(0.0f);
    b.u32(0);                     // shader_id (version < 32)
    b.u32(0); b.u32(0); b.u32(0); b.u32(0);
    b.f32(1.0f);
    for (int i = 0; i < 4; ++i) b.f32(static_cast<float>(i)); // 2x2 heightmap
    for (int i = 0; i < 2 * 2; ++i) { b.u8(0); b.u8(0); b.u8(0); b.u8(0); } // color map
    b.u32(1);                     // tex_map_res = 1
    b.u8(1); b.u8(2); b.u8(3); b.u8(255);
    b.u32(0);                     // flair_count
    b.u8(0xAB);                   // the single skipped scene-map byte
    return b.data;
}

TEST(Terrain, V30SkipsSingleSceneMapByte) {
    auto data = build_v30_chunk_terrain();
    auto terrain = terrain_parse(data);
    ASSERT_EQ(terrain.chunks.size(), 1u);
    EXPECT_EQ(terrain.chunks[0].scene_map_skip_byte, 0xAB);
    EXPECT_TRUE(terrain.chunks[0].scene_map.empty());
}

TEST(Terrain, V30RoundTripByteIdentical) {
    auto data = build_v30_chunk_terrain();
    auto terrain = terrain_parse(data);
    EXPECT_EQ(terrain_write(terrain), data);
}
