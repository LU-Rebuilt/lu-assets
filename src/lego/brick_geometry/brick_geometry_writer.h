#pragma once
#include "lego/brick_geometry/brick_geometry_types.h"

#include <cstdint>
#include <vector>

namespace lu::assets {

// Serialize a BrickGeometry back to the .g binary format. Byte-identical round-trip:
// the format has no padding/alignment and every field the reader consumes is stored on
// the struct (block sizes come from vector sizes, e.g. skin_indices.weight_group_indices
// .size() standing in for the on-disk count field), so
// brick_geometry_write(brick_geometry_parse(data)) == data — verified against all real
// .g/.g1/.g2 files (11784/11784 across two client trees).
std::vector<uint8_t> brick_geometry_write(const BrickGeometry& geom);

} // namespace lu::assets
