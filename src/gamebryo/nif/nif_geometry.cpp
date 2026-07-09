#include "gamebryo/nif/nif_geometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace lu::assets {

namespace {

struct Transform {
    Vec3 translation = {0, 0, 0};
    Quat rotation = {0, 0, 0, 1};
    float scale = 1.0f;
};

struct LodAssignment {
    uint32_t parent_block = 0;
    uint32_t level = 0;
    float near_distance = 0.0f;
    float far_distance = 0.0f;
    Vec3 center;
};

Vec3 rotate_vec(const Quat& q, const Vec3& v) {
    Vec3 u{q.x, q.y, q.z};
    float s = q.w;

    Vec3 cross1{
        u.y * v.z - u.z * v.y,
        u.z * v.x - u.x * v.z,
        u.x * v.y - u.y * v.x
    };
    Vec3 cross2{
        u.y * cross1.z - u.z * cross1.y,
        u.z * cross1.x - u.x * cross1.z,
        u.x * cross1.y - u.y * cross1.x
    };

    return {
        v.x + 2.0f * (s * cross1.x + cross2.x),
        v.y + 2.0f * (s * cross1.y + cross2.y),
        v.z + 2.0f * (s * cross1.z + cross2.z)
    };
}

Vec3 transform_point(const Transform& t, const Vec3& v) {
    Vec3 scaled{v.x * t.scale, v.y * t.scale, v.z * t.scale};
    Vec3 rotated = rotate_vec(t.rotation, scaled);
    return {
        rotated.x + t.translation.x,
        rotated.y + t.translation.y,
        rotated.z + t.translation.z
    };
}

Quat multiply_quat(const Quat& a, const Quat& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
    };
}

Quat normalize_quat(const Quat& q) {
    float len_sq = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (len_sq <= 0.0f) return {0, 0, 0, 1};
    float inv_len = 1.0f / std::sqrt(len_sq);
    return {q.x * inv_len, q.y * inv_len, q.z * inv_len, q.w * inv_len};
}

Transform local_transform(const NifNode& node) {
    Transform transform;
    transform.translation = node.translation;
    transform.rotation = normalize_quat(node.rotation);
    transform.scale = node.scale;
    return transform;
}

Transform compose_transform(const Transform& parent, const Transform& child) {
    Transform out;
    out.translation = transform_point(parent, child.translation);
    out.rotation = normalize_quat(multiply_quat(parent.rotation, child.rotation));
    out.scale = parent.scale * child.scale;
    return out;
}

Transform compute_world_transform(
    const NifNode& node,
    const std::unordered_map<uint32_t, const NifNode*>& node_by_block,
    const std::unordered_map<uint32_t, uint32_t>& parent_by_block,
    std::unordered_map<uint32_t, Transform>& world_by_block,
    std::unordered_set<uint32_t>& visiting) {

    auto cached = world_by_block.find(node.block_index);
    if (cached != world_by_block.end()) return cached->second;

    Transform local = local_transform(node);
    if (!visiting.insert(node.block_index).second) {
        return local;
    }

    Transform world = local;
    auto parent_it = parent_by_block.find(node.block_index);
    if (parent_it != parent_by_block.end() && parent_it->second != node.block_index) {
        auto parent_node_it = node_by_block.find(parent_it->second);
        if (parent_node_it != node_by_block.end()) {
            Transform parent_world = compute_world_transform(
                *parent_node_it->second, node_by_block, parent_by_block, world_by_block, visiting);
            world = compose_transform(parent_world, local);
        }
    }

    visiting.erase(node.block_index);
    world_by_block[node.block_index] = world;
    return world;
}

NifRenderMaterial default_material() {
    NifRenderMaterial mat;
    mat.name = "Default";
    return mat;
}

template <typename T>
const T* find_by_block(const std::unordered_map<uint32_t, const T*>& by_block, int32_t block) {
    if (block < 0) return nullptr;
    auto it = by_block.find(static_cast<uint32_t>(block));
    return it == by_block.end() ? nullptr : it->second;
}

std::string texture_filename(
    const std::unordered_map<uint32_t, const NifTextureRef*>& textures,
    int32_t texture_ref) {
    const NifTextureRef* tex = find_by_block(textures, texture_ref);
    return tex && tex->use_external ? tex->filename : std::string{};
}

