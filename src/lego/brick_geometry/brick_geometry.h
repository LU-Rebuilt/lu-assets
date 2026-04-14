#pragma once

#include "netdevil/zone/luz/luz_types.h"

#include <cstdint>
#include <span>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// References:
//   - lu-toolbox (github.com/Squareville/lu-toolbox) — Blender .g import reference
//   - lcdr/lu_formats g.ksy (github.com/lcdr/lu_formats) — Kaitai Struct spec for .g binary format

// Brick geometry (.g) file parser.
// 5,892 .g/.g1/.g2 files in brickprimitives/ at 3 LOD levels.
//
// Binary format (verified from community geometry parser):
//   [u32 magic]        = 1111961649 (0x42420D31)
//   [u32 vertex_count]
//   [u32 index_count]  (total indices, triangles = index_count / 3)
//   [u32 options]      bitflags:
//       (options & 3) == 3: has texture coordinates
//       (options & 48) == 48: has bone weights
//   [vertex_count * 3 * f32]: vertex positions (x, y, z)
//   [vertex_count * 3 * f32]: vertex normals (nx, ny, nz)
//   if has_uvs: [vertex_count * 2 * f32]: texture coordinates (u, v)
//   [index_count * u32]: triangle indices
//   if has_bones: bone weight data (variable length)

struct BrickGeometryError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr uint32_t BRICK_GEOM_MAGIC = 1111961649; // 0x42420D31

struct BrickVertex {
    Vec3 position;
    Vec3 normal;
    float u = 0, v = 0;
};

struct BrickGeometry {
    uint32_t options = 0;
    bool has_uvs = false;
    bool has_bones = false;
    std::vector<BrickVertex> vertices;
    std::vector<uint32_t> indices;      // Triangle indices (groups of 3)
    std::vector<uint32_t> bone_mapping;  // Per-vertex bone index (when has_bones)
};

BrickGeometry brick_geometry_parse(std::span<const uint8_t> data);

} // namespace lu::assets
