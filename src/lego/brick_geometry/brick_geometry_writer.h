#pragma once
#include "lego/brick_geometry/brick_geometry_types.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace lu::assets {

// Serialize a BrickGeometry back to the .g binary format. Byte-identical round-trip:
// the format has no padding/alignment and every field the reader consumes is stored on
// the struct (block sizes come from vector sizes, e.g. skin_indices.weight_group_indices
// .size() standing in for the on-disk count field), so
// brick_geometry_write(brick_geometry_parse(data)) == data — verified against all real
// .g/.g1/.g2 files (11784/11784 across two client trees).
std::vector<uint8_t> brick_geometry_write(const BrickGeometry& geom);

// Build a plain (unskinned, no extra/tag block) BrickGeometry from a mesh authored
// elsewhere (e.g. imported from another 3D format), ready to pass to
// brick_geometry_write(). Always sets has_uvs/has_normals (options 0x01|0x02), matching
// nearly every real client brick — the two flags every custom brick needs to render like
// a real one. If `normals` is empty, smooth per-vertex normals are computed by averaging
// the face normal of every triangle each vertex belongs to.
BrickGeometry brick_geometry_from_mesh(const std::vector<Vec3>& positions,
                                        const std::vector<Vec3>& normals,
                                        const std::vector<std::pair<float, float>>& uvs,
                                        const std::vector<uint32_t>& indices);

} // namespace lu::assets
