#include "usd/object_to_usd.h"
#include "usd/usd_compose_internal.h"

#include "gamebryo/nif/nif_reader.h"
#include "gamebryo/kfm/kfm_reader.h"
#include "netdevil/archive/vfs/vfs.h"
#include "netdevil/database/cdclient/cdclient.h"
#include "gamebryo/nif/nif_geometry.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/quatd.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>

#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace lu::assets {

namespace {

std::string sanitize(const std::string& raw, const std::string& fallback) {
    std::string out;
    for (char c : raw) {
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_');
    }
    if (out.empty()) out = fallback;
    if (std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    return out;
}

// Normalize a KFM/CDClient asset reference (backslashes, mixed case) to forward
// slashes and strip a leading "./". CDClient stores escaped separators
// ("mesh\\veh\\x.nif"), which become "//" — collapse runs of slashes so the
// path resolves.
std::string slashify(std::string p) {
    for (char& c : p) if (c == '\\') c = '/';
    // Collapse repeated slashes.
    std::string out;
    out.reserve(p.size());
    bool prev_slash = false;
    for (char c : p) {
        if (c == '/') {
            if (prev_slash) continue;
            prev_slash = true;
        } else {
            prev_slash = false;
        }
        out.push_back(c);
    }
    if (out.rfind("./", 0) == 0) out.erase(0, 2);
    return out;
}

// Resolve a reference that may be relative to base_dir (a game path like
// "res/animations/creatures"), collapsing ".."/"." components. KFM model/KF
// references are relative to the KFM's own directory (e.g. "..\..\mesh\x.nif"
// or "movement/y.kf"), not to the client root.
std::string resolve_relative(const std::string& base_dir, const std::string& ref) {
    std::string r = slashify(ref);
    // Absolute-ish (already rooted at res/) references pass through.
    if (r.rfind("res/", 0) == 0 || r.rfind("/", 0) == 0) return r;

    std::vector<std::string> parts;
    auto push = [&](const std::string& seg) {
        if (seg.empty() || seg == ".") return;
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); return; }
        parts.push_back(seg);
    };
    auto split_into = [&](const std::string& s) {
        size_t start = 0;
        while (start <= s.size()) {
            size_t slash = s.find('/', start);
            if (slash == std::string::npos) { push(s.substr(start)); break; }
            push(s.substr(start, slash - start));
            start = slash + 1;
        }
    };
    split_into(slashify(base_dir));
    split_into(r);

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += '/';
        out += parts[i];
    }
    return out;
}

// Case-insensitive test that a game path ends with an extension (e.g. ".kfm").
bool has_ext_ci(const std::string& p, const std::string& ext) {
    if (p.size() < ext.size()) return false;
    for (size_t i = 0; i < ext.size(); ++i) {
        char a = static_cast<char>(std::tolower(static_cast<unsigned char>(p[p.size() - ext.size() + i])));
        char b = static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i])));
        if (a != b) return false;
    }
    return true;
}

std::optional<std::vector<uint8_t>> vfs_read_any(const Vfs& vfs, const std::string& game_path) {
    std::string p = slashify(game_path);
    if (auto d = vfs.read(p)) return d;
    // Some references omit the res/ prefix; try it.
    if (p.rfind("res/", 0) != 0) {
        if (auto d = vfs.read("res/" + p)) return d;
        // CDClient physics_asset paths are rooted under res/physics/ but stored
        // without that prefix (e.g. "minifig\minifig_physics.hkx" lives at
        // res/physics/minifig/minifig_physics.hkx). Try it for .hkx refs.
        if (has_ext_ci(p, ".hkx")) {
            if (auto d = vfs.read("res/physics/" + p)) return d;
        }
    }
    return std::nullopt;
}

// Directory portion of a game path ("res/a/b/c.kfm" -> "res/a/b").
std::string dir_of(const std::string& p) {
    std::string s = slashify(p);
    size_t slash = s.find_last_of('/');
    return slash == std::string::npos ? std::string() : s.substr(0, slash);
}

