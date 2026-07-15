#pragma once
// usd_compose_internal.h — Internal author-into-stage helpers shared by the
// per-file USD exporters and the object-composition layer (object_to_usd).
//
// The public *_to_usd entry points create their own stage; these helpers author
// the same content under a caller-supplied path on an existing stage, so a
// single composed stage can hold a model's geometry + animation + collision
// together. Not part of the public API.

#include "gamebryo/nif/nif_types.h"
#include "usd/nif_to_usd.h"
#include "usd/kf_to_usd.h"
#include "usd/hkx_to_usd.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/gf/matrix4d.h>

#include <map>
#include <string>
#include <vector>

namespace lu::assets {

// Author a NIF's render meshes/materials under parent_path on `stage`.
// parent_path must already exist (or be definable). Returns the mesh count.
//
// When skel_path is non-empty, skinned meshes are bound to the skeleton at that
// path via UsdSkelBindingAPI: skel:joints + primvars:skel:jointIndices/Weights
// are authored from the NIF skin influences, and the mesh is made a target of
// the skeleton so it deforms with the animation. skel_joints is the skeleton's
// joint-token ordering (the mesh's per-vertex bone indices are remapped onto it
// by joint name). Without a skel_path, meshes are authored unskinned (static).
int author_nif_model(const PXR_NS::UsdStagePtr& stage,
                     const PXR_NS::SdfPath& parent_path,
                     const NifFile& nif,
                     const NifToUsdOptions& options = {},
                     const PXR_NS::SdfPath& skel_path = {},
                     const std::vector<std::string>& skel_joints = {});

// Author a KF file's clips as UsdSkelAnimations plus a skeleton under
// parent_path (a SkelRoot). Returns the number of clips authored.
int author_kf_animations(const PXR_NS::UsdStagePtr& stage,
                         const PXR_NS::SdfPath& parent_path,
                         const NifFile& kf,
                         const KfToUsdOptions& options = {});

// --- Object-composition helpers (one shared skeleton across many KFs + model) ---

// Union of joint names a KF's clips target, first-seen order (clip_index < 0 = all).
std::vector<std::string> collect_kf_joints(const NifFile& kf, int clip_index = -1);

// Define a flat UsdSkelSkeleton at skel_path from joint_names; returns the
// sanitized joint tokens (the ordering clips and skinned meshes must index into).
// bind_transforms (same length as joint_names, or empty for identity) gives each
// joint's world-space bind/rest transform — required for skinned meshes to
// deform correctly rather than collapse.
std::vector<std::string> author_skeleton(const PXR_NS::UsdStagePtr& stage,
                                         const PXR_NS::SdfPath& skel_path,
                                         const std::vector<std::string>& joint_names,
                                         const std::vector<PXR_NS::GfMatrix4d>& bind_transforms);

// Define a UsdSkelSkeleton with a real bone hierarchy: joint_paths encode
// parenting ("parent/child/…"), local_bind holds each joint's parent-relative
// bind transform. Required for LU's parent-relative animation to compose
// correctly (a flat skeleton scrambles the pose).
void author_skeleton_hierarchical(const PXR_NS::UsdStagePtr& stage,
                                  const PXR_NS::SdfPath& skel_path,
                                  const std::vector<std::string>& joint_paths,
                                  const std::vector<PXR_NS::GfMatrix4d>& local_bind);

// Author only a KF's clips (no skeleton) against a pre-built shared skeleton.
// Accumulates the clip time range (seconds) into min_t/max_t. max_clips caps how
// many clips from this file are authored (0 = no limit). Returns clips authored.
int author_kf_clips_shared(const PXR_NS::UsdStagePtr& stage,
                           const PXR_NS::SdfPath& parent_path,
                           const NifFile& kf,
                           const std::vector<std::string>& joint_paths,
                           const std::map<std::string, int>& joint_lookup,
                           double tcps, double& min_t, double& max_t,
                           int max_clips = 0);

// Author an HKX file's collision meshes under parent_path. Declared in
// hkx_to_usd.cpp. Returns the mesh count.
int author_hkx_collision(const PXR_NS::UsdStagePtr& stage,
                         const PXR_NS::SdfPath& parent_path,
                         const std::string& hkx_path,
                         const HkxToUsdOptions& options = {});

} // namespace lu::assets
