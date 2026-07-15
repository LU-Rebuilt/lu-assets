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

// Author one KF clip as a UsdSkelAnimation, sampling every joint's TRS at the
// union of all key times in the clip.
void author_clip(const UsdStagePtr& stage, const SdfPath& skel_root_path,
                 const NifControllerSequence& seq, const AnimIndex& idx,
                 const std::vector<std::string>& joint_paths,
                 const std::map<std::string, int>& joint_lookup,
                 double tcps) {
    const SdfPath anim_path = skel_root_path.AppendChild(TfToken("anim_" + sanitize(seq.name)));
    UsdSkelAnimation anim = UsdSkelAnimation::Define(stage, anim_path);

    // joints attribute: the order the *Anim samples are in.
    VtArray<TfToken> joint_tokens;
    joint_tokens.reserve(joint_paths.size());
    for (const auto& jp : joint_paths) joint_tokens.push_back(TfToken(jp));
    anim.CreateJointsAttr(VtValue(joint_tokens));

    // Collect the union of key times across every controlled block's data.
    std::set<float> times;
    times.insert(seq.start_time);
    times.insert(seq.stop_time);
    for (const auto& cb : seq.controlled_blocks) {
        auto it = idx.interp.find(static_cast<uint32_t>(cb.interpolator_ref));
        if (it == idx.interp.end()) continue;
        const NifTransformInterpolator* interp = it->second;
        auto dit = idx.data.find(static_cast<uint32_t>(interp->data_ref));
        if (dit == idx.data.end()) continue;
        const NifTransformData* td = dit->second;
        for (const auto& k : td->translation_keys) times.insert(k.time);
        for (const auto& k : td->rotation_keys) times.insert(k.time);
        for (const auto& k : td->scale_keys) times.insert(k.time);
    }

    // Per joint, resolve its interpolator's default + data channel.
    struct Chan {
        const NifTransformInterpolator* interp = nullptr;
        const NifTransformData* data = nullptr;
    };
    std::vector<Chan> channels(joint_paths.size());
    for (const auto& cb : seq.controlled_blocks) {
        auto jit = joint_lookup.find(cb.node_name);
        if (jit == joint_lookup.end()) continue;
        auto it = idx.interp.find(static_cast<uint32_t>(cb.interpolator_ref));
        if (it == idx.interp.end()) continue;
        channels[jit->second].interp = it->second;
        auto dit = idx.data.find(static_cast<uint32_t>(it->second->data_ref));
        if (dit != idx.data.end()) channels[jit->second].data = dit->second;
    }

    UsdAttribute tAttr = anim.CreateTranslationsAttr();
    UsdAttribute rAttr = anim.CreateRotationsAttr();
    UsdAttribute sAttr = anim.CreateScalesAttr();

    for (float t : times) {
        VtArray<GfVec3f> trans(joint_paths.size(), GfVec3f(0));
        VtArray<GfQuatf> rots(joint_paths.size(), GfQuatf(1, 0, 0, 0));
        VtArray<GfVec3h> scales(joint_paths.size(), GfVec3h(1));
        for (size_t j = 0; j < channels.size(); ++j) {
            const Chan& c = channels[j];
            GfVec3f defT(0);
            GfQuatf defR(1, 0, 0, 0);
            float defS = 1.0f;
            if (c.interp) {
                defT = GfVec3f(c.interp->translation.x, c.interp->translation.y, c.interp->translation.z);
                defR = GfQuatf(c.interp->rotation.w, c.interp->rotation.x, c.interp->rotation.y, c.interp->rotation.z);
                defS = c.interp->scale;
            }
            if (c.data) {
                trans[j] = sample_translation(*c.data, t, defT);
                rots[j] = sample_rotation(*c.data, t, defR);
                float s = sample_scale(*c.data, t, defS);
                scales[j] = GfVec3h(s, s, s);
            } else {
                trans[j] = defT;
                rots[j] = defR;
                scales[j] = GfVec3h(defS, defS, defS);
            }
        }
        const UsdTimeCode tc(static_cast<double>(t) * tcps);
        tAttr.Set(trans, tc);
        rAttr.Set(rots, tc);
        sAttr.Set(scales, tc);
    }
}

} // namespace

