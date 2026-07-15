#pragma once
// usd_to_nif.h — Build a Gamebryo NIF model from a Universal Scene Description stage.
//
// Phase 2 of the USD content-authoring bridge (the inverse of nif_to_usd):
// import a USD stage back into a NIF the LU client accepts and renders.
//
// Fidelity bar: unlike the byte-perfect round-trip work elsewhere in this
// library, USD->NIF cannot be byte-identical — USD does not carry Gamebryo's
// exact raw-block layout, block ordering, or the many optional fields an
// original exporter wrote. Success here means "produces a valid NIF whose
// geometry/materials the client loads and draws the same," not byte-for-byte.
//
// What is imported in Phase 2:
//   - Every UsdGeomMesh on the stage becomes a NiTriShape + NiTriShapeData,
//     triangulated, with positions, normals, the primary UV set (st) and
//     authored vertex colors (displayColor/displayOpacity) when present.
//   - A NiMaterialProperty per mesh from the bound UsdPreviewSurface's
//     constant inputs; when the surface's diffuse is textured, a
//     NiTexturingProperty + NiSourceTexture carry the texture file path.
//   - A single root NiNode parents every shape. Per-mesh local transforms are
//     baked into vertices on export, so shapes use identity transforms here.
//
// Not yet handled (later phases): skeletons/skinning (UsdSkel), animation,
// collision, LOD nodes, and multi-level scene-graph hierarchy — a flat
// root-node scene is emitted. The lu:* custom attributes nif_to_usd writes are
// consulted where present to better reconstruct LOD/material provenance.

#include "gamebryo/nif/nif_types.h"

#include <string>

namespace lu::assets {

struct UsdToNifOptions {
    // Target NIF version. Defaults to LU's shipping version (20.3.0.9, user
    // version 12 / user version 2 = 34). Only LU-range versions are supported.
    uint32_t version = 0x14030009;
    uint32_t user_version = 12;
    uint32_t user_version_2 = 34;
    // When the stage is authored Y-up, rotate geometry back into NIF's Z-up so
    // the imported model sits upright in the client. Auto-detected from stage
    // upAxis metadata by default; this forces it on.
    bool force_z_up_from_y = false;
};

// Build a NifFile from the USD stage at usd_path. Throws NifError on read or
// construction failure. The returned NifFile can be serialized with nif_write.
NifFile usd_to_nif(const std::string& usd_path, const UsdToNifOptions& options = {});

} // namespace lu::assets
