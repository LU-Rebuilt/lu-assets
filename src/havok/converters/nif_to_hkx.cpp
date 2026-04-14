// nif_to_hkx.cpp — Convert NIF mesh geometry to HKX collision data.

#include "havok/converters/nif_to_hkx.h"
#include "havok/writer/hkx_writer.h"
#include "gamebryo/nif/nif_geometry.h"

#include <cmath>
#include <algorithm>
#include <filesystem>

namespace Hkx {

// Build an ExtendedMeshShape from flat vertex/triangle data
static ShapeInfo buildExtendedMeshShape(const std::vector<float>& vertices,
                                         const std::vector<uint32_t>& indices) {
    ShapeInfo shape;
    shape.type = ShapeType::ExtendedMesh;
    shape.className = "hkpStorageExtendedMeshShape";

    // Store vertices in planeEquations (matching the binary reader convention
    // where planeEquations doubles as vertex storage for mesh shapes)
    shape.planeEquations.reserve(vertices.size() / 3);
    for (size_t i = 0; i + 2 < vertices.size(); i += 3) {
        shape.planeEquations.push_back({vertices[i], vertices[i+1], vertices[i+2], 0.0f});
    }

    // Store triangles
    shape.triangles.reserve(indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        shape.triangles.push_back({indices[i], indices[i+1], indices[i+2]});
    }

    shape.numVertices = static_cast<int>(shape.planeEquations.size());
    shape.numTriangles = static_cast<int>(shape.triangles.size());

    return shape;
}

// Wrap a shape in a Mopp BvTree (the standard LU collision container)
static ShapeInfo wrapInMopp(ShapeInfo meshShape) {
    ShapeInfo mopp;
    mopp.type = ShapeType::Mopp;
    mopp.className = "hkpMoppBvTreeShape";
    mopp.children.push_back(std::move(meshShape));
    return mopp;
}

// Build a rigid body containing the shape
static RigidBodyInfo buildRigidBody(ShapeInfo shape, float friction, float restitution) {
    RigidBodyInfo rb;
    rb.shape = std::move(shape);
    rb.material.friction = friction;
    rb.material.restitution = restitution;
    rb.material.responseType = 0; // RESPONSE_SIMPLE_CONTACT
    rb.mass = 0.0f; // static
    // Identity transform — vertices are already in local space
    return rb;
}

ParseResult convertMeshToHkx(const std::vector<float>& vertices,
                              const std::vector<uint32_t>& indices,
                              const NifToHkxOptions& options) {
    ParseResult result;
    result.success = true;
    result.havokVersion = options.havokVersion;
    result.fileVersion = 7;
    result.pointerSize = 4;

    if (vertices.empty() || indices.empty()) {
        result.success = false;
        result.error = "No geometry to convert";
        return result;
    }

    // Build shape hierarchy
    auto meshShape = buildExtendedMeshShape(vertices, indices);
    ShapeInfo finalShape = options.wrapInMopp
        ? wrapInMopp(std::move(meshShape))
        : std::move(meshShape);

    // Build rigid body
    auto rb = buildRigidBody(std::move(finalShape), options.friction, options.restitution);

    // Build physics hierarchy: RB → System → Data → RootContainer
    PhysicsSystemInfo sys;
    sys.rigidBodies.push_back(std::move(rb));

    PhysicsDataInfo pd;
    pd.systems.push_back(std::move(sys));

    result.physicsData.push_back(std::move(pd));

    // Root level container
    RootLevelContainerInfo rlc;
    rlc.namedVariants.push_back({"Physics Data", "hkpPhysicsData", 0});
    result.rootContainers.push_back(std::move(rlc));

    return result;
}

ParseResult convertNifToHkx(const lu::assets::NifFile& nif,
                             const NifToHkxOptions& options) {
    // Extract all mesh geometry from the NIF
    auto extraction = lu::assets::extractNifGeometry(nif);

    // Merge all meshes into one flat vertex/triangle buffer
    std::vector<float> allVerts;
    std::vector<uint32_t> allIndices;

    for (auto& mesh : extraction.meshes) {
        uint32_t baseVertex = static_cast<uint32_t>(allVerts.size() / 3);
        allVerts.insert(allVerts.end(), mesh.vertices.begin(), mesh.vertices.end());
        for (auto idx : mesh.indices) {
            allIndices.push_back(baseVertex + idx);
        }
    }

    return convertMeshToHkx(allVerts, allIndices, options);
}

bool writeNifAsHkx(const lu::assets::NifFile& nif,
                    const std::filesystem::path& outputPath,
                    const NifToHkxOptions& options) {
    auto result = convertNifToHkx(nif, options);
    if (!result.success) return false;

    WriteOptions writeOpts;
    writeOpts.havokVersion = options.havokVersion;

    HkxWriter writer;
    return writer.Write(outputPath, result, writeOpts);
}

} // namespace Hkx
