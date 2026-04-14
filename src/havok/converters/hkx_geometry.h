#pragma once
// hkx_geometry.h — Extract world-space triangle meshes from parsed HKX data.
//
// Produces flat vertex/index arrays that can be converted to RenderMesh or ObjectGeo.

#include "havok/types/hkx_types.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Hkx {

// A single extracted mesh with world-space vertices.
struct ExtractedMesh {
    std::vector<float> vertices;     // xyz interleaved, world space
    std::vector<float> normals;      // xyz interleaved (optional)
    std::vector<uint32_t> indices;   // triangle indices
    ShapeType shapeType = ShapeType::Unknown;
    bool isSceneMesh = false;
    std::string label;
};

// Extract all renderable geometry from a ParseResult.
// Produces one ExtractedMesh per shape/scene mesh, with all vertices
// transformed to world space using rigid body and node hierarchy transforms.
struct ExtractionResult {
    std::vector<ExtractedMesh> meshes;
    int rigidBodyCount = 0;
    int shapeCount = 0;
    int sceneMeshCount = 0;
};

ExtractionResult extractGeometry(const ParseResult& result);

// Extract collision triangles only (for navmesh — no scene meshes, no primitives).
// Uses the same transform pipeline but outputs flat triangle data.
struct CollisionMesh {
    std::vector<float> vertices;     // xyz interleaved
    std::vector<uint32_t> indices;   // triangle indices
};

CollisionMesh extractCollision(const ParseResult& result,
                                const Transform& worldTransform = Transform{});

} // namespace Hkx
