#pragma once
// nif_to_usd.h — Export a parsed NIF model to a Universal Scene Description stage.
//
// Phase 1 of the USD content-authoring bridge (see the USD tool project notes):
// LU's Gamebryo NIF meshes -> USD, so assets can be opened in Blender/Houdini/
// usdview. This sits on top of the existing NIF reader + geometry extractor
// (gamebryo/nif) — it does not re-parse raw blocks.
//
// What is exported in Phase 1:
//   - One UsdGeomMesh per NifRenderMesh, in world space, triangulated.
//   - Per-vertex normals, first UV set (st), and authored vertex colors
//     (displayColor/displayOpacity) when present.
//   - A UsdShadeMaterial per mesh: UsdPreviewSurface driven by NiMaterialProperty
//     colors, with the diffuse texture wired through a UsdUVTexture when the NIF
//     references one. Texture file paths are preserved verbatim (asset refs).
//   - Stage metadata: Z-up (NIF/Gamebryo convention), meters, the source
//     filename, and per-mesh custom attributes carrying LU-specific data that
//     USD has no native slot for (LOD bands, skin bone bindings) so a future
//     USD->NIF importer can round-trip them.
//
// Skinned meshes: the bone bindings and per-vertex influences are preserved as
// custom metadata here; full UsdSkel skeleton + animation export lands in a
// later phase alongside KF/ETK.

#include "gamebryo/nif/nif_types.h"

#include <string>

namespace lu::assets {

struct NifToUsdOptions {
    // Emit a binary .usdc crate when true, else a text .usda stage. Inferred
    // from the output path extension by the CLI, but can be forced here.
    bool binary = false;
    // Author authored vertex colors as displayColor/displayOpacity primvars.
    bool export_vertex_colors = true;
    // Bake the NIF's Z-up world into USD's Y-up by rotating the default prim,
    // instead of just tagging the stage upAxis = Z. Off by default so exported
    // coordinates match the NIF exactly (Z-up), which the importer relies on.
    bool convert_to_y_up = false;
};

// Convert a parsed NIF into a USD stage written to out_path (.usda / .usdc).
// source_name is recorded in stage metadata for provenance. Throws NifError on
// extraction failure or on USD write failure.
void nif_to_usd(const NifFile& nif,
                const std::string& out_path,
                const std::string& source_name,
                const NifToUsdOptions& options = {});

} // namespace lu::assets