NifRenderMaterial build_material_for_node(
    const NifNode& node,
    const std::unordered_map<uint32_t, const NifMaterial*>& materials,
    const std::unordered_map<uint32_t, const NifTexturingProperty*>& texturing_props,
    const std::unordered_map<uint32_t, const NifTextureRef*>& textures,
    const std::unordered_map<uint32_t, const NifAlphaProperty*>& alpha_props) {

    NifRenderMaterial out = default_material();

    const NifMaterial* src_mat = nullptr;
    for (int32_t prop_ref : node.properties) {
        if ((src_mat = find_by_block(materials, prop_ref)) != nullptr) break;
    }

    if (src_mat) {
        out.name = src_mat->name.empty() ? "NIF Material" : src_mat->name;
        out.ambient[0] = src_mat->ambient.x;
        out.ambient[1] = src_mat->ambient.y;
        out.ambient[2] = src_mat->ambient.z;
        out.diffuse[0] = src_mat->diffuse.x;
        out.diffuse[1] = src_mat->diffuse.y;
        out.diffuse[2] = src_mat->diffuse.z;
        out.diffuse[3] = src_mat->alpha;
        out.specular[0] = src_mat->specular.x;
        out.specular[1] = src_mat->specular.y;
        out.specular[2] = src_mat->specular.z;
        out.specular[3] = src_mat->glossiness;
        out.emissive[0] = src_mat->emissive.x;
        out.emissive[1] = src_mat->emissive.y;
        out.emissive[2] = src_mat->emissive.z;
    }

    const NifTexturingProperty* tex_prop = nullptr;
    for (int32_t prop_ref : node.properties) {
        if ((tex_prop = find_by_block(texturing_props, prop_ref)) != nullptr) break;
    }

    if (tex_prop) {
        out.diffuse_texture = texture_filename(textures, tex_prop->base_texture_source_ref);
        out.dark_texture = texture_filename(textures, tex_prop->dark_texture_source_ref);
        out.detail_texture = texture_filename(textures, tex_prop->detail_texture_source_ref);
        out.gloss_texture = texture_filename(textures, tex_prop->gloss_texture_source_ref);
        out.glow_texture = texture_filename(textures, tex_prop->glow_texture_source_ref);
    }

    const NifAlphaProperty* alpha = nullptr;
    for (int32_t prop_ref : node.properties) {
        if ((alpha = find_by_block(alpha_props, prop_ref)) != nullptr) break;
    }
    if (alpha) {
        // Keep alpha as parsed property data. Renderers may combine this with
        // texture format, material alpha, shader family, and pass policy.
        out.has_alpha_property = true;
        out.alpha_flags = alpha->flags;
        out.alpha_threshold = alpha->threshold;
    }

    return out;
}

void assign_lod_to_descendants(
    uint32_t block,
    const LodAssignment& assignment,
    const std::unordered_map<uint32_t, const NifNode*>& node_by_block,
    std::unordered_map<uint32_t, LodAssignment>& lod_by_node,
    std::unordered_set<uint32_t>& visiting) {

    if (!visiting.insert(block).second) return;

    auto node_it = node_by_block.find(block);
    if (node_it == node_by_block.end()) {
        visiting.erase(block);
        return;
    }

    lod_by_node[block] = assignment;
    for (int32_t child_ref : node_it->second->children) {
        if (child_ref < 0) continue;
        assign_lod_to_descendants(
            static_cast<uint32_t>(child_ref), assignment, node_by_block, lod_by_node, visiting);
    }

    visiting.erase(block);
}

void populate_skinning(
    const NifNode& node,
    size_t vertex_count,
    const std::unordered_map<uint32_t, const NifNode*>& node_by_block,
    const std::unordered_map<uint32_t, const NifSkinInstance*>& skin_instances,
    const std::unordered_map<uint32_t, const NifSkinData*>& skin_data,
    NifRenderMesh& out) {

    const NifSkinInstance* skin = find_by_block(skin_instances, node.skin_instance_ref);
    if (!skin) return;

    const NifSkinData* data = find_by_block(skin_data, skin->data_ref);
    if (!data) return;

    out.is_skinned = true;
    out.skin_instance_block = skin->block_index;
    out.skeleton_root_block = skin->skeleton_root_ref;
    out.skin_bone_node_blocks = skin->bone_refs;
    out.skin_bone_names.reserve(skin->bone_refs.size());
    for (int32_t bone_ref : skin->bone_refs) {
        std::string name;
        if (bone_ref >= 0) {
            auto bone_it = node_by_block.find(static_cast<uint32_t>(bone_ref));
            if (bone_it != node_by_block.end()) name = bone_it->second->name;
        }
        out.skin_bone_names.push_back(std::move(name));
    }

    out.vertex_influences.resize(vertex_count);
    const size_t bone_count = std::min(skin->bone_refs.size(), data->bones.size());
    for (size_t bone_index = 0; bone_index < bone_count; ++bone_index) {
        const NifSkinBoneData& bone = data->bones[bone_index];
        for (const NifSkinWeight& weight : bone.weights) {
            if (weight.vertex_index >= vertex_count || weight.weight <= 0.0f) continue;
            out.vertex_influences[weight.vertex_index].push_back({
                static_cast<uint16_t>(bone_index),
                weight.weight
            });
        }
    }

    for (auto& influences : out.vertex_influences) {
        std::sort(influences.begin(), influences.end(),
            [](const NifRenderSkinInfluence& a, const NifRenderSkinInfluence& b) {
                return a.weight > b.weight;
            });
    }
}

} // namespace