// The ETK sibling of a KF path ("a/b/x.kf" -> "a/b/x.etk"). ETK holds the
// clip's NiTextKeyExtraData event markers, stored beside the KF.
std::string etk_sibling(const std::string& kf_path) {
    std::string s = slashify(kf_path);
    size_t dot = s.find_last_of('.');
    size_t slash = s.find_last_of('/');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return s.substr(0, dot) + ".etk";
    }
    return s + ".etk";
}

// Write the KFM transition graph for a clip as lu:* metadata on its
// SkelAnimation prim, so the animation state machine survives the round-trip.
void author_transition_metadata(const UsdStagePtr& stage, const SdfPath& anim_path,
                                const KfmSequence& seq) {
    UsdPrim prim = stage->GetPrimAtPath(anim_path);
    if (!prim) return;
    prim.CreateAttribute(TfToken("lu:kfmSequenceId"), SdfValueTypeNames->UInt, true)
        .Set(seq.id);
    if (seq.transitions.empty()) return;

    VtArray<unsigned int> dests, types;
    VtArray<float> durations;
    for (const auto& t : seq.transitions) {
        dests.push_back(t.dest_id);
        types.push_back(t.stored_type);
        durations.push_back(t.duration);
    }
    prim.CreateAttribute(TfToken("lu:transitionDestIds"),
        SdfValueTypeNames->UIntArray, true).Set(dests);
    prim.CreateAttribute(TfToken("lu:transitionTypes"),
        SdfValueTypeNames->UIntArray, true).Set(types);
    prim.CreateAttribute(TfToken("lu:transitionDurations"),
        SdfValueTypeNames->FloatArray, true).Set(durations);
}

// Attach an ETK's text-key event markers (time -> event name) to a clip's
// SkelAnimation prim as parallel lu:* arrays. ETK holds NiTextKeyExtraData
// event markers (footstep/hit-frame/…), not motion, so they annotate the clip.
void author_text_keys(const UsdStagePtr& stage, const SdfPath& anim_path,
                      const NifFile& etk) {
    UsdPrim prim = stage->GetPrimAtPath(anim_path);
    if (!prim || etk.text_key_data.empty()) return;
    VtArray<float> times;
    VtArray<std::string> events;
    for (const auto& tkd : etk.text_key_data) {
        for (const auto& k : tkd.keys) {
            times.push_back(k.time);
            events.push_back(k.text);
        }
    }
    if (times.empty()) return;
    prim.CreateAttribute(TfToken("lu:textKeyTimes"),
        SdfValueTypeNames->FloatArray, true).Set(times);
    prim.CreateAttribute(TfToken("lu:textKeyEvents"),
        SdfValueTypeNames->StringArray, true).Set(events);
}

// A KF file to author, with its optional paired ETK (event keys).
struct AnimFile {
    std::string kf_path;
    std::vector<uint8_t> kf_bytes;
    std::optional<std::vector<uint8_t>> etk_bytes; // sibling <name>.etk, if present
};

// The bone hierarchy extracted from a model NIF, in the form UsdSkel needs.
// LU animation transforms are parent-relative, so the skeleton must carry the
// real parent/child topology (a flat skeleton scrambles the pose).
struct SkeletonTopology {
    std::vector<std::string> joint_names;    // bare bone names, hierarchy order
    std::vector<std::string> joint_paths;    // "parent/child/…" USD joint paths
    std::vector<GfMatrix4d> local_bind;      // joint-local (parent-relative) bind xform
    std::map<std::string, GfMatrix4d> world_bind; // bone name -> world bind (node space)
};

// Guard against NIF's +/-FLT_MAX "unset" sentinels, which would otherwise poison
// a transform (and, via half-precision scale, produce inf).
static double finite_or(double v, double fallback) {
    return (std::isfinite(v) && std::abs(v) < 1e30) ? v : fallback;
}

// Convert a node's local TRS to a GfMatrix4d (USD row-vector convention).
static GfMatrix4d node_local_matrix(const Vec3& t, const Quat& q, float s) {
    GfMatrix4d m(1.0);
    m.SetRotate(GfQuatd(finite_or(q.w, 1.0), finite_or(q.x, 0.0),
                        finite_or(q.y, 0.0), finite_or(q.z, 0.0)));
    double sc = finite_or(s, 1.0);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) m[r][c] *= sc;
    m.SetTranslateOnly(GfVec3d(finite_or(t.x, 0.0), finite_or(t.y, 0.0),
                               finite_or(t.z, 0.0)));
    return m;
}

