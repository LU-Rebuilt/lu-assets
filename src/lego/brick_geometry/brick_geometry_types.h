#pragma once

#include "netdevil/zone/luz/luz_types.h"

#include <cstdint>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// References:
//   - lu-toolbox (github.com/Squareville/lu-toolbox) — Blender .g import reference
//   - lcdr/lu_formats g.ksy (github.com/lcdr/lu_formats) — Kaitai Struct spec for .g binary format
//   - Ghidra RE of legouniverse.exe 1.10.64: LEGO::BrickGeometry::vfunc3 (0x00982cf0, the
//     real Read dispatch target) and its symmetric writer/allocator siblings vfunc0
//     (0x00982840), vfunc2 (0x00982b80), vfunc4 (0x009830b0) — the option-bit semantics
//     documented below come from there, not from the community sources above, which had
//     the UV/normal bits swapped/combined incorrectly (verified byte-exact against all
//     5934 real .g/.g1/.g2 files under res/brickprimitives/).

// Brick geometry (.g) file format.
// 5,892 .g/.g1/.g2 files in brickprimitives/ at 3 LOD levels.
//
// Binary format:
//   [u32 magic]        = 1111961649 (0x42473031, "BG01")
//   [u32 vertex_count]
//   [u32 index_count]  (total indices, triangles = index_count / 3)
//   [u32 options]      independent bitflags (NOT combined — each gates its own block):
//       0x01: has texture coordinates (per-vertex UV)
//       0x02: has normals (per-vertex, present in ~100% of real files)
//       0x04: has an "extra vertex data" block (opaque blob + per-vertex remap array)
//       0x08: has a trailing opaque "tag" block (tag u32 + size u32 + raw bytes)
//       0x10: has a "skin weight index" block (weight-group index list)
//       0x20: has a "skin weight" block (Vec3-valued table, not simple indices)
//   [vertex_count * 3 * f32]: vertex positions (x, y, z) — always present
//   if 0x02: [vertex_count * 3 * f32]: vertex normals (nx, ny, nz)
//   if 0x01: [vertex_count * 2 * f32]: texture coordinates (u, v)
//   [index_count * u32]: triangle indices — always present
//   if 0x10: [u32 count][count * u32 weight_group_indices][index_count * u32 mapping]
//   if 0x20: [u32 count][count * Vec3 weights][index_count * u32 mapping]
//   if 0x04: [u32 size][size bytes extra_data][vertex_count * u32 remap]
//   if 0x08: [u32 tag][u32 size][size bytes opaque payload]

struct BrickGeometryError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr uint32_t BRICK_GEOM_MAGIC = 1111961649; // 0x42473031 = "BG01" (little-endian bytes 31 30 47 42)

struct BrickVertex {
    Vec3 position;
    Vec3 normal;
    float u = 0, v = 0;
};

// One of the two independent skin-weight tail blocks (options 0x10 / 0x20).
struct BrickSkinBlock {
    std::vector<uint32_t> weight_group_indices; // 0x10 block only
    std::vector<Vec3>     weights;              // 0x20 block only
    std::vector<uint32_t> index_mapping;        // index_count entries, both blocks
};

struct BrickGeometry {
    uint32_t options = 0;
    bool has_uvs     = false; // 0x01
    bool has_normals = false; // 0x02
    bool has_extra   = false; // 0x04
    bool has_tag     = false; // 0x08
    bool has_skin_indices = false; // 0x10
    bool has_skin_weights = false; // 0x20

    std::vector<BrickVertex> vertices;
    std::vector<uint32_t> indices;      // Triangle indices (groups of 3)

    BrickSkinBlock skin_indices; // present when has_skin_indices
    BrickSkinBlock skin_weights; // present when has_skin_weights

    std::vector<uint8_t> extra_data;       // 0x04 block payload
    std::vector<uint32_t> extra_remap;     // 0x04 block per-vertex remap array

    uint32_t tag = 0;                 // 0x08 block
    std::vector<uint8_t> tag_data;     // 0x08 block payload
};

} // namespace lu::assets
