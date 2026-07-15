#pragma once
// object_to_usd.h — Compose a whole LU object into a single USD stage.
//
// Phase 4 of the USD content-authoring bridge: gather everything that makes up
// one object — its render model (NIF geometry + materials + textures), its full
// animation set (every referenced KF clip), and its collision (HKX) — into one
// composed USD stage, so a content author opens the object as a unit rather than
// juggling separate files.
//
// Two entry points produce the same composed stage shape:
//   object_from_kfm(kfm_path, ...) — driven by a KFM animation manifest, which
//       names the model NIF, the scene root, and every animation sequence/clip.
//   object_from_lot(lot, ...)      — driven by a CDClient LOT. Its render
//       component's asset may be a NIF model OR a KFM (animated actors like the
//       minifig point at a KFM); when it is a KFM, the model NIF it names and
//       its whole animation set are pulled in. Collision comes from the object's
//       physics component(s).
//
// Composed layout:
//   /object                  (SkelRoot)
//     Model/                 model geometry + materials  (author_nif_model)
//     Skeleton               one flat skeleton over the animated joints
//     anim_<clip> …          one UsdSkelAnimation per KF clip (author_kf_animations)
//     Collision/
//       <PhysicsType>/       collision meshes (guide), one sub-scope per physics
//                            component (author_hkx_collision)
//
// Preserved as lu:* custom metadata on each SkelAnimation (USD has no native
// slot for these):
//   - the KFM transition graph (blend/morph/chain edges between clips), and
//   - a sibling ETK's NiExtraTextKeyExtraData event markers (time -> event).
//
// Physics: an object may carry more than one physics component (a phantom
// trigger volume alongside a solid body); each of the physics-family component
// types resolves its physics_asset HKX and gets its own labelled sub-scope.
//
// File resolution goes through a Vfs (packed or unpacked client). References are
// normalized (backslashes, collapsed slash runs, "."/".." resolved relative to
// the referring KFM's directory); .hkx physics paths are additionally tried
// under res/physics/, where CDClient roots them.

#include <string>

namespace lu::assets {

class Vfs;
class CdClient;

struct ObjectToUsdOptions {
    double time_codes_per_second = 30.0;
    bool include_collision = true;
    bool include_animation = true;
};

// Compose from a KFM manifest. kfm_path is a client-relative path resolved via
// vfs (e.g. "res/animations/foo.kfm"). Throws on resolution/parse failure.
void object_from_kfm(const std::string& kfm_path,
                     const Vfs& vfs,
                     const std::string& out_path,
                     const ObjectToUsdOptions& options = {});

// Compose from a CDClient LOT. Resolves the render asset (a NIF model or a KFM
// manifest), the animation set, and the physics asset(s) (HKX) via the database,
// then reads each through vfs. Throws if the LOT has no render asset.
void object_from_lot(int32_t lot,
                     const CdClient& cd,
                     const Vfs& vfs,
                     const std::string& out_path,
                     const ObjectToUsdOptions& options = {});

} // namespace lu::assets
