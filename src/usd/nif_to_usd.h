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
//   - A "LOD" variant set on the model prim when NiLODNodes band the meshes
//     into more than one level: variant "LOD<level>" leaves only that level's
//     meshes visible, defaulting to the highest detail (lowest level).
//
// Skinned meshes: this exporter preserves the bone bindings and per-vertex
// influences as custom metadata on the mesh. Animation itself is exported
// separately (kf_to_usd -> UsdSkel), and object_to_usd composes a model with its
// animation set into one stage.

#include "gamebryo/nif/nif_types.h"

#include <string>

namespace lu::assets {

struct NifToUsdOptions {
    // Emit a binary .usdc crate when true, else a text .usda stage. Inferred
    // from the output path extension by the CLI, but can be forced here.
    bool binary = false;
    // Author authored vertex colors as displayColor/displayOpacity primvars.
    bool export_vertex_colors = true;
    // LU NIF geometry is Y-up (a minifig stands along +Y), so the stage is
    // authored upAxis = Y by default and the model imports upright. Set this to
    // bake a +90° X rotation and tag the stage Z-up instead, for the rare
    // consumer that requires Z-up.
    bool convert_to_z_up = false;
};

// Convert a parsed NIF into a USD stage written to out_path (.usda / .usdc).
// source_name is recorded in stage metadata for provenance. Throws NifError on
// extraction failure or on USD write failure.
void nif_to_usd(const NifFile& nif,
                const std::string& out_path,
                const std::string& source_name,
                const NifToUsdOptions& options = {});

} // namespace lu::assets
