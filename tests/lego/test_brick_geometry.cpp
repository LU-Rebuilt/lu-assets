#include <gtest/gtest.h>
#include "lego/brick_geometry/brick_geometry.h"

#include <cstring>
#include <vector>

using namespace lu::assets;

namespace {

struct GFileBuilder {
    std::vector<uint8_t> data;
    void u32(uint32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void f32(float v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
};

// Build a .g file with 3 vertices, 3 indices (one triangle), no UVs.
std::vector<uint8_t> build_triangle_g(uint32_t options = 0) {
    GFileBuilder b;

    b.u32(BRICK_GEOM_MAGIC); // magic
    b.u32(3);                 // vertex_count
    b.u32(3);                 // index_count
    b.u32(options);           // options

    // Vertex positions (3 vertices)
    b.f32(0.0f); b.f32(0.0f); b.f32(0.0f);  // v0
    b.f32(1.0f); b.f32(0.0f); b.f32(0.0f);  // v1
    b.f32(0.0f); b.f32(1.0f); b.f32(0.0f);  // v2

    // Vertex normals (3 vertices, all pointing +Z)
    b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);
    b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);
    b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);

    // UVs (only if options & 3 == 3)
    if ((options & 3) == 3) {
        b.f32(0.0f); b.f32(0.0f);  // v0 UV
        b.f32(1.0f); b.f32(0.0f);  // v1 UV
        b.f32(0.0f); b.f32(1.0f);  // v2 UV
    }

    // Triangle indices
    b.u32(0); b.u32(1); b.u32(2);

    return b.data;
}

} // anonymous namespace

TEST(BrickGeometry, ParseTriangleNoUVs) {
    auto data = build_triangle_g(0);
    auto geom = brick_geometry_parse(data);

    EXPECT_EQ(geom.options, 0u);
    EXPECT_FALSE(geom.has_uvs);
    EXPECT_FALSE(geom.has_bones);

    ASSERT_EQ(geom.vertices.size(), 3u);
    EXPECT_FLOAT_EQ(geom.vertices[0].position.x, 0.0f);
    EXPECT_FLOAT_EQ(geom.vertices[0].position.y, 0.0f);
    EXPECT_FLOAT_EQ(geom.vertices[0].position.z, 0.0f);
    EXPECT_FLOAT_EQ(geom.vertices[1].position.x, 1.0f);
    EXPECT_FLOAT_EQ(geom.vertices[2].position.y, 1.0f);

    // Normals
    EXPECT_FLOAT_EQ(geom.vertices[0].normal.z, 1.0f);
    EXPECT_FLOAT_EQ(geom.vertices[1].normal.z, 1.0f);

    // UVs should be default (0,0)
    EXPECT_FLOAT_EQ(geom.vertices[0].u, 0.0f);
    EXPECT_FLOAT_EQ(geom.vertices[0].v, 0.0f);

    ASSERT_EQ(geom.indices.size(), 3u);
    EXPECT_EQ(geom.indices[0], 0u);
    EXPECT_EQ(geom.indices[1], 1u);
    EXPECT_EQ(geom.indices[2], 2u);
}

TEST(BrickGeometry, ParseTriangleWithUVs) {
    auto data = build_triangle_g(3); // options = 3 means has_uvs
    auto geom = brick_geometry_parse(data);

    EXPECT_EQ(geom.options, 3u);
    EXPECT_TRUE(geom.has_uvs);
    EXPECT_FALSE(geom.has_bones);

    ASSERT_EQ(geom.vertices.size(), 3u);

    // Verify UV values
    EXPECT_FLOAT_EQ(geom.vertices[0].u, 0.0f);
    EXPECT_FLOAT_EQ(geom.vertices[0].v, 0.0f);
    EXPECT_FLOAT_EQ(geom.vertices[1].u, 1.0f);
    EXPECT_FLOAT_EQ(geom.vertices[1].v, 0.0f);
    EXPECT_FLOAT_EQ(geom.vertices[2].u, 0.0f);
    EXPECT_FLOAT_EQ(geom.vertices[2].v, 1.0f);

    // Positions should still be correct
    EXPECT_FLOAT_EQ(geom.vertices[1].position.x, 1.0f);
}

TEST(BrickGeometry, InvalidMagicThrows) {
    GFileBuilder b;
    b.u32(0xDEADBEEF); // wrong magic
    b.u32(0);           // vertex_count
    b.u32(0);           // index_count
    b.u32(0);           // options

    EXPECT_THROW(brick_geometry_parse(b.data), BrickGeometryError);
}

TEST(BrickGeometry, MagicConstantValue) {
    EXPECT_EQ(BRICK_GEOM_MAGIC, 1111961649u);
    EXPECT_EQ(BRICK_GEOM_MAGIC, 0x42473031u);
}

TEST(BrickGeometry, EmptyDataThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(brick_geometry_parse(empty), std::out_of_range);
}

TEST(BrickGeometry, BoneOptionsBitFlag) {
    // options = 48 (0x30) means has_bones = true, has_uvs = false
    auto data = build_triangle_g(48);
    auto geom = brick_geometry_parse(data);

    EXPECT_FALSE(geom.has_uvs);
    EXPECT_TRUE(geom.has_bones);
}