// Walk the NIF node hierarchy and emit a SkeletonTopology of ONLY the real
// bones: nodes named in `wanted` (animated or skinned to) plus the ancestor
// chain up to each, so parenting stays valid. Scene-graph plumbing that is not
// an ancestor of any bone — LOD groups, the mesh shape nodes, etc. — is pruned.
// Joint paths encode parenting so UsdSkel composes parent-relative animation.
//
// bind_override maps a bone name to its parent-relative bind transform taken
// from the KF animation's interpolator defaults. LU model NIFs author some bone
// nodes at a bind offset far from the mesh (e.g. `root` at Y≈64 while the mesh
// and every animated pose are at ~1 unit), which would draw a giant bone and put
// the skeleton in a different space than the animation. Using the animation's
// rest pose puts the skeleton in the SAME space as the mesh and the animation.
// A bone with no override falls back to its node-hierarchy transform.
SkeletonTopology build_skeleton_topology(const NifFile& nif,
                                         const std::set<std::string>& wanted,
                                         const std::map<std::string, GfMatrix4d>& bind_override) {
    std::map<uint32_t, const NifNode*> by_block;
    for (const auto& n : nif.nodes) by_block[n.block_index] = &n;

    // parent[block] = parent block (or -1 for a root).
    std::map<uint32_t, int32_t> parent;
    for (const auto& n : nif.nodes)
        for (int32_t ch : n.children)
            parent[static_cast<uint32_t>(ch)] = static_cast<int32_t>(n.block_index);

    // Keep set = every wanted node plus all its ancestors.
    std::set<uint32_t> keep;
    for (const auto& n : nif.nodes) {
        if (!wanted.count(n.name)) continue;
        int32_t b = static_cast<int32_t>(n.block_index);
        while (b >= 0 && keep.insert(static_cast<uint32_t>(b)).second) {
            auto it = parent.find(static_cast<uint32_t>(b));
            b = it != parent.end() ? it->second : -1;
        }
    }
    const bool keep_all = keep.empty();

    SkeletonTopology topo;
    // world_of[name] = the world bind matrix used for a kept bone, so a child can
    // divide it out to get its parent-relative local transform.
    std::map<std::string, GfMatrix4d> world_of;

    std::function<void(const NifNode*, const std::string&, const GfMatrix4d&)> visit =
        [&](const NifNode* node, const std::string& parent_path,
            const GfMatrix4d& parent_world) {
            if (!node) return;
            const bool kept = keep_all || keep.count(node->block_index);
            std::string path = parent_path;
            GfMatrix4d node_world = parent_world;

            if (kept) {
                std::string name = sanitize(node->name, "joint");
                path = parent_path.empty() ? name : parent_path + "/" + name;

                // The skeleton's rest pose comes from the animation (mesh space).
                // A bone the animation doesn't drive must NOT use its NIF node
                // transform: LU authors some helper/IK bones (Lt_Foot, ik_*_hand)
                // at a far bind offset (Y≈63) while everything the animation
                // touches is at ~1 unit, and mixing the two spaces in one skeleton
                // explodes the rig. So un-animated bones get identity local
                // (they simply follow their parent), keeping the whole skeleton in
                // the animation's space.
                GfMatrix4d local(1.0);
                auto ov = bind_override.find(node->name);
                if (ov != bind_override.end()) local = ov->second;
                node_world = local * parent_world;

                topo.joint_names.push_back(node->name);
                topo.joint_paths.push_back(path);
                topo.local_bind.push_back(local);
                topo.world_bind[node->name] = node_world;
                world_of[path] = node_world;
            }
            for (int32_t ch : node->children) {
                auto it = by_block.find(static_cast<uint32_t>(ch));
                if (it != by_block.end()) visit(it->second, path, node_world);
            }
        };

    std::set<uint32_t> is_child;
    for (const auto& n : nif.nodes)
        for (int32_t ch : n.children) is_child.insert(static_cast<uint32_t>(ch));
    for (const auto& n : nif.nodes) {
        if (!is_child.count(n.block_index)) visit(&n, "", GfMatrix4d(1.0));
    }
    return topo;
}

