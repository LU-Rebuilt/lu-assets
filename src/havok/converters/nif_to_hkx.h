#pragma once
// nif_to_hkx.h — Convert NIF mesh geometry to HKX collision data.
//
// Takes parsed NIF geometry and produces a ParseResult suitable for
// HkxWriter, containing a rigid body with an ExtendedMeshShape.

#include "havok/types/hkx_types.h"
#include "gamebryo/nif/nif_types.h"

#include <filesystem>
#include <vector>

namespace Hkx {

struct NifToHkxOptions {
    float friction = 0.5f;
    float restitution = 0.4f;
    bool wrapInMopp = true;        // Wrap mesh in hkpMoppBvTreeShape (standard for LU)
    std::string havokVersion = "Havok-7.1.0-r1";
};

// Convert NIF mesh data to an HKX ParseResult.
// The result contains a single rigid body with the mesh as collision.
ParseResult convertNifToHkx(const lu::assets::NifFile& nif,
                             const NifToHkxOptions& options = {});

// Convert NIF and write directly to an HKX file.
bool writeNifAsHkx(const lu::assets::NifFile& nif,
                    const std::filesystem::path& outputPath,
                    const NifToHkxOptions& options = {});

// Convert raw vertex/triangle data to HKX ParseResult.
// Useful for arbitrary mesh data (not just NIF).
ParseResult convertMeshToHkx(const std::vector<float>& vertices,    // xyz interleaved
                              const std::vector<uint32_t>& indices,  // triangle indices
                              const NifToHkxOptions& options = {});

} // namespace Hkx
