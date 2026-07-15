#include "usd/kf_to_usd.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/animation.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/tf/token.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace lu::assets {

namespace {

// Build lookups from block_index -> the parsed interpolator / transform data,
// since NIF refs are block indices, not vector positions.
struct AnimIndex {
    std::map<uint32_t, const NifTransformInterpolator*> interp;
    std::map<uint32_t, const NifTransformData*> data;

    explicit AnimIndex(const NifFile& kf) {
        for (const auto& it : kf.transform_interpolators) interp[it.block_index] = &it;
        for (const auto& td : kf.transform_data) data[td.block_index] = &td;
    }
};

// Sanitize a node name into a valid USD path segment (UsdSkel joint tokens are
// path-like; each segment must be identifier-ish).
std::string sanitize(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_');
    }
    if (out.empty()) out = "joint";
    if (std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    return out;
}

// Linear interpolation of a Vec3 translation channel at time t.
GfVec3f sample_translation(const NifTransformData& td, float t, const GfVec3f& fallback) {
    const auto& keys = td.translation_keys;
    if (keys.empty()) return fallback;
    if (t <= keys.front().time) return GfVec3f(keys.front().value.x, keys.front().value.y, keys.front().value.z);
    if (t >= keys.back().time) return GfVec3f(keys.back().value.x, keys.back().value.y, keys.back().value.z);
    for (size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].time) {
            const auto& a = keys[i - 1];
            const auto& b = keys[i];
            float span = b.time - a.time;
            float f = span > 1e-8f ? (t - a.time) / span : 0.0f;
            return GfVec3f(a.value.x + (b.value.x - a.value.x) * f,
                           a.value.y + (b.value.y - a.value.y) * f,
                           a.value.z + (b.value.z - a.value.z) * f);
        }
    }
    return fallback;
}

// Nlerp of quaternion rotation keys at time t (USD orient is w,x,y,z order).
GfQuatf sample_rotation(const NifTransformData& td, float t, const GfQuatf& fallback) {
    const auto& keys = td.rotation_keys;
    if (keys.empty()) return fallback;
    auto to_gf = [](const Quat& q) { return GfQuatf(q.w, q.x, q.y, q.z); };
    if (t <= keys.front().time) return to_gf(keys.front().value);
    if (t >= keys.back().time) return to_gf(keys.back().value);
    for (size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].time) {
            const auto& a = keys[i - 1];
            const auto& b = keys[i];
            float span = b.time - a.time;
            float f = span > 1e-8f ? (t - a.time) / span : 0.0f;
            GfQuatf qa = to_gf(a.value), qb = to_gf(b.value);
            // Shortest-arc nlerp.
            if (GfDot(qa, qb) < 0.0f) qb = -qb;
            GfQuatf r(qa.GetReal() + (qb.GetReal() - qa.GetReal()) * f,
                      qa.GetImaginary() + (qb.GetImaginary() - qa.GetImaginary()) * f);
            r.Normalize();
            return r;
        }
    }
    return fallback;
}

float sample_scale(const NifTransformData& td, float t, float fallback) {
    const auto& keys = td.scale_keys;
    if (keys.empty()) return fallback;
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;
    for (size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].time) {
            const auto& a = keys[i - 1];
            const auto& b = keys[i];
            float span = b.time - a.time;
            float f = span > 1e-8f ? (t - a.time) / span : 0.0f;
            return a.value + (b.value - a.value) * f;
        }
    }
    return fallback;
}

