#pragma once
// kf_to_usd.h — Export NIF/KF keyframe animation to a UsdSkel animation.
//
// Phase 3 of the USD content-authoring bridge: bring LU's Gamebryo KF animation
// clips into USD so they can be inspected and edited in Blender/Houdini/Maya
// (which import UsdSkel as skeletal animation) or previewed in usdview.
//
// LU KF files parse through the same NIF container as models (nif_parse). Each
// file holds one or more NiControllerSequence clips; each clip targets scene
// nodes *by name* with per-node NiTransformController -> NiTransformInterpolator
// -> NiTransformData keyframe channels (translation / rotation / scale, each
// with their own key times). Most LU models are rigid node hierarchies rather
// than skinned meshes, but UsdSkel represents both uniformly, so we map:
//
//   NIF node targets  -> UsdSkelSkeleton joints (a flat joint set — a KF clip
//                        carries target names, not the bind hierarchy)
//   each KF clip       -> a UsdSkelAnimation with per-joint translate/rotate/
//                        scale time-samples, sampled at the union of key times
//   clip timing        -> stage start/end time codes and timeCodesPerSecond
//
// Fidelity bar is semantic (the same motion plays back), not byte-identity.
// Rotation EULER channels are converted to quaternions; QUADRATIC/TBC keys are
// sampled at their key times (tangents are not re-authored as USD has no direct
// equivalent for Gamebryo's TBC form).

#include "gamebryo/nif/nif_types.h"

#include <string>

namespace lu::assets {

struct KfToUsdOptions {
    // USD time codes per second. LU KF key times are in seconds; 30 gives a
    // conventional frame rate for DCC import without changing the timing.
    double time_codes_per_second = 30.0;
    // When >= 0, export only the clip at this index; otherwise export every clip
    // (each as its own UsdSkelAnimation under the skeleton).
    int clip_index = -1;
};

// Convert the animation clips in a parsed NIF/KF file to a USD stage at out_path
// (.usda / .usdc). source_name is recorded in stage metadata. Throws NifError if
// the file has no animation clips or on USD write failure.
void kf_to_usd(const NifFile& kf,
               const std::string& out_path,
               const std::string& source_name,
               const KfToUsdOptions& options = {});

} // namespace lu::assets
