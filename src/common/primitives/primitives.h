#pragma once
// primitives.h — Procedural primitive geometry generation.
// Shared between tools for box/sphere/cylinder/capsule collision primitives.

#include <cstdint>
#include <vector>

namespace lu::assets {

struct PrimitiveMesh {
    std::vector<float> vertices;     // xyz interleaved
    std::vector<uint32_t> indices;   // triangle indices
};

// Generate an axis-aligned box centered at (cx,cy,cz) with half-extents (hx,hy,hz).
PrimitiveMesh generate_box(float cx, float cy, float cz,
                            float hx, float hy, float hz);

// Generate a UV sphere centered at (cx,cy,cz) with given radius.
PrimitiveMesh generate_sphere(float cx, float cy, float cz, float radius,
                               int rings = 8, int sectors = 12);

// Generate a capsule (two half-spheres connected by a cylinder) between
// points a[3] and b[3] with given radius.
PrimitiveMesh generate_capsule(const float* a, const float* b, float radius,
                                int rings = 12, int sectors = 16);

// Generate a cylinder between points a[3] and b[3] with given radius.
PrimitiveMesh generate_cylinder(const float* a, const float* b, float radius,
                                 int segments = 16);

} // namespace lu::assets