// Author one KF clip as a UsdSkelAnimation. Only the joints this clip actually
// drives are listed in the animation's `joints` attr — UsdSkel keeps every other
// joint at its skeleton rest transform, so non-animated joints (SceneNode, LOD
// group, shape nodes) don't get stomped to identity, which would collapse the
// parent-relative hierarchy.
void author_clip(const UsdStagePtr& stage, const SdfPath& skel_root_path,
                 const NifControllerSequence& seq, const AnimIndex& idx,
                 const std::vector<std::string>& joint_paths,
                 const std::map<std::string, int>& joint_lookup,
                 double tcps) {
    const SdfPath anim_path = skel_root_path.AppendChild(TfToken("anim_" + sanitize(seq.name)));
    UsdSkelAnimation anim = UsdSkelAnimation::Define(stage, anim_path);

    // Resolve each controlled block to (skeleton joint path, interpolator, data),
    // keeping only blocks whose target node is a joint in the skeleton.
    struct Chan {
        std::string joint_path;
        const NifTransformInterpolator* interp = nullptr;
        const NifTransformData* data = nullptr;
    };
    std::vector<Chan> channels;
    std::set<float> times;
    times.insert(seq.start_time);
    times.insert(seq.stop_time);
    for (const auto& cb : seq.controlled_blocks) {
        auto jit = joint_lookup.find(cb.node_name);
        if (jit == joint_lookup.end()) continue;
        auto it = idx.interp.find(static_cast<uint32_t>(cb.interpolator_ref));
        if (it == idx.interp.end()) continue;
        Chan ch;
        ch.joint_path = joint_paths[jit->second];
        ch.interp = it->second;
        auto dit = idx.data.find(static_cast<uint32_t>(it->second->data_ref));
        if (dit != idx.data.end()) {
            ch.data = dit->second;
            for (const auto& k : ch.data->translation_keys) times.insert(k.time);
            for (const auto& k : ch.data->rotation_keys) times.insert(k.time);
            for (const auto& k : ch.data->scale_keys) times.insert(k.time);
        }
        channels.push_back(ch);
    }
    if (channels.empty()) return;

    // The animation drives exactly these joints, in this order.
    VtArray<TfToken> joint_tokens;
    joint_tokens.reserve(channels.size());
    for (const auto& c : channels) joint_tokens.push_back(TfToken(c.joint_path));
    anim.CreateJointsAttr(VtValue(joint_tokens));

    UsdAttribute tAttr = anim.CreateTranslationsAttr();
    UsdAttribute rAttr = anim.CreateRotationsAttr();
    UsdAttribute sAttr = anim.CreateScalesAttr();

    // NiTransformInterpolator uses +/-FLT_MAX as a "channel unset — use identity"
    // sentinel (a bone with no scale key stores scale = -FLT_MAX, etc.). Passing
    // those through overflows the half-precision scale to +/-inf and makes the
    // matrix non-decomposable (the whole rig then imports broken). Sanitize.
    auto finite_or = [](float v, float fallback) {
        return (std::isfinite(v) && std::abs(v) < 1e30f) ? v : fallback;
    };

    for (float t : times) {
        VtArray<GfVec3f> trans(channels.size(), GfVec3f(0));
        VtArray<GfQuatf> rots(channels.size(), GfQuatf(1, 0, 0, 0));
        VtArray<GfVec3h> scales(channels.size(), GfVec3h(1));
        for (size_t j = 0; j < channels.size(); ++j) {
            const Chan& c = channels[j];
            GfVec3f defT(finite_or(c.interp->translation.x, 0.0f),
                         finite_or(c.interp->translation.y, 0.0f),
                         finite_or(c.interp->translation.z, 0.0f));
            GfQuatf defR(finite_or(c.interp->rotation.w, 1.0f),
                         finite_or(c.interp->rotation.x, 0.0f),
                         finite_or(c.interp->rotation.y, 0.0f),
                         finite_or(c.interp->rotation.z, 0.0f));
            float defS = finite_or(c.interp->scale, 1.0f);
            if (c.data) {
                GfVec3f st = sample_translation(*c.data, t, defT);
                trans[j] = GfVec3f(finite_or(st[0], 0.0f), finite_or(st[1], 0.0f),
                                   finite_or(st[2], 0.0f));
                GfQuatf sr = sample_rotation(*c.data, t, defR);
                GfQuatf clean(finite_or(sr.GetReal(), 1.0f),
                              GfVec3f(finite_or(sr.GetImaginary()[0], 0.0f),
                                      finite_or(sr.GetImaginary()[1], 0.0f),
                                      finite_or(sr.GetImaginary()[2], 0.0f)));
                if (clean.GetLength() < 1e-6f) clean = GfQuatf(1, 0, 0, 0);
                clean.Normalize();
                rots[j] = clean;
                scales[j] = GfVec3h(finite_or(sample_scale(*c.data, t, defS), 1.0f));
            } else {
                trans[j] = defT;
                rots[j] = defR;
                scales[j] = GfVec3h(defS);
            }
        }
        const UsdTimeCode tc(static_cast<double>(t) * tcps);
        tAttr.Set(trans, tc);
        rAttr.Set(rots, tc);
        sAttr.Set(scales, tc);
    }
}

} // namespace