// Build boneName -> NiSkinData skin-to-bone offset (the transform from mesh/skin
// space into that bone's local space at bind time). The bone name comes from the
// paired NiSkinInstance's bone_refs -> node. Row-vector convention.
std::map<std::string, GfMatrix4d> build_skin_to_bone(const NifFile& nif) {
    std::map<std::string, GfMatrix4d> offsets;
    auto node_name = [&](int32_t block) -> std::string {
        for (const auto& n : nif.nodes)
            if (static_cast<int32_t>(n.block_index) == block) return n.name;
        return {};
    };
    auto to_matrix = [](const Vec3& t, const Quat& q, float s) {
        GfMatrix4d m(1.0);
        m.SetRotate(GfQuatd(finite_or(q.w, 1.0), finite_or(q.x, 0.0),
                            finite_or(q.y, 0.0), finite_or(q.z, 0.0)));
        double sc = finite_or(s, 1.0);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) m[r][c] *= sc;
        m.SetTranslateOnly(GfVec3d(finite_or(t.x, 0.0), finite_or(t.y, 0.0),
                                   finite_or(t.z, 0.0)));
        return m;
    };
    for (size_t si = 0; si < nif.skin_instances.size(); ++si) {
        const NifSkinInstance& inst = nif.skin_instances[si];
        if (inst.data_ref < 0) continue;
        const NifSkinData* sd = nullptr;
        for (const auto& d : nif.skin_data)
            if (static_cast<int32_t>(d.block_index) == inst.data_ref) { sd = &d; break; }
        if (!sd) continue;
        const size_t n = std::min(inst.bone_refs.size(), sd->bones.size());
        for (size_t b = 0; b < n; ++b) {
            std::string name = node_name(inst.bone_refs[b]);
            if (name.empty() || offsets.count(name)) continue;
            offsets[name] = to_matrix(sd->bones[b].translation,
                                      sd->bones[b].rotation, sd->bones[b].scale);
        }
    }
    return offsets;
}

// The mesh's geomBindTransform maps mesh-local points into the skeleton bind
// space. LU authors the mesh, the bones and the skin bind in different spaces;
// the NiSkinData skin-to-bone offset bridges skin space to a bone's local space,
// and world_bind[bone] takes that to skeleton world. Their product,
// world_bind[bone] * skinToBone[bone], is the mesh->bind-space transform and is
// (up to per-bone rounding) the same for every skinned bone — so any skinned
// bone present in both maps yields it. Returns identity if none can be resolved.
GfMatrix4d build_geom_bind_transform(const std::map<std::string, GfMatrix4d>& world_bind,
                                     const std::map<std::string, GfMatrix4d>& skin_to_bone) {
    for (const auto& [name, offset] : skin_to_bone) {
        auto wb = world_bind.find(name);
        if (wb != world_bind.end()) {
            // row-vector: mesh -> skinToBone (skin->boneLocal) -> boneWorld.
            return offset * wb->second;
        }
    }
    return GfMatrix4d(1.0);
}

// Common composition: set up the SkelRoot stage, author the model, then the
// animations and collision, and save.
// One collision source: a disk path to an HKX plus a label for its sub-scope
// (derived from the physics component type, e.g. "ControllablePhysics").
struct CollisionSource {
    std::string label;
    std::string disk_path;
};

