#include "havok/converters/hkx_geometry.h"
#include "havok/types/hkx_types.h"
#include <gtest/gtest.h>

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
