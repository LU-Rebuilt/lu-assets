#include <gtest/gtest.h>
#include "lego/brick_geometry/brick_geometry_reader.h"
#include "lego/brick_geometry/brick_geometry_writer.h"

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

// Build a .g file with 3 vertices, 3 indices (one triangle). options controls which
// optional blocks are present, per the real bit layout: 0x01 UVs, 0x02 normals.
std::vector<uint8_t> build_triangle_g(uint32_t options = 0x02) {
    GFileBuilder b;

    b.u32(BRICK_GEOM_MAGIC); // magic
    b.u32(3);                 // vertex_count
    b.u32(3);                 // index_count
    b.u32(options);           // options

    // Vertex positions (3 vertices) — always present.
    b.f32(0.0f); b.f32(0.0f); b.f32(0.0f);  // v0
    b.f32(1.0f); b.f32(0.0f); b.f32(0.0f);  // v1
    b.f32(0.0f); b.f32(1.0f); b.f32(0.0f);  // v2

    // Vertex normals (3 vertices, all pointing +Z) — gated by 0x02.
    if (options & 0x02) {
        b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);
        b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);
        b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);
    }

    // UVs — gated by 0x01.
    if (options & 0x01) {
        b.f32(0.0f); b.f32(0.0f);  // v0 UV
        b.f32(1.0f); b.f32(0.0f);  // v1 UV
        b.f32(0.0f); b.f32(1.0f);  // v2 UV
    }

    // Triangle indices — always present.
    b.u32(0); b.u32(1); b.u32(2);

    return b.data;
}

} // anonymous namespace

TEST(BrickGeometry, ParseTriangleNormalsOnly) {
    auto data = build_triangle_g(0x02);
    auto geom = brick_geometry_parse(data);

    EXPECT_EQ(geom.options, 0x02u);
    EXPECT_FALSE(geom.has_uvs);
    EXPECT_TRUE(geom.has_normals);
    EXPECT_FALSE(geom.has_skin_indices);
    EXPECT_FALSE(geom.has_skin_weights);

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

TEST(BrickGeometry, ParseTriangleWithUVsAndNormals) {
    auto data = build_triangle_g(0x03); // normals + UVs
    auto geom = brick_geometry_parse(data);

    EXPECT_EQ(geom.options, 0x03u);
    EXPECT_TRUE(geom.has_uvs);
    EXPECT_TRUE(geom.has_normals);

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

TEST(BrickGeometry, ParseTriangleNoNormalsNoUVs) {
    auto data = build_triangle_g(0x00);
    auto geom = brick_geometry_parse(data);

    EXPECT_FALSE(geom.has_uvs);
    EXPECT_FALSE(geom.has_normals);
    ASSERT_EQ(geom.vertices.size(), 3u);
    // Normals default to zero since the block is absent.
    EXPECT_FLOAT_EQ(geom.vertices[0].normal.z, 0.0f);
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

TEST(BrickGeometry, SkinIndicesOptionBitFlag) {
    // Build a triangle with the 0x10 skin-index block: weight_group_indices +
    // index_count-sized index_mapping.
    GFileBuilder b;
    b.u32(BRICK_GEOM_MAGIC);
    b.u32(3);      // vertex_count
    b.u32(3);      // index_count
    b.u32(0x12);   // normals (0x02) + skin indices (0x10)

    b.f32(0.0f); b.f32(0.0f); b.f32(0.0f);
    b.f32(1.0f); b.f32(0.0f); b.f32(0.0f);
    b.f32(0.0f); b.f32(1.0f); b.f32(0.0f);
    b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);
    b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);
    b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);
    b.u32(0); b.u32(1); b.u32(2);

    b.u32(2);          // weight_group_indices count
    b.u32(5); b.u32(7); // weight_group_indices values
    b.u32(0); b.u32(1); b.u32(0); // index_mapping (3 entries, one per triangle index)

    auto geom = brick_geometry_parse(b.data);
    EXPECT_TRUE(geom.has_skin_indices);
    EXPECT_FALSE(geom.has_skin_weights);
    ASSERT_EQ(geom.skin_indices.weight_group_indices.size(), 2u);
    EXPECT_EQ(geom.skin_indices.weight_group_indices[0], 5u);
    EXPECT_EQ(geom.skin_indices.weight_group_indices[1], 7u);
    ASSERT_EQ(geom.skin_indices.index_mapping.size(), 3u);
    EXPECT_EQ(geom.skin_indices.index_mapping[1], 1u);
}

TEST(BrickGeometry, RoundTripByteIdenticalNoOptionalBlocks) {
    auto data = build_triangle_g(0x00);
    auto geom = brick_geometry_parse(data);
    EXPECT_EQ(brick_geometry_write(geom), data);
}

TEST(BrickGeometry, RoundTripByteIdenticalWithUVsAndNormals) {
    auto data = build_triangle_g(0x03);
    auto geom = brick_geometry_parse(data);
    EXPECT_EQ(brick_geometry_write(geom), data);
}

TEST(BrickGeometry, RoundTripByteIdenticalWithSkinIndices) {
    GFileBuilder b;
    b.u32(BRICK_GEOM_MAGIC);
    b.u32(3);
    b.u32(3);
    b.u32(0x12);
    b.f32(0.0f); b.f32(0.0f); b.f32(0.0f);
    b.f32(1.0f); b.f32(0.0f); b.f32(0.0f);
    b.f32(0.0f); b.f32(1.0f); b.f32(0.0f);
    b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);
    b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);
    b.f32(0.0f); b.f32(0.0f); b.f32(1.0f);
    b.u32(0); b.u32(1); b.u32(2);
    b.u32(2);
    b.u32(5); b.u32(7);
    b.u32(0); b.u32(1); b.u32(0);

    auto geom = brick_geometry_parse(b.data);
    EXPECT_EQ(brick_geometry_write(geom), b.data);
}

TEST(BrickGeometry, RoundTripByteIdenticalWithTagBlock) {
    GFileBuilder b;
    b.u32(BRICK_GEOM_MAGIC);
    b.u32(1);
    b.u32(0);
    b.u32(0x08); // tag block only
    b.f32(0.0f); b.f32(0.0f); b.f32(0.0f); // 1 vertex position, no normals/uvs
    // no indices (index_count=0)
    b.u32(3);          // tag
    b.u32(4);          // payload size
    b.data.push_back(1); b.data.push_back(2); b.data.push_back(3); b.data.push_back(4);

    auto geom = brick_geometry_parse(b.data);
    EXPECT_TRUE(geom.has_tag);
    EXPECT_EQ(geom.tag, 3u);
    ASSERT_EQ(geom.tag_data.size(), 4u);
    EXPECT_EQ(brick_geometry_write(geom), b.data);
}