// Collect the union of joint names targeted by a KF's (selected) clips, in
// first-seen order. Used to build a skeleton — either per-file or, for object
// composition, unified across every KF plus the model's skin bones.
std::vector<std::string> collect_kf_joints(const NifFile& kf, int clip_index) {
    std::vector<std::string> joint_names;
    std::set<std::string> seen;
    for (size_t i = 0; i < kf.sequences.size(); ++i) {
        if (clip_index >= 0 && clip_index != static_cast<int>(i)) continue;
        for (const auto& cb : kf.sequences[i].controlled_blocks) {
            if (seen.insert(cb.node_name).second) joint_names.push_back(cb.node_name);
        }
    }
    return joint_names;
}

// Author a flat UsdSkelSkeleton at skel_path from joint_names (sanitized,
// unique). Returns the sanitized joint tokens in the same order — this is the
// joint ordering every animation clip must reference. Used by the standalone
// kf_to_usd path, which has no model hierarchy to draw on (identity bind pose).
std::vector<std::string> author_skeleton(const UsdStagePtr& stage,
                                         const SdfPath& skel_path,
                                         const std::vector<std::string>& joint_names,
                                         const std::vector<GfMatrix4d>& bind_transforms) {
    UsdSkelSkeleton skeleton = UsdSkelSkeleton::Define(stage, skel_path);
    VtArray<TfToken> joints;
    std::vector<std::string> joint_paths;
    std::set<std::string> used;
    for (const auto& n : joint_names) {
        std::string base = sanitize(n);
        std::string name = base;
        int suffix = 1;
        while (!used.insert(name).second) name = base + "_" + std::to_string(suffix++);
        joints.push_back(TfToken(name));
        joint_paths.push_back(name);
    }
    skeleton.CreateJointsAttr(VtValue(joints));

    VtArray<GfMatrix4d> binds(joint_names.size(), GfMatrix4d(1.0));
    if (bind_transforms.size() == joint_names.size()) {
        for (size_t i = 0; i < bind_transforms.size(); ++i) binds[i] = bind_transforms[i];
    }
    skeleton.CreateBindTransformsAttr(VtValue(binds));
    skeleton.CreateRestTransformsAttr(VtValue(binds));
    return joint_paths;
}

// Author a UsdSkelSkeleton whose joints carry the real bone hierarchy: joint
// paths ("parent/child/…") encode parenting, so UsdSkel composes the
// parent-relative animation transforms correctly. local_bind holds each joint's
// parent-relative bind transform (world bindTransforms are then computed by USD
// from the topology). bindTransforms must still be WORLD space, so we accumulate
// them down the path here.
void author_skeleton_hierarchical(const UsdStagePtr& stage, const SdfPath& skel_path,
                                  const std::vector<std::string>& joint_paths,
                                  const std::vector<GfMatrix4d>& local_bind) {
    UsdSkelSkeleton skeleton = UsdSkelSkeleton::Define(stage, skel_path);

    VtArray<TfToken> joints;
    for (const auto& p : joint_paths) joints.push_back(TfToken(p));
    skeleton.CreateJointsAttr(VtValue(joints));

    // Rest transforms are the joint-local bind transforms.
    VtArray<GfMatrix4d> rests(local_bind.begin(), local_bind.end());
    skeleton.CreateRestTransformsAttr(VtValue(rests));

    // World bind transforms: for each joint, multiply local transforms down its
    // path. Index joints by path for parent lookup.
    std::map<std::string, size_t> path_index;
    for (size_t i = 0; i < joint_paths.size(); ++i) path_index[joint_paths[i]] = i;
    VtArray<GfMatrix4d> world(joint_paths.size(), GfMatrix4d(1.0));
    for (size_t i = 0; i < joint_paths.size(); ++i) {
        const std::string& path = joint_paths[i];
        size_t slash = path.find_last_of('/');
        if (slash == std::string::npos) {
            world[i] = local_bind[i]; // root joint
        } else {
            std::string parent = path.substr(0, slash);
            auto it = path_index.find(parent);
            // USD row-vector convention: child_world = child_local * parent_world.
            world[i] = it != path_index.end()
                ? local_bind[i] * world[it->second]
                : local_bind[i];
        }
    }
    skeleton.CreateBindTransformsAttr(VtValue(world));
}