void kf_to_usd(const NifFile& kf,
               const std::string& out_path,
               const std::string& source_name,
               const KfToUsdOptions& options) {
    if (kf.sequences.empty()) {
        throw NifError("kf_to_usd: file has no animation clips (NiControllerSequence)");
    }

    AnimIndex idx(kf);

    // The joint set is the union of every node targeted across the exported
    // clips. A KF clip carries target names, not the bind hierarchy, so we emit
    // a flat skeleton (all joints under the skeleton root) — enough for the
    // animation to play back; a real bind pose comes from the paired model.
    std::vector<std::string> joint_names;
    std::map<std::string, int> joint_lookup;
    auto want_clip = [&](int i) { return options.clip_index < 0 || options.clip_index == i; };
    for (size_t i = 0; i < kf.sequences.size(); ++i) {
        if (!want_clip(static_cast<int>(i))) continue;
        for (const auto& cb : kf.sequences[i].controlled_blocks) {
            if (joint_lookup.emplace(cb.node_name, static_cast<int>(joint_names.size())).second) {
                joint_names.push_back(cb.node_name);
            }
        }
    }
    if (joint_names.empty()) {
        throw NifError("kf_to_usd: no animated nodes in the selected clip(s)");
    }

    UsdStageRefPtr stage = UsdStage::CreateNew(out_path);
    if (!stage) throw NifError("kf_to_usd: failed to create USD stage at " + out_path);

    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->z); // NIF/Gamebryo Z-up
    UsdGeomSetStageMetersPerUnit(stage, 1.0);
    stage->SetTimeCodesPerSecond(options.time_codes_per_second);
    stage->SetMetadata(TfToken("comment"), VtValue("Exported from LU KF: " + source_name));

    // SkelRoot -> Skeleton, plus one SkelAnimation per clip.
    const SdfPath root_path("/anim");
    UsdSkelRoot skel_root = UsdSkelRoot::Define(stage, root_path);
    stage->SetDefaultPrim(skel_root.GetPrim());

    const SdfPath skel_path = root_path.AppendChild(TfToken("Skeleton"));
    UsdSkelSkeleton skeleton = UsdSkelSkeleton::Define(stage, skel_path);

    // Flat joint paths: each joint is a top-level joint token. UsdSkel joint
    // tokens are path-like; unique-sanitize to keep them valid and distinct.
    VtArray<TfToken> joints;
    std::vector<std::string> joint_paths;
    std::set<std::string> used;
    joints.reserve(joint_names.size());
    joint_paths.reserve(joint_names.size());
    for (const auto& n : joint_names) {
        std::string base = sanitize(n);
        std::string name = base;
        int suffix = 1;
        while (!used.insert(name).second) name = base + "_" + std::to_string(suffix++);
        joints.push_back(TfToken(name));
        joint_paths.push_back(name);
    }
    skeleton.CreateJointsAttr(VtValue(joints));
    // Identity bind + rest transforms (the KF clip has no bind pose of its own).
    VtArray<GfMatrix4d> identity(joint_names.size(), GfMatrix4d(1.0));
    skeleton.CreateBindTransformsAttr(VtValue(identity));
    skeleton.CreateRestTransformsAttr(VtValue(identity));

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
        UsdSkelBindingAPI binding = UsdSkelBindingAPI::Apply(skeleton.GetPrim());
        binding.CreateAnimationSourceRel().SetTargets({anim_path});
        break;
    }

    if (min_t <= max_t) {
        stage->SetStartTimeCode(min_t * options.time_codes_per_second);
        stage->SetEndTimeCode(max_t * options.time_codes_per_second);
    }

    stage->GetRootLayer()->Save();
}

} // namespace lu::assets
