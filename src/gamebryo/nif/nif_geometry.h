#pragma once
// nif_geometry.h — Extract world-space triangle meshes from parsed NIF data.
//
// Shared between NIF viewer (for rendering) and navmesh tool (for collision fallback).

#include "gamebryo/nif/nif_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lu::assets {

struct NifExtractedMesh {
    std::vector<float> vertices;  // xyz interleaved, world space
    std::vector<float> normals;   // xyz interleaved
    std::vector<uint32_t> indices;
    std::string name;
};

struct NifExtractionResult {
    std::vector<NifExtractedMesh> meshes;
    uint32_t total_vertices = 0;
    uint32_t total_triangles = 0;
};

// Extract all meshes from a parsed NIF file.
// pos/scale apply a world transform (for navmesh object placement).
NifExtractionResult extractNifGeometry(const NifFile& nif,
                                        float pos_x = 0, float pos_y = 0, float pos_z = 0,
                                        float scale = 1.0f);

} // namespace lu::assets
