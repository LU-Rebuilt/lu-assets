#pragma once
#include "netdevil/zone/terrain/terrain_types.h"

#include <vector>

namespace lu::assets {

// Serialize a TerrainFile back to the .raw terrain format. Byte-identical to the source
// file for any TerrainFile produced by terrain_parse: the version<32 color map is emitted
// from color_map_full (the verbatim on-disk BGRA texels, including the row/column the
// client's in-memory view crops away), and the trailing mesh LOD section is emitted from
// the typed mesh_vert_usage/mesh_vert_size/mesh_tris fields.
std::vector<uint8_t> terrain_write(const TerrainFile& terrain);

} // namespace lu::assets