NifExtractionResult extractNifGeometry(const NifFile& nif,
                                        float pos_x, float pos_y, float pos_z,
                                        float scale) {
    NifExtractionResult result;

    for (const auto& mesh : nif.meshes) {
        if (mesh.vertices.empty() || mesh.triangles.empty()) continue;

        NifExtractedMesh em;
        em.vertices.reserve(mesh.vertices.size() * 3);
        em.normals.reserve(mesh.vertices.size() * 3);
        em.uvs.reserve(mesh.vertices.size() * 2);

        for (const auto& v : mesh.vertices) {
            em.vertices.push_back(v.position.x * scale + pos_x);
            em.vertices.push_back(v.position.y * scale + pos_y);
            em.vertices.push_back(v.position.z * scale + pos_z);
            em.normals.push_back(v.normal.x);
            em.normals.push_back(v.normal.y);
            em.normals.push_back(v.normal.z);
            em.uvs.push_back(v.u);
            em.uvs.push_back(v.v);
        }

        em.indices.reserve(mesh.triangles.size() * 3);
        for (const auto& tri : mesh.triangles) {
            if (tri.a < mesh.vertices.size() && tri.b < mesh.vertices.size() &&
                tri.c < mesh.vertices.size()) {
                em.indices.push_back(tri.a);
                em.indices.push_back(tri.b);
                em.indices.push_back(tri.c);
            }
        }

        em.block_index = mesh.block_index;

        result.total_vertices += static_cast<uint32_t>(mesh.vertices.size());
        result.total_triangles += static_cast<uint32_t>(mesh.triangles.size());
        result.meshes.push_back(std::move(em));
    }

    return result;
}

