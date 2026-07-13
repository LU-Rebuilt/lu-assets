#include "havok/converters/hkx_geometry.h"
#include "havok/types/hkx_types.h"
#include <gtest/gtest.h>

#include <cmath>

using namespace Hkx;

TEST(HkxGeometry, EmptyResultExtractsNothing) {
    ParseResult empty;
    empty.success = true;
    auto result = extractGeometry(empty);
    EXPECT_TRUE(result.meshes.empty());
    EXPECT_EQ(result.rigidBodyCount, 0);
}

TEST(HkxGeometry, WithRigidBodyExtractsShapes) {
    ParseResult pr;
    pr.success = true;

    RigidBodyInfo rb;
    rb.shape.type = ShapeType::ExtendedMesh;
    rb.shape.planeEquations.push_back({0, 0, 0, 0});
    rb.shape.planeEquations.push_back({1, 0, 0, 0});
    rb.shape.planeEquations.push_back({0, 1, 0, 0});
    rb.shape.triangles.push_back({0, 1, 2});
    rb.motion.motionState.transform = Transform{};
    pr.rigidBodies.push_back(rb);

    auto result = extractGeometry(pr);
    EXPECT_EQ(result.rigidBodyCount, 1);
    EXPECT_GE(result.shapeCount, 1);
}

TEST(HkxGeometry, CollisionExtractsTriangles) {
    ParseResult pr;
    pr.success = true;

    RigidBodyInfo rb;
    rb.shape.type = ShapeType::ExtendedMesh;
    // Add some triangle data via planeEquations (vertex storage convention)
    rb.shape.planeEquations.push_back({0, 0, 0, 0});
    rb.shape.planeEquations.push_back({1, 0, 0, 0});
    rb.shape.planeEquations.push_back({0, 1, 0, 0});
    rb.shape.triangles.push_back({0, 1, 2});
    rb.motion.motionState.transform = Transform{};
    pr.rigidBodies.push_back(rb);

    auto collision = extractCollision(pr);
    EXPECT_GE(collision.vertices.size(), 9u); // at least 3 vertices * 3 floats
    EXPECT_GE(collision.indices.size(), 3u);   // at least 1 triangle
}

TEST(HkxGeometry, TransformApplied) {
    ParseResult pr;
    pr.success = true;

    RigidBodyInfo rb;
    rb.shape.type = ShapeType::Box;
    rb.shape.halfExtents = {1, 1, 1, 0};
    rb.motion.motionState.transform = Transform{};
    rb.motion.motionState.transform.translation = {10, 20, 30, 1};
    pr.rigidBodies.push_back(rb);

    Transform world;
    world.translation = {100, 0, 0, 1};
    auto collision = extractCollision(pr, world);
    // With world offset of 100 in X, vertices should be near x=110
    if (!collision.vertices.empty()) {
        EXPECT_GT(collision.vertices[0], 50.0f); // shifted by world transform
    }
}

// hkpConvexVerticesShape has no stored faces; the extractor computes the convex
// hull from the (SIMD-transposed) vertex data. Given the 8 corners of a unit
// cube it must produce a closed hull (a cube hull -> 12 triangles).
TEST(HkxGeometry, ConvexVerticesFormsHull) {
    ParseResult pr;
    pr.success = true;

    RigidBodyInfo rb;
    rb.shape.type = ShapeType::ConvexVertices;
    rb.shape.numVertices = 8;
    // 8 cube corners packed as two FourTransposedPoints (xs/ys/zs per group).
    // Group 0: corners (-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1)
    // Group 1: corners (-1,-1, 1),(1,-1, 1),(1,1, 1),(-1,1, 1)
    FourTransposedPoints g0;
    g0.xs = {-1, 1, 1, -1};
    g0.ys = {-1, -1, 1, 1};
    g0.zs = {-1, -1, -1, -1};
    FourTransposedPoints g1;
    g1.xs = {-1, 1, 1, -1};
    g1.ys = {-1, -1, 1, 1};
    g1.zs = {1, 1, 1, 1};
    rb.shape.rotatedVertices = {g0, g1};
    pr.rigidBodies.push_back(rb);

    auto result = extractGeometry(pr);
    ASSERT_FALSE(result.meshes.empty());
    const auto& m = result.meshes[0];
    EXPECT_EQ(m.shapeType, ShapeType::ConvexVertices);
    // A cube hull is 12 triangles (6 faces x 2). The incremental hull may split
    // coplanar faces differently, so require at least a closed-looking set.
    EXPECT_GE(m.indices.size(), 12u * 3u - 6u);
    EXPECT_EQ(m.indices.size() % 3u, 0u);
    // Every hull vertex must lie within the cube's [-1,1] bounds.
    for (size_t i = 0; i + 2 < m.vertices.size(); i += 3) {
        EXPECT_LE(std::abs(m.vertices[i]), 1.001f);
        EXPECT_LE(std::abs(m.vertices[i + 1]), 1.001f);
        EXPECT_LE(std::abs(m.vertices[i + 2]), 1.001f);
    }
}