// Author a KF's clips as UsdSkelAnimations plus a skeleton under root_path
// (which must be, or become, a SkelRoot). Sets the stage time range to cover
// the clips. Returns the number of clips authored. Shared by kf_to_usd (own
// stage) and the object-composition layer.
int author_kf_animations(const UsdStagePtr& stage, const SdfPath& root_path,
                         const NifFile& kf, const KfToUsdOptions& options) {
    if (kf.sequences.empty()) {
        throw NifError("kf_to_usd: file has no animation clips (NiControllerSequence)");
    }

    std::vector<std::string> joint_names = collect_kf_joints(kf, options.clip_index);
    if (joint_names.empty()) {
        throw NifError("kf_to_usd: no animated nodes in the selected clip(s)");
    }

    const SdfPath skel_path = root_path.AppendChild(TfToken("Skeleton"));
    // Standalone KF export has no model, so no bind pose — identity skeleton.
    std::vector<std::string> joint_paths = author_skeleton(stage, skel_path, joint_names, {});

    // The animation samples index into the same joint order the skeleton uses.
    std::map<std::string, int> joint_lookup;
    for (size_t i = 0; i < joint_names.size(); ++i) joint_lookup[joint_names[i]] = static_cast<int>(i);

    AnimIndex idx(kf);
    auto want_clip = [&](int i) { return options.clip_index < 0 || options.clip_index == i; };
    double min_t = 1e30, max_t = -1e30;
    for (size_t i = 0; i < kf.sequences.size(); ++i) {
        if (!want_clip(static_cast<int>(i))) continue;
        const NifControllerSequence& seq = kf.sequences[i];
        author_clip(stage, root_path, seq, idx, joint_paths, joint_lookup,
                    options.time_codes_per_second);
        min_t = std::min(min_t, static_cast<double>(seq.start_time));
        max_t = std::max(max_t, static_cast<double>(seq.stop_time));
    }

    // Bind the first authored clip on the skeleton so usdview plays it by default.
    for (size_t i = 0; i < kf.sequences.size(); ++i) {
        if (!want_clip(static_cast<int>(i))) continue;
        const SdfPath anim_path =
            root_path.AppendChild(TfToken("anim_" + sanitize(kf.sequences[i].name)));
        UsdSkelBindingAPI binding = UsdSkelBindingAPI::Apply(
            stage->GetPrimAtPath(skel_path));
        binding.CreateAnimationSourceRel().SetTargets({anim_path});
        break;
    }

    if (min_t <= max_t) {
        stage->SetStartTimeCode(min_t * options.time_codes_per_second);
        stage->SetEndTimeCode(max_t * options.time_codes_per_second);
    }

    int authored = 0;
    for (size_t i = 0; i < kf.sequences.size(); ++i) {
        if (want_clip(static_cast<int>(i))) ++authored;
    }
    return authored;
}

// Author only a KF's clips (no skeleton) against a pre-built shared skeleton,
// using the supplied joint ordering. For object composition, where one unified
// skeleton is shared by every KF and the skinned model. Returns [min,max] clip
// time (seconds) via out params accumulated by the caller.
int author_kf_clips_shared(const UsdStagePtr& stage, const SdfPath& root_path,
                           const NifFile& kf,
                           const std::vector<std::string>& joint_paths,
                           const std::map<std::string, int>& joint_lookup,
                           double tcps, double& min_t, double& max_t) {
    if (kf.sequences.empty()) return 0;
    AnimIndex idx(kf);
    int authored = 0;
    for (const auto& seq : kf.sequences) {
        author_clip(stage, root_path, seq, idx, joint_paths, joint_lookup, tcps);
        min_t = std::min(min_t, static_cast<double>(seq.start_time));
        max_t = std::max(max_t, static_cast<double>(seq.stop_time));
        ++authored;
    }
    return authored;
}

void kf_to_usd(const NifFile& kf,
               const std::string& out_path,
               const std::string& source_name,
               const KfToUsdOptions& options) {
    UsdStageRefPtr stage = UsdStage::CreateNew(out_path);
    if (!stage) throw NifError("kf_to_usd: failed to create USD stage at " + out_path);

    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y); // LU/Gamebryo geometry is Y-up
    UsdGeomSetStageMetersPerUnit(stage, 1.0);
    stage->SetTimeCodesPerSecond(options.time_codes_per_second);
    stage->SetMetadata(TfToken("comment"), VtValue("Exported from LU KF: " + source_name));

    const SdfPath root_path("/anim");
    UsdSkelRoot skel_root = UsdSkelRoot::Define(stage, root_path);
    stage->SetDefaultPrim(skel_root.GetPrim());

    author_kf_animations(stage, root_path, kf, options);

    stage->GetRootLayer()->Save();
}

} // namespace lu::assets