NifRenderExtractionResult extractNifRenderGeometry(const NifFile& nif) {
    NifRenderExtractionResult result;

    // Build block-index maps once so property and data refs can be resolved
    // without changing the parsed NIF model or duplicating lookup loops in each
    // viewer/importer.
    std::unordered_map<uint32_t, const NifNode*> node_by_block;
    for (const auto& node : nif.nodes) {
        node_by_block[node.block_index] = &node;
    }

    std::unordered_map<uint32_t, uint32_t> parent_by_block;
    for (const auto& node : nif.nodes) {
        for (int32_t child_ref : node.children) {
            if (child_ref < 0) continue;
            uint32_t child_block = static_cast<uint32_t>(child_ref);
            if (node_by_block.find(child_block) != node_by_block.end() &&
                parent_by_block.find(child_block) == parent_by_block.end()) {
                parent_by_block[child_block] = node.block_index;
            }
        }
    }

    std::unordered_map<uint32_t, const NifMesh*> mesh_by_block;
    for (const auto& mesh : nif.meshes) {
        mesh_by_block[mesh.block_index] = &mesh;
    }

    std::unordered_map<uint32_t, const NifMaterial*> material_by_block;
    for (const auto& material : nif.materials) {
        material_by_block[material.block_index] = &material;
    }

    std::unordered_map<uint32_t, const NifTexturingProperty*> texturing_by_block;
    for (const auto& prop : nif.texturing_properties) {
        texturing_by_block[prop.block_index] = &prop;
    }

    std::unordered_map<uint32_t, const NifTextureRef*> texture_by_block;
    for (const auto& tex : nif.textures) {
        texture_by_block[tex.block_index] = &tex;
    }

    std::unordered_map<uint32_t, const NifAlphaProperty*> alpha_by_block;
    for (const auto& alpha : nif.alpha_properties) {
        alpha_by_block[alpha.block_index] = &alpha;
    }

    std::unordered_map<uint32_t, const NifRangeLODData*> range_lod_by_block;
    for (const auto& range_data : nif.range_lod_data) {
        range_lod_by_block[range_data.block_index] = &range_data;
    }

    std::unordered_map<uint32_t, const NifSkinInstance*> skin_instance_by_block;
    for (const auto& skin : nif.skin_instances) {
        skin_instance_by_block[skin.block_index] = &skin;
    }

    std::unordered_map<uint32_t, const NifSkinData*> skin_data_by_block;
    for (const auto& skin : nif.skin_data) {
        skin_data_by_block[skin.block_index] = &skin;
    }

    std::unordered_map<uint32_t, Transform> world_by_block;
    std::unordered_map<uint32_t, LodAssignment> lod_by_node;

    for (const auto& lod_node : nif.nodes) {
        if (lod_node.type_name != "NiLODNode") continue;

        const NifRangeLODData* range_data = find_by_block(range_lod_by_block, lod_node.lod_data_ref);
        const auto& ranges = range_data ? range_data->ranges : lod_node.lod_ranges;
        if (ranges.empty()) continue;

        Vec3 center = range_data ? range_data->center : lod_node.lod_center;
        std::unordered_set<uint32_t> visiting_transform;
        Transform lod_world = compute_world_transform(
            lod_node, node_by_block, parent_by_block, world_by_block, visiting_transform);
        Vec3 world_center = transform_point(lod_world, center);

        const size_t count = std::min(lod_node.children.size(), ranges.size());
        for (size_t level = 0; level < count; ++level) {
            int32_t child_ref = lod_node.children[level];
            if (child_ref < 0) continue;

            LodAssignment assignment;
            assignment.parent_block = lod_node.block_index;
            assignment.level = static_cast<uint32_t>(level);
            assignment.near_distance = ranges[level].first;
            assignment.far_distance = ranges[level].second;
            assignment.center = world_center;

            std::unordered_set<uint32_t> visiting_lod;
            assign_lod_to_descendants(
                static_cast<uint32_t>(child_ref), assignment, node_by_block, lod_by_node, visiting_lod);
        }
    }

    for (const auto& node : nif.nodes) {
        // A renderable mesh is defined by a scene node referencing geometry.
        // Loose geometry blocks are skipped on purpose; rendering them would
        // mask missing/broken node refs and lose authored transforms/properties.
        if (node.data_ref < 0) continue;
        auto mesh_it = mesh_by_block.find(static_cast<uint32_t>(node.data_ref));
        if (mesh_it == mesh_by_block.end()) continue;

        const NifMesh& src = *mesh_it->second;
        if (src.vertices.empty() || src.triangles.empty()) continue;

        std::unordered_set<uint32_t> visiting;
        Transform transform = compute_world_transform(
            node, node_by_block, parent_by_block, world_by_block, visiting);

        NifRenderMesh out;
        out.name = node.name.empty() ? "NIF Mesh" : node.name;
        out.source_mesh_block = src.block_index;
        out.source_node_block = node.block_index;
        out.has_vertex_colors = src.has_vertex_colors;
        auto lod_it = lod_by_node.find(node.block_index);
        if (lod_it != lod_by_node.end()) {
            out.has_lod_range = true;
            out.lod_parent_block = lod_it->second.parent_block;
            out.lod_level = lod_it->second.level;
            out.lod_near = lod_it->second.near_distance;
            out.lod_far = lod_it->second.far_distance;
            out.lod_center[0] = lod_it->second.center.x;
            out.lod_center[1] = lod_it->second.center.y;
            out.lod_center[2] = lod_it->second.center.z;
        }
        out.material = build_material_for_node(
            node, material_by_block, texturing_by_block, texture_by_block, alpha_by_block);
        populate_skinning(
            node, src.vertices.size(), node_by_block, skin_instance_by_block, skin_data_by_block, out);

        out.vertices.reserve(src.vertices.size());
        for (size_t vertex_index = 0; vertex_index < src.vertices.size(); ++vertex_index) {
            const auto& v = src.vertices[vertex_index];
            NifRenderVertex rv;
            Vec3 p = transform_point(transform, v.position);
            Vec3 n = rotate_vec(transform.rotation, v.normal);

            rv.position[0] = p.x;
            rv.position[1] = p.y;
            rv.position[2] = p.z;
            rv.normal[0] = n.x;
            rv.normal[1] = n.y;
            rv.normal[2] = n.z;
            rv.uv[0] = v.u;
            rv.uv[1] = v.v;
            if (!src.extra_uv_sets.empty() && vertex_index < src.extra_uv_sets[0].size()) {
                rv.uv2[0] = src.extra_uv_sets[0][vertex_index].u;
                rv.uv2[1] = src.extra_uv_sets[0][vertex_index].v;
            } else {
                rv.uv2[0] = v.u;
                rv.uv2[1] = v.v;
            }
            rv.color[0] = v.r;
            rv.color[1] = v.g;
            rv.color[2] = v.b;
            rv.color[3] = v.a;
            out.vertices.push_back(rv);
        }

        out.indices.reserve(src.triangles.size() * 3);
        for (const auto& tri : src.triangles) {
            if (tri.a < src.vertices.size() && tri.b < src.vertices.size() &&
                tri.c < src.vertices.size()) {
                out.indices.push_back(tri.a);
                out.indices.push_back(tri.b);
                out.indices.push_back(tri.c);
            }
        }

        result.total_vertices += static_cast<uint32_t>(out.vertices.size());
        result.total_triangles += static_cast<uint32_t>(out.indices.size() / 3);
        result.meshes.push_back(std::move(out));
    }

    return result;
}

} // namespace lu::assets
