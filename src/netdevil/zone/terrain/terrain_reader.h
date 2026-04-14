#pragma once
#include "netdevil/zone/terrain/terrain_types.h"

namespace lu::assets {

TerrainFile terrain_parse(std::span<const uint8_t> data);

// Generate a triangle mesh from a parsed terrain file.
// Vertex positions match DarkflameServer terrain math:
//   x = (col + offset_x/scale) * scale
//   y = heightmap_value
//   z = (row + offset_z/scale) * scale
struct TerrainMesh {
    std::vector<float> vertices;     // xyz interleaved
    std::vector<uint32_t> indices;   // triangle indices
    uint32_t triangle_count = 0;
};

TerrainMesh terrain_generate_mesh(const TerrainFile& terrain);

// Generate per-vertex colors for a terrain mesh.
// Each vertex gets an RGB triplet (3 floats) matching the vertex order from terrain_generate_mesh.
enum class TerrainColorMode {
    HeightGradient,   // Blue(low) → Green(mid) → Red(high)
    ColorMap,         // Baked RGBA color map sampled per vertex
    SceneMap,         // Scene ID → distinct color per vertex (official 146-color palette)
    TextureWeights,   // Texture layer weights as RGBA visualization
};

struct TerrainVertexColors {
    std::vector<float> colors;   // rgb interleaved, one per vertex
};

TerrainVertexColors terrain_generate_colors(const TerrainFile& terrain, TerrainColorMode mode);

} // namespace lu::assets
