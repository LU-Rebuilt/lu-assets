#pragma once
// hkx_to_usd.h — Export Havok HKX collision geometry to USD.
//
// Phase 3b of the USD content-authoring bridge. LU HKX files hold an object's
// physics/collision shapes; this extracts their triangle geometry (via the
// existing Hkx::extractGeometry pipeline, which resolves rigid-body and scene
// node transforms to world space) and writes it as UsdGeomMesh prims under a
// "Physics" scope, tagged as guide/proxy geometry so DCC tools treat it as
// collision rather than render geometry.
//
// Fidelity bar is semantic: the same collision hull, not a byte-identical HKX.
// Primitive shapes (box/sphere/capsule) come through as their tessellated
// triangle meshes from the extractor, which is what a content author needs to
// see; the original ShapeType is preserved as lu:* metadata.

#include <string>

namespace lu::assets {

struct HkxToUsdOptions {
    // Author the meshes with purpose = "guide" (USD's convention for
    // non-render helper geometry) so viewers/DCC treat them as collision.
    bool as_guide = true;
};

// Convert an HKX file at hkx_path to a USD stage written to out_path
// (.usda / .usdc). source_name is recorded in stage metadata. Throws
// std::runtime_error on parse failure or if the file has no collision geometry.
void hkx_to_usd(const std::string& hkx_path,
                const std::string& out_path,
                const std::string& source_name,
                const HkxToUsdOptions& options = {});

} // namespace lu::assets