void compose(const std::string& out_path,
             const std::string& source_name,
             const std::optional<std::vector<uint8_t>>& model_nif,
             const std::vector<AnimFile>& kf_files,
             const std::vector<CollisionSource>& collisions,
             const std::vector<const KfmSequence*>& kfm_seqs,
             const ObjectToUsdOptions& options) {
    UsdStageRefPtr stage = UsdStage::CreateNew(out_path);
    if (!stage) throw NifError("object_to_usd: failed to create USD stage at " + out_path);

    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y); // LU/Gamebryo geometry is Y-up
    UsdGeomSetStageMetersPerUnit(stage, 1.0);
    stage->SetTimeCodesPerSecond(options.time_codes_per_second);
    stage->SetMetadata(TfToken("comment"), VtValue("Composed LU object: " + source_name));

    const SdfPath root("/object");
    UsdSkelRoot skel_root = UsdSkelRoot::Define(stage, root);
    stage->SetDefaultPrim(skel_root.GetPrim());

    // Parse every KF up front. All clips and the skinned model share ONE
    // skeleton, so the joint set must be the union across every KF (plus the
    // model's own skin bones) with a single consistent ordering.
    std::vector<NifFile> parsed_kfs;
    if (options.include_animation) {
        parsed_kfs.reserve(kf_files.size());
        for (const auto& af : kf_files) {
            try {
                parsed_kfs.push_back(
                    nif_parse(std::span<const uint8_t>(af.kf_bytes.data(), af.kf_bytes.size())));
            } catch (const std::exception&) {
                parsed_kfs.emplace_back(); // placeholder so indices stay aligned
            }
        }
    }

    std::optional<NifFile> model;
    if (model_nif) {
        try {
            model = nif_parse(std::span<const uint8_t>(model_nif->data(), model_nif->size()));
        } catch (const std::exception&) {}
    }

    // Build the skeleton. When a model exists, take the real bone hierarchy from
    // its NIF node tree (joint paths encode parenting) so LU's parent-relative
    // animation transforms compose correctly and the mesh deforms right. Without
    // a model (KF-only), fall back to a flat joint set — enough to preview the
    // motion, though without a bind pose.
    const SdfPath skel_path = root.AppendChild(TfToken("Skeleton"));
    std::vector<std::string> joint_names;   // bare bone names (clip/skin lookup)
    std::vector<std::string> joint_paths;   // sanitized names or "a/b/c" paths
    std::map<std::string, int> joint_lookup; // bone name -> skeleton joint index
    GfMatrix4d model_geom_bind(1.0);        // mesh-local -> skeleton bind space
    bool have_geom_bind = false;

    if (model) {
        // Wanted set: joints the animation targets or the mesh skins to (kept for
        // possible future pruning; the full hierarchy is emitted for now).
        std::set<std::string> wanted;
        for (const auto& kf : parsed_kfs)
            for (const auto& n : collect_kf_joints(kf, -1)) wanted.insert(n);

        // Per-bone bind override = the FIRST clip's frame-0 pose (the clip that
        // is bound by default below). UsdSkel treats a SkelAnimation's per-joint
        // transforms as absolute locals and skins via jointAnimWorld *
        // inverse(jointBindWorld); the mesh is only undistorted when the bind
        // pose matches the played clip's rest, so the bind must come from that
        // exact clip. Use each animated bone's first keyframe (or interpolator
        // default when it has no keys), in the model's real ~1-unit space — which
        // also keeps the skeleton at the mesh's scale (no far-off bind bones).
        std::map<std::string, GfMatrix4d> bind_override;
        for (const auto& kf : parsed_kfs) {
            if (kf.sequences.empty()) continue;
            std::map<uint32_t, const NifTransformInterpolator*> interp_by_block;
            for (const auto& it : kf.transform_interpolators)
                interp_by_block[it.block_index] = &it;
            std::map<uint32_t, const NifTransformData*> data_by_block;
            for (const auto& td : kf.transform_data)
                data_by_block[td.block_index] = &td;
            const NifControllerSequence& seq = kf.sequences.front();
            for (const auto& cb : seq.controlled_blocks) {
                if (bind_override.count(cb.node_name)) continue;
                auto it = interp_by_block.find(static_cast<uint32_t>(cb.interpolator_ref));
                if (it == interp_by_block.end()) continue;
                const NifTransformInterpolator* ip = it->second;
                Vec3 t = ip->translation;
                Quat rot = ip->rotation;
                float sc = ip->scale;
                // Prefer the first keyframe of the channel over the default.
                auto dit = data_by_block.find(static_cast<uint32_t>(ip->data_ref));
                if (dit != data_by_block.end()) {
                    const NifTransformData* td = dit->second;
                    if (!td->translation_keys.empty()) t = td->translation_keys.front().value;
                    if (!td->rotation_keys.empty()) rot = td->rotation_keys.front().value;
                    if (!td->scale_keys.empty()) sc = td->scale_keys.front().value;
                }
                bind_override[cb.node_name] =
                    node_local_matrix({t.x, t.y, t.z}, rot, sc);
            }
            break; // only the first (bound) clip defines the bind pose
        }
        SkeletonTopology topo = build_skeleton_topology(*model, wanted, bind_override);

        joint_names = topo.joint_names;
        joint_paths = topo.joint_paths;
        for (size_t i = 0; i < joint_names.size(); ++i)
            joint_lookup[joint_names[i]] = static_cast<int>(i);

        author_skeleton_hierarchical(stage, skel_path, topo.joint_paths, topo.local_bind);

        // The skeleton is now in the animation's (== mesh) space, so the mesh
        // needs no geomBind bridge — it's authored where the bones' bind pose is.
        // (An earlier design put bones in NIF node space and used geomBind =
        // skinToBone * worldBind to bridge; with the anim-space bind that no
        // longer matches skinToBone's space, so leave geomBind identity.)
        have_geom_bind = false;
    } else {
        std::set<std::string> seen;
        for (const auto& kf : parsed_kfs)
            for (const auto& n : collect_kf_joints(kf, -1))
                if (seen.insert(n).second) joint_names.push_back(n);
        if (!joint_names.empty()) {
            joint_paths = author_skeleton(stage, skel_path, joint_names, {});
            for (size_t i = 0; i < joint_names.size(); ++i)
                joint_lookup[joint_names[i]] = static_cast<int>(i);
        }
    }

    // Model geometry under /object/Model, skinned to the shared skeleton so it
    // deforms with the animation (and renders under the SkelRoot). The mesh's
    // geomBindTransform (authored per skinned mesh) bridges the mesh's authored
    // space into the bones' bind space, so the skinning resolves to the correct
    // pose at the origin and animation deltas apply on top — no counter-transform
    // on the Model prim is needed.
    if (model) {
        UsdGeomXform::Define(stage, root.AppendChild(TfToken("Model")));
        author_nif_model(stage, root.AppendChild(TfToken("Model")), *model, {},
                         joint_names.empty() ? SdfPath() : skel_path, joint_paths,
                         have_geom_bind ? &model_geom_bind : nullptr);
    }

    // Animation clips, all against the shared skeleton; attach KFM transition
    // metadata and sibling-ETK event keys per clip.
    double min_t = 1e30, max_t = -1e30;
    size_t seq_cursor = 0;
    UsdSkelBindingAPI skel_binding;
    bool bound_default = false;
    int total_authored = 0;
    if (options.include_animation && !joint_names.empty()) {
        for (size_t fi = 0; fi < kf_files.size(); ++fi) {
            const AnimFile& af = kf_files[fi];
            const NifFile& kf = parsed_kfs[fi];
            if (kf.sequences.empty()) { continue; }

            // Respect the overall animation cap (a minifig has ~1100 clips, which
            // can overwhelm an interactive DCC viewport). 0 = unlimited.
            int budget = options.max_animations > 0
                ? options.max_animations - total_authored : 0;
            if (options.max_animations > 0 && budget <= 0) break;

            int n = author_kf_clips_shared(stage, root, kf, joint_paths, joint_lookup,
                                           options.time_codes_per_second, min_t, max_t,
                                           budget);

            std::optional<NifFile> etk;
            if (af.etk_bytes) {
                try {
                    etk = nif_parse(std::span<const uint8_t>(af.etk_bytes->data(),
                                                             af.etk_bytes->size()));
                } catch (const std::exception&) {}
            }
            // Only the first n sequences of this file were authored.
            int emitted = 0;
            for (const auto& seq : kf.sequences) {
                if (emitted >= n) break;
                const SdfPath anim_path =
                    root.AppendChild(TfToken("anim_" + sanitize(seq.name, "clip")));
                if (seq_cursor < kfm_seqs.size() && kfm_seqs[seq_cursor]) {
                    author_transition_metadata(stage, anim_path, *kfm_seqs[seq_cursor]);
                }
                if (etk) author_text_keys(stage, anim_path, *etk);
                // Bind the first clip on the skeleton so usdview plays it by default.
                if (!bound_default) {
                    skel_binding = UsdSkelBindingAPI::Apply(stage->GetPrimAtPath(skel_path));
                    skel_binding.CreateAnimationSourceRel().SetTargets({anim_path});
                    bound_default = true;
                }
                ++seq_cursor;
                ++emitted;
            }
            total_authored += n;
        }
        if (min_t <= max_t) {
            stage->SetStartTimeCode(min_t * options.time_codes_per_second);
            stage->SetEndTimeCode(max_t * options.time_codes_per_second);
        }
    }

    // Collision under /object/Collision. An object may carry more than one
    // physics component (e.g. a phantom trigger volume plus a solid body), so
    // each collision source gets its own labelled sub-scope.
    if (options.include_collision && !collisions.empty()) {
        const SdfPath coll_root = root.AppendChild(TfToken("Collision"));
        UsdGeomScope::Define(stage, coll_root);
        std::set<std::string> used;
        for (const auto& c : collisions) {
            std::string name = sanitize(c.label, "physics");
            int suffix = 1;
            std::string unique = name;
            while (!used.insert(unique).second) unique = name + "_" + std::to_string(suffix++);
            try {
                author_hkx_collision(stage, coll_root.AppendChild(TfToken(unique)),
                                     c.disk_path, {});
            } catch (const std::exception&) {
                // Missing/empty collision is non-fatal.
            }
        }
    }

    stage->GetRootLayer()->Save();
}

