#pragma once
#include "lego/brick_geometry/brick_geometry.h"

namespace lu::assets {

// Serialize a BrickGeometry back to the .g binary format. Byte-identical round-trip:
// the format has no padding/alignment and every field the reader consumes is stored on
// the struct (including bone_mapping.size() standing in for the on-disk bone_length
// field), so brick_geometry_write(brick_geometry_parse(data)) == data.
std::vector<uint8_t> brick_geometry_write(const BrickGeometry& geom);

} // namespace lu::assets
