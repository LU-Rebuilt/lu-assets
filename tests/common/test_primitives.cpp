#include "common/primitives/primitives.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace lu::assets;

// ── Box ──────────────────────────────────────────────────────────────────────

TEST(Primitives, BoxVertexCount) {
    auto m = generate_box(0, 0, 0, 1, 1, 1);
    EXPECT_EQ(m.vertices.size(), 8u * 3); // 8 corners, xyz each
}

TEST(Primitives, BoxIndexCount) {
    auto m = generate_box(0, 0, 0, 1, 1, 1);
    EXPECT_EQ(m.indices.size(), 36u); // 6 faces * 2 triangles * 3 indices
}

TEST(Primitives, BoxCentered) {
    auto m = generate_box(5, 10, 15, 1, 1, 1);
    // All vertices should be within ±1 of center
    for (size_t i = 0; i < m.vertices.size(); i += 3) {
        EXPECT_NEAR(m.vertices[i], 5.0f, 1.01f);
        EXPECT_NEAR(m.vertices[i+1], 10.0f, 1.01f);
        EXPECT_NEAR(m.vertices[i+2], 15.0f, 1.01f);
    }
}

// ── Sphere ───────────────────────────────────────────────────────────────────

TEST(Primitives, SphereHasVertices) {
    auto m = generate_sphere(0, 0, 0, 2.0f);
    EXPECT_GT(m.vertices.size(), 0u);
    EXPECT_GT(m.indices.size(), 0u);
}

TEST(Primitives, SphereVerticesOnSurface) {
    auto m = generate_sphere(0, 0, 0, 3.0f, 8, 12);
    for (size_t i = 0; i < m.vertices.size(); i += 3) {
        float dist = std::sqrt(m.vertices[i]*m.vertices[i] +
                               m.vertices[i+1]*m.vertices[i+1] +
                               m.vertices[i+2]*m.vertices[i+2]);
        EXPECT_NEAR(dist, 3.0f, 0.01f);
    }
}

TEST(Primitives, SphereHigherResolution) {
    auto low = generate_sphere(0, 0, 0, 1.0f, 4, 6);
    auto high = generate_sphere(0, 0, 0, 1.0f, 16, 24);
    EXPECT_GT(high.vertices.size(), low.vertices.size());
}

// ── Capsule ──────────────────────────────────────────────────────────────────

TEST(Primitives, CapsuleHasGeometry) {
    float a[3] = {0, 0, 0}, b[3] = {0, 5, 0};
    auto m = generate_capsule(a, b, 1.0f);
    EXPECT_GT(m.vertices.size(), 0u);
    EXPECT_GT(m.indices.size(), 0u);
}

TEST(Primitives, CapsuleDegenerateFallsBackToSphere) {
    float a[3] = {1, 2, 3}, b[3] = {1, 2, 3}; // same point
    auto m = generate_capsule(a, b, 2.0f);
    EXPECT_GT(m.vertices.size(), 0u); // should still produce geometry (sphere)
}

TEST(Primitives, CapsuleIndicesValid) {
    float a[3] = {0, 0, 0}, b[3] = {0, 3, 0};
    auto m = generate_capsule(a, b, 1.0f, 8, 12);
    uint32_t maxVertex = static_cast<uint32_t>(m.vertices.size() / 3);
    for (auto idx : m.indices) {
        EXPECT_LT(idx, maxVertex);
    }
}

// ── Cylinder ─────────────────────────────────────────────────────────────────

TEST(Primitives, CylinderHasGeometry) {
    float a[3] = {0, 0, 0}, b[3] = {0, 4, 0};
    auto m = generate_cylinder(a, b, 1.0f);
    EXPECT_GT(m.vertices.size(), 0u);
    EXPECT_GT(m.indices.size(), 0u);
}

TEST(Primitives, CylinderDegenerateReturnsEmpty) {
    float a[3] = {0, 0, 0}, b[3] = {0, 0, 0};
    auto m = generate_cylinder(a, b, 1.0f);
    EXPECT_TRUE(m.vertices.empty()); // zero-length cylinder
}

TEST(Primitives, CylinderIndicesValid) {
    float a[3] = {0, 0, 0}, b[3] = {5, 0, 0};
    auto m = generate_cylinder(a, b, 2.0f, 12);
    uint32_t maxVertex = static_cast<uint32_t>(m.vertices.size() / 3);
    for (auto idx : m.indices) {
        EXPECT_LT(idx, maxVertex);
    }
}