// VFS reads return bytes, but hkx_to_usd/author_hkx_collision parse from a disk
// path. Spill the collision bytes to a temp file so the shared HKX parser (which
// takes a filesystem path) can read it. Returns the temp path (caller cleans up).
std::optional<std::string> spill_temp(const std::vector<uint8_t>& bytes, const char* suffix) {
    auto p = std::filesystem::temp_directory_path() /
        ("lu_compose_" + std::to_string(std::hash<const void*>{}(bytes.data())) + suffix);
    std::ofstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return p.string();
}

// Gather a KFM's model NIF, its animation KFs (+ sibling ETKs) and per-clip KFM
// sequence records, resolving every reference relative to the KFM's directory.
// Shared by object_from_kfm and object_from_lot (whose render asset is often a
// KFM, not a NIF). `kfm` must outlive the returned seq_order pointers.
void gather_from_kfm(const KfmFile& kfm, const std::string& kfm_game_path,
                     const Vfs& vfs,
                     std::optional<std::vector<uint8_t>>& model_out,
                     std::vector<AnimFile>& kf_files_out,
                     std::vector<const KfmSequence*>& seq_order_out) {
    const std::string base = dir_of(kfm_game_path);

    // Model NIF named by the KFM (relative to the KFM directory).
    model_out = vfs_read_any(vfs, resolve_relative(base, kfm.model_path));

    std::set<std::string> seen_kf;
    for (const auto& seq : kfm.sequences) {
        std::string kfp = resolve_relative(base, seq.kf_filename);
        if (seen_kf.insert(kfp).second) {
            if (auto d = vfs_read_any(vfs, kfp)) {
                AnimFile af;
                af.kf_path = kfp;
                af.kf_bytes = std::move(*d);
                af.etk_bytes = vfs_read_any(vfs, etk_sibling(kfp));
                kf_files_out.push_back(std::move(af));
            }
        }
        seq_order_out.push_back(&seq);
    }
}

} // namespace

