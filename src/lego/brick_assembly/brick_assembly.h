#pragma once
// brick_assembly.h — Assemble LXFML brick models into renderable geometry.
// Loads brick .g primitives, applies bone transforms, assigns material colors.
// Shared between tooling and eventual game client.
//
// References:
//   - lu-toolbox (github.com/Squareville/lu-toolbox) — assembly pipeline and material color mapping

#include "lego/lxfml/lxfml_types.h"
#include "lego/brick_geometry/brick_geometry.h"
#include "lego/brick_assembly/brick_colors.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lu::assets {

// A single assembled brick mesh ready for rendering.
struct AssembledBrick {
    std::string label;              // "Brick <refID> (design <designID>)"
    std::vector<float> vertices;    // xyz interleaved
    std::vector<float> normals;     // xyz interleaved
    std::vector<uint32_t> indices;  // triangle indices
    BrickColor color;               // primary material color
    int brick_ref_id = 0;
    int design_id = 0;
};

// Result of assembling an entire LXFML model.
struct AssemblyResult {
    std::vector<AssembledBrick> bricks;
    uint32_t total_vertices = 0;
    uint32_t total_triangles = 0;
    uint32_t bricks_loaded = 0;
    uint32_t bricks_missing = 0;    // geometry files not found
};

// Callback to load a brick geometry file by design ID and sub-part index.
// designID = the part's design ID, partIndex = 0 for .g, 1 for .g1, etc.
// Return empty BrickGeometry (vertices.empty()) if file not found.
using BrickGeometryLoader = std::function<BrickGeometry(int designID, int partIndex)>;

// Assemble an LXFML model into renderable geometry.
// The loader callback is responsible for finding and parsing .g files.
AssemblyResult assemble_lxfml(const LxfmlFile& lxfml, const BrickGeometryLoader& loader);

} // namespace lu::assets