void object_from_kfm(const std::string& kfm_path,
                     const Vfs& vfs,
                     const std::string& out_path,
                     const ObjectToUsdOptions& options) {
    auto kfm_bytes = vfs_read_any(vfs, kfm_path);
    if (!kfm_bytes) throw NifError("object_from_kfm: cannot read KFM " + kfm_path);
    KfmFile kfm = kfm_parse(std::span<const uint8_t>(kfm_bytes->data(), kfm_bytes->size()));

    std::optional<std::vector<uint8_t>> model;
    std::vector<AnimFile> kf_files;
    std::vector<const KfmSequence*> seq_order;
    gather_from_kfm(kfm, kfm_path, vfs, model, kf_files, seq_order);

    // A KFM-only source carries no physics reference; collision comes from the
    // LOT path (via a physics component), so compose with no collisions here.
    compose(out_path, kfm_path, model, kf_files, {}, seq_order, options);
}

void object_from_lot(int32_t lot,
                     const CdClient& cd,
                     const Vfs& vfs,
                     const std::string& out_path,
                     const ObjectToUsdOptions& options) {
    auto render_asset = cd.get_render_asset(lot);
    if (!render_asset) {
        throw NifError("object_from_lot: LOT " + std::to_string(lot) +
                       " has no render asset");
    }

    std::optional<std::vector<uint8_t>> model;
    std::vector<AnimFile> kf_files;
    std::vector<const KfmSequence*> seq_order;
    KfmFile kfm; // must outlive seq_order (holds pointers into kfm.sequences)

    // The render asset can itself be a KFM (animation manifest — common for
    // animated actors like the minifig) OR a plain NIF model. When it's a KFM,
    // pull in the NIF the KFM names plus its whole animation set; when it's a
    // NIF, use it directly and look for a sibling .kfm for animations.
    if (has_ext_ci(*render_asset, ".kfm")) {
        std::string kfm_path = slashify(*render_asset);
        auto kfm_bytes = vfs_read_any(vfs, kfm_path);
        if (!kfm_bytes) {
            throw NifError("object_from_lot: KFM render asset not found in VFS: " +
                           *render_asset);
        }
        kfm = kfm_parse(std::span<const uint8_t>(kfm_bytes->data(), kfm_bytes->size()));
        gather_from_kfm(kfm, kfm_path, vfs, model, kf_files, seq_order);
    } else {
        model = vfs_read_any(vfs, *render_asset);
        if (!model) {
            throw NifError("object_from_lot: render asset not found in VFS: " +
                           *render_asset);
        }
        // A NIF model may still have a paired .kfm (same stem) for its animations.
        std::string kfm_ref = slashify(*render_asset);
        auto dot = kfm_ref.find_last_of('.');
        if (dot != std::string::npos) {
            kfm_ref = kfm_ref.substr(0, dot) + ".kfm";
            if (auto d = vfs_read_any(vfs, kfm_ref)) {
                try {
                    kfm = kfm_parse(std::span<const uint8_t>(d->data(), d->size()));
                    // Model already loaded from the NIF; gather only animations.
                    std::optional<std::vector<uint8_t>> ignore_model;
                    gather_from_kfm(kfm, kfm_ref, vfs, ignore_model, kf_files, seq_order);
                } catch (const std::exception&) {}
            }
        }
    }

    // Physics: an object's collision comes from whatever physics component(s)
    // sit on it. The physics-family component types (RE'd ComponentType enum;
    // see the component-type reference) all resolve their HKX through the
    // PhysicsComponent table (VehiclePhysics has its own). Multiple can coexist —
    // a phantom trigger volume alongside one solid body — so gather each rather
    // than stopping at the first. physics_asset is queried via the generic
    // interface because get_physics_component() names columns that don't exist
    // in every CDClient schema version and would silently return nothing.
    std::vector<CollisionSource> collisions;
    std::vector<std::string> hkx_temps; // own the spilled temp files
    auto physics_type_name = [](int32_t t) -> const char* {
        switch (t) {
            case 1:  return "ControllablePhysics";
            case 3:  return "SimplePhysics";
            case 20: return "ProjectilePhysics";
            case 22: return "RigidBodyPhantomPhysics";
            case 28: return "PhantomPhysics";
            case 68: return "VehiclePhysics";
            case 69: return "PhysicsSystem";
            default: return nullptr;
        }
    };
    for (const auto& comp : cd.get_components(lot)) {
        const char* type_name = physics_type_name(comp.component_type);
        if (!type_name) continue;
        const char* table = (comp.component_type == 68) ? "VehiclePhysics"
                                                        : "PhysicsComponent";
        auto rows = cd.query(table, "id = ?", {CdValue{comp.component_id}});
        for (const auto& row : rows) {
            for (const auto& val : row) {
                if (const auto* s = std::get_if<std::string>(&val)) {
                    if (has_ext_ci(*s, ".hkx")) {
                        if (auto d = vfs_read_any(vfs, *s)) {
                            if (auto tmp = spill_temp(*d, ".hkx")) {
                                hkx_temps.push_back(*tmp);
                                collisions.push_back({type_name, *tmp});
                            }
                        }
                    }
                }
            }
        }
    }

    compose(out_path, "LOT " + std::to_string(lot), model, kf_files, collisions,
            seq_order, options);

    for (const auto& t : hkx_temps) {
        std::error_code ec;
        std::filesystem::remove(t, ec);
    }
}

} // namespace lu::assets
