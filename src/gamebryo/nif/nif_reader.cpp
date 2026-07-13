#include "gamebryo/nif/nif_reader.h"
#include "common/binary_reader/binary_reader.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>
#include <unordered_set>

namespace lu::assets {

namespace {

// ============================================================================
// Header helpers
// ============================================================================

// Find the newline after the text header line.
// Header format: "Gamebryo File Format, Version X.X.X.X\n"
size_t find_header_end(std::span<const uint8_t> data) {
    for (size_t i = 0; i < std::min(data.size(), size_t(256)); ++i) {
        if (data[i] == 0x0A) return i + 1;
    }
    throw NifError("NIF: could not find header newline");
}

// Sanity-check a list/array count read from the file before resize()-ing a vector to it.
// A malformed or divergent-content file can put a huge garbage value in a count field;
// without this, resize() attempts a multi-gigabyte allocation that isn't a crash but is
// effectively a hang (observed: several GB/sec of RSS growth, no forward progress) rather
// than the "block extends past end of file" error the same file throws when truncated
// right after the same count field — i.e. the eventual out-of-bounds read that would
// catch this is reached far too late, after the damage (allocation stall) is already
// done. Since every element needs at least 1 byte on disk, count can never legitimately
// exceed the file's remaining bytes.
uint32_t bounded_count(BinaryReader& r, uint32_t count, const char* what) {
    if (count > r.remaining()) {
        throw NifError(std::string("NIF: ") + what + " count " + std::to_string(count) +
                        " exceeds remaining file data");
    }
    return count;
}

// ============================================================================
// String table resolution
// ============================================================================

// Resolve a string table index. Returns "" for -1 or out-of-range.
std::string resolve_string(const std::vector<std::string>& string_table, int32_t idx) {
    if (idx < 0 || static_cast<size_t>(idx) >= string_table.size()) {
        return {};
    }
    return string_table[static_cast<size_t>(idx)];
}

// ============================================================================
// NiObjectNET: name, extra data refs, controller ref
// Present in NiNode, NiTriShape, NiTriStrips, NiCamera, NiMaterialProperty, etc.
// ============================================================================

struct NiObjectNETResult {
    std::string name;
    int32_t controller_ref = -1;
    std::vector<int32_t> extra_data_refs;
};

NiObjectNETResult read_ni_object_net(BinaryReader& r, uint32_t version,
                                      const std::vector<std::string>& string_table) {
    NiObjectNETResult result;

    // Name: string table index (version >= 20.1.0.3) or inline string
    if (version >= 0x14010003) {
        int32_t name_idx = r.read_s32();
        result.name = resolve_string(string_table, name_idx);
    } else {
        result.name = r.read_string32();
    }

    // Extra data list (version >= 10.0.1.0): count + array of i32 block refs.
    // Block indices of attached NiExtraData objects (NiStringExtraData, NiTextKeyExtraData, etc.)
    if (version >= 0x0A000100) {
        uint32_t num_extra = r.read_u32();
        result.extra_data_refs.resize(bounded_count(r, num_extra, "extra_data_refs"));
        for (uint32_t i = 0; i < num_extra; ++i) {
            result.extra_data_refs[i] = r.read_s32();
        }
    }

    // Controller ref (i32, -1 = none)
    result.controller_ref = r.read_s32();

    return result;
}

// ============================================================================
// NiAVObject: flags, transform, properties, collision ref
// Inherits NiObjectNET. Present in NiNode, NiTriShape, NiTriStrips, NiCamera.
//
// CRITICAL: flags is ALWAYS u16. The original client reads u16 here.
// A previous version of this parser incorrectly read u32 for version >= 20.2.0.7,
// which caused the NiNode parser to read 2 bytes of the translation as part of
// flags, shifting all subsequent field reads and causing hangs.
// ============================================================================

void read_ni_av_object(BinaryReader& r, uint32_t version, NifNode& node,
                       const std::vector<std::string>& string_table) {
    auto net = read_ni_object_net(r, version, string_table);
    node.name = std::move(net.name);
    node.controller_ref = net.controller_ref;
    node.extra_data_refs = std::move(net.extra_data_refs);

    // Flags: ALWAYS u16 (verified against Ghidra RE)
    node.flags = r.read_u16();

    // Translation: 3x f32
    node.translation.x = r.read_f32();
    node.translation.y = r.read_f32();
    node.translation.z = r.read_f32();

    // Rotation matrix: 3x3 = 9 floats.
    //
    // IMPORTANT: Gamebryo stores matrices in ROW-MAJOR order and applies
    // them with ROW vectors (v_world = v_local * M). Standard GLM uses
    // COLUMN-MAJOR storage with COLUMN vectors (v_world = M * v_local).
    // For the same rotation R, M_gamebryo = M_standard^T (transposes).
    //
    // Verified against Ghidra RE of NiQuaternion::ToRotation @ 0040ff40:
    // the matrix built from a quaternion there is the transpose of the
    // GLM/standard RH convention matrix. Writing bytes from the file
    // into m[row][col] stores the Gamebryo-layout matrix directly; we
    // transpose on read so Shepperd's extraction (which assumes the
    // standard convention) yields the correct quaternion.
    float m[3][3];
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            m[col][row] = r.read_f32();  // transpose during read
        }
    }

    // Matrix-to-quaternion conversion (Shepperd's method)
    float trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        node.rotation.w = 0.25f * s;
        node.rotation.x = (m[2][1] - m[1][2]) / s;
        node.rotation.y = (m[0][2] - m[2][0]) / s;
        node.rotation.z = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
        node.rotation.w = (m[2][1] - m[1][2]) / s;
        node.rotation.x = 0.25f * s;
        node.rotation.y = (m[0][1] + m[1][0]) / s;
        node.rotation.z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
        node.rotation.w = (m[0][2] - m[2][0]) / s;
        node.rotation.x = (m[0][1] + m[1][0]) / s;
        node.rotation.y = 0.25f * s;
        node.rotation.z = (m[1][2] + m[2][1]) / s;
    } else {
        float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
        node.rotation.w = (m[1][0] - m[0][1]) / s;
        node.rotation.x = (m[0][2] + m[2][0]) / s;
        node.rotation.y = (m[1][2] + m[2][1]) / s;
        node.rotation.z = 0.25f * s;
    }

    // Scale: f32
    node.scale = r.read_f32();

    // Properties (version >= 10.0.1.0): count + array of i32 block refs
    if (version >= 0x0A000100) {
        uint32_t num_props = r.read_u32();
        node.properties.resize(bounded_count(r, num_props, "properties"));
        for (uint32_t i = 0; i < num_props; ++i) {
            node.properties[i] = r.read_s32();
        }
    }

    // Collision object ref (version >= 10.0.1.0): i32 (-1 = none)
    if (version >= 0x0A000100) {
        node.collision_ref = r.read_s32();
    }
}

// ============================================================================
// NiNode: scene hierarchy node with children and effects
// ============================================================================

NifNode parse_ni_node(BinaryReader& r, uint32_t version,
                      const std::vector<std::string>& string_table) {
    NifNode node;
    node.type_name = "NiNode";
    read_ni_av_object(r, version, node, string_table);

    // Children: count + array of i32 block refs
    uint32_t num_children = r.read_u32();
    node.children.resize(bounded_count(r, num_children, "children"));
    for (uint32_t i = 0; i < num_children; ++i) {
        node.children[i] = r.read_s32();
    }

    // Effects: count + array of i32 block refs.
    // Block indices of attached NiDynamicEffect objects (lights, environment maps, etc.)
    uint32_t num_effects = r.read_u32();
    node.effect_refs.resize(bounded_count(r, num_effects, "effect_refs"));
    for (uint32_t i = 0; i < num_effects; ++i) {
        node.effect_refs[i] = r.read_s32();
    }

    return node;
}

NifNode parse_ni_sort_adjust_node(BinaryReader& r, uint32_t version,
                                  const std::vector<std::string>& string_table,
                                  size_t block_end) {
    NifNode node = parse_ni_node(r, version, string_table);
    node.type_name = "NiSortAdjustNode";
    if (r.pos() + 4 <= block_end) {
        node.has_sorting_mode = true;
        node.sorting_mode = r.read_u32();
    }
    // NiAccumulator was removed after 20.0.0.3 and is absent from LU 20.3.0.9.
    return node;
}

// ============================================================================
// NiLODNode: same as NiNode but with LOD center and range data
// ============================================================================

NifNode parse_ni_lod_node(BinaryReader& r, uint32_t version,
                          const std::vector<std::string>& string_table,
                          size_t block_end) {
    NifNode node = parse_ni_node(r, version, string_table);
    node.type_name = "NiLODNode";

    // LOD data: LU's newer files store a level count, a small active-level marker,
    // then a block ref to NiRangeLODData. Older files may inline the ranges.
    if (version >= 0x0A010000) {
        size_t remaining = block_end > r.pos() ? block_end - r.pos() : 0;
        if (remaining >= 10) {
            node.lod_level_count = r.read_u32();
            node.lod_active_level = r.read_u16();
            node.lod_data_ref = r.read_s32();
        } else if (remaining >= 4) {
            node.lod_data_ref = r.read_s32();
        }
    } else {
        // Inline: LOD center + levels
        node.lod_center.x = r.read_f32();
        node.lod_center.y = r.read_f32();
        node.lod_center.z = r.read_f32();
        uint32_t num_levels = r.read_u32();
        node.lod_ranges.resize(bounded_count(r, num_levels, "lod_ranges"));
        for (uint32_t i = 0; i < num_levels; ++i) {
            node.lod_ranges[i].first = r.read_f32();  // near
            node.lod_ranges[i].second = r.read_f32(); // far
        }
    }

    return node;
}

NifRangeLODData parse_range_lod_data(BinaryReader& r) {
    NifRangeLODData data;
    data.center.x = r.read_f32();
    data.center.y = r.read_f32();
    data.center.z = r.read_f32();

    uint32_t num_ranges = r.read_u32();
    data.ranges.resize(num_ranges);
    for (uint32_t i = 0; i < num_ranges; ++i) {
        data.ranges[i].first = r.read_f32();
        data.ranges[i].second = r.read_f32();
    }
    return data;
}

Quat read_rotation_matrix_as_quat(BinaryReader& r) {
    float m[3][3];
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            m[col][row] = r.read_f32();
        }
    }

    Quat q;
    float trace = m[0][0] + m[1][1] + m[2][2];
    if (trace > 0.0f) {
        float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m[2][1] - m[1][2]) / s;
        q.y = (m[0][2] - m[2][0]) / s;
        q.z = (m[1][0] - m[0][1]) / s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;
        q.w = (m[2][1] - m[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (m[0][1] + m[1][0]) / s;
        q.z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;
        q.w = (m[0][2] - m[2][0]) / s;
        q.x = (m[0][1] + m[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (m[1][2] + m[2][1]) / s;
    } else {
        float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;
        q.w = (m[1][0] - m[0][1]) / s;
        q.x = (m[0][2] + m[2][0]) / s;
        q.y = (m[1][2] + m[2][1]) / s;
        q.z = 0.25f * s;
    }
    return q;
}

void read_skin_transform(BinaryReader& r, Vec3& translation, Quat& rotation, float& scale) {
    rotation = read_rotation_matrix_as_quat(r);
    translation.x = r.read_f32();
    translation.y = r.read_f32();
    translation.z = r.read_f32();
    scale = r.read_f32();
}

NifSkinInstance parse_skin_instance(BinaryReader& r) {
    NifSkinInstance skin;
    skin.data_ref = r.read_s32();
    skin.skin_partition_ref = r.read_s32();
    skin.skeleton_root_ref = r.read_s32();

    uint32_t num_bones = r.read_u32();
    skin.bone_refs.resize(num_bones);
    for (uint32_t i = 0; i < num_bones; ++i) {
        skin.bone_refs[i] = r.read_s32();
    }
    return skin;
}

NifSkinData parse_skin_data(BinaryReader& r) {
    NifSkinData skin;
    read_skin_transform(r, skin.translation, skin.rotation, skin.scale);

    uint32_t num_bones = r.read_u32();
    skin.has_vertex_weights = r.read_bool();
    skin.bones.resize(num_bones);
    for (uint32_t bone_index = 0; bone_index < num_bones; ++bone_index) {
        NifSkinBoneData& bone = skin.bones[bone_index];
        read_skin_transform(r, bone.translation, bone.rotation, bone.scale);
        bone.bound_center.x = r.read_f32();
        bone.bound_center.y = r.read_f32();
        bone.bound_center.z = r.read_f32();
        bone.bound_radius = r.read_f32();

        uint16_t num_weights = r.read_u16();
        if (skin.has_vertex_weights) {
            bone.weights.resize(num_weights);
            for (uint16_t weight_index = 0; weight_index < num_weights; ++weight_index) {
                bone.weights[weight_index].vertex_index = r.read_u16();
                bone.weights[weight_index].weight = r.read_f32();
            }
        }
    }
    return skin;
}

// ============================================================================
// NiTriShape / NiTriStrips: geometry container with data ref
// ============================================================================

NifNode parse_ni_tri_shape(BinaryReader& r, uint32_t version,
                           const std::string& type,
                           const std::vector<std::string>& string_table) {
    NifNode node;
    node.type_name = type;
    read_ni_av_object(r, version, node, string_table);

    // Data ref: block index of NiTriShapeData or NiTriStripsData
    node.data_ref = r.read_s32();

    // Skin instance ref
    node.skin_instance_ref = r.read_s32();

    // Material binding (version >= 10.0.1.0).
    // material_name_indices[i] = string table index of material name i.
    // material_extra_data_refs[i] = block index of per-material extra data (-1 = none).
    // active_material = index into the above arrays (-1 = default).
    if (version >= 0x0A000100) {
        uint32_t num_materials = r.read_u32();
        node.material_name_indices.resize(bounded_count(r, num_materials, "material_name_indices"));
        for (uint32_t i = 0; i < num_materials; ++i) {
            node.material_name_indices[i] = r.read_s32();
        }
        node.material_extra_data_refs.resize(bounded_count(r, num_materials, "material_extra_data_refs"));
        for (uint32_t i = 0; i < num_materials; ++i) {
            node.material_extra_data_refs[i] = r.read_s32();
        }
        node.active_material = r.read_s32();
    }

    // Shader property + alpha property (version >= 20.2.0.7)
    if (version >= 0x14020007) {
        r.read_s32(); // shader property ref
        r.read_s32(); // alpha property ref
    }

    return node;
}

// ============================================================================
// Shared geometry data parser (NiTriBasedGeomData base)
// Used by both NiTriShapeData and NiTriStripsData.
// ============================================================================

void parse_geom_data_header(BinaryReader& r, uint32_t version, NifMesh& mesh) {
    // Group ID (version >= 20.3.0.9)
    if (version >= 0x14030009) {
        mesh.group_id = r.read_u32();
    }

    uint16_t num_verts = r.read_u16();

    // Keep/compress flags (version >= 10.1.0.0)
    if (version >= 0x0A010000) {
        mesh.keep_flags = r.read_u16();
    }

    // Vertices
    bool has_vertices = r.read_bool();
    mesh.vertices.resize(bounded_count(r, num_verts, "vertices"));
    if (has_vertices) {
        for (uint16_t i = 0; i < num_verts; ++i) {
            mesh.vertices[i].position.x = r.read_f32();
            mesh.vertices[i].position.y = r.read_f32();
            mesh.vertices[i].position.z = r.read_f32();
        }
    }

    // Vector flags (version >= 10.0.1.0)
    // Low 6 bits: number of UV sets
    // Bit 12: has tangent space data
    if (version >= 0x0A000100) {
        mesh.vector_flags = r.read_u16();
    }
    uint16_t num_uv_sets = mesh.vector_flags & 0x3F;
    mesh.has_tangents = (mesh.vector_flags & (1 << 12)) != 0;

    // Normals
    bool has_normals = r.read_bool();
    if (has_normals) {
        for (uint16_t i = 0; i < num_verts; ++i) {
            mesh.vertices[i].normal.x = r.read_f32();
            mesh.vertices[i].normal.y = r.read_f32();
            mesh.vertices[i].normal.z = r.read_f32();
        }
        // Tangents and bitangents (if tangent space flag set)
        if (mesh.has_tangents) {
            for (uint16_t i = 0; i < num_verts; ++i) {
                mesh.vertices[i].tangent.x = r.read_f32();
                mesh.vertices[i].tangent.y = r.read_f32();
                mesh.vertices[i].tangent.z = r.read_f32();
            }
            for (uint16_t i = 0; i < num_verts; ++i) {
                mesh.vertices[i].bitangent.x = r.read_f32();
                mesh.vertices[i].bitangent.y = r.read_f32();
                mesh.vertices[i].bitangent.z = r.read_f32();
            }
        }
    }

    // Bounding sphere: center (3x f32) + radius (f32)
    mesh.bound_center.x = r.read_f32();
    mesh.bound_center.y = r.read_f32();
    mesh.bound_center.z = r.read_f32();
    mesh.bound_radius = r.read_f32();

    // Vertex colors
    bool has_colors = r.read_bool();
    if (has_colors) {
        mesh.has_vertex_colors = true;
        for (uint16_t i = 0; i < num_verts; ++i) {
            mesh.vertices[i].r = r.read_f32();
            mesh.vertices[i].g = r.read_f32();
            mesh.vertices[i].b = r.read_f32();
            mesh.vertices[i].a = r.read_f32();
        }
    }

    // UV sets
    if (num_uv_sets > 0) {
        // First UV set goes into vertices
        for (uint16_t i = 0; i < num_verts; ++i) {
            mesh.vertices[i].u = r.read_f32();
            mesh.vertices[i].v = r.read_f32();
        }
        // Extra UV sets (beyond the first): store per-set, per-vertex
        for (uint16_t s = 1; s < num_uv_sets; ++s) {
            std::vector<NifUV> uv_set(num_verts);
            for (uint16_t i = 0; i < num_verts; ++i) {
                uv_set[i].u = r.read_f32();
                uv_set[i].v = r.read_f32();
            }
            mesh.extra_uv_sets.push_back(std::move(uv_set));
        }
    }

    // Consistency flags (version >= 10.0.1.0)
    if (version >= 0x0A000100) {
        mesh.consistency_flags = r.read_u16();
    }

    // Additional data ref (version >= 20.0.0.4)
    if (version >= 0x14000004) {
        mesh.additional_data_ref = r.read_s32();
    }
}

// ============================================================================
// NiTriShapeData: indexed triangles
// ============================================================================

NifMesh parse_tri_shape_data(BinaryReader& r, uint32_t version) {
    NifMesh mesh;
    mesh.block_type = "NiTriShapeData";

    parse_geom_data_header(r, version, mesh);

    // Triangle count (NiTriBasedGeomData)
    uint16_t num_triangles = r.read_u16();

    // Total triangle point count (u32, informational)
    r.read_u32();

    // Triangle indices
    bool has_triangles = r.read_bool();
    if (has_triangles && num_triangles > 0) {
        mesh.triangles.resize(bounded_count(r, num_triangles, "triangles"));
        for (uint16_t i = 0; i < num_triangles; ++i) {
            mesh.triangles[i].a = r.read_u16();
            mesh.triangles[i].b = r.read_u16();
            mesh.triangles[i].c = r.read_u16();
        }
    }

    // Match groups (version >= 10.0.1.0)
    if (version >= 0x0A000100) {
        uint16_t num_match_groups = r.read_u16();
        mesh.match_groups.resize(bounded_count(r, num_match_groups, "match_groups"));
        for (uint16_t mg = 0; mg < num_match_groups; ++mg) {
            uint16_t count = r.read_u16();
            mesh.match_groups[mg].indices.resize(bounded_count(r, count, "match_group indices"));
            for (uint16_t i = 0; i < count; ++i) {
                mesh.match_groups[mg].indices[i] = r.read_u16();
            }
        }
    }

    return mesh;
}

// ============================================================================
// NiTriStripsData: triangle strips
// ============================================================================

NifMesh parse_tri_strips_data(BinaryReader& r, uint32_t version) {
    NifMesh mesh;
    mesh.block_type = "NiTriStripsData";

    parse_geom_data_header(r, version, mesh);

    // Triangle count (NiTriBasedGeomData, informational for strips)
    r.read_u16();

    // Strip data
    uint16_t num_strips = r.read_u16();
    std::vector<uint16_t> strip_lengths(num_strips);
    for (uint16_t s = 0; s < num_strips; ++s) {
        strip_lengths[s] = r.read_u16();
    }

    bool has_points = r.read_bool();
    if (has_points) {
        for (uint16_t s = 0; s < num_strips; ++s) {
            std::vector<uint16_t> indices(strip_lengths[s]);
            for (uint16_t i = 0; i < strip_lengths[s]; ++i) {
                indices[i] = r.read_u16();
            }
            // Convert strip to triangles with correct winding
            for (uint16_t i = 2; i < strip_lengths[s]; ++i) {
                NifTriangle tri;
                if (i % 2 == 0) {
                    tri = {indices[i - 2], indices[i - 1], indices[i]};
                } else {
                    tri = {indices[i - 1], indices[i - 2], indices[i]};
                }
                // Skip degenerate triangles
                if (tri.a != tri.b && tri.b != tri.c && tri.a != tri.c) {
                    mesh.triangles.push_back(tri);
                }
            }
        }
    }

    return mesh;
}

// ============================================================================
// NiMaterialProperty: material colors and opacity
// Inherits NiProperty -> NiObjectNET (NOT NiAVObject -- no transform).
//
// NiProperty::Flags only exists in versions <= 10.0.0.0 (nifxml condition).
// LU files are version 20.3.0.9 and have NO flags field here.
// Verified by byte-tracing: block size = 68 = 12 (NiObjectNET) + 56 (colors).
// ============================================================================

NifMaterial parse_material_property(BinaryReader& r, uint32_t version,
                                    const std::vector<std::string>& string_table) {
    NifMaterial mat;
    auto net = read_ni_object_net(r, version, string_table);
    mat.name = std::move(net.name);

    // NiProperty::Flags: only present in versions <= 10.0.0.0
    if (version <= 0x0A000100) {
        mat.flags = r.read_u16();
    }

    // Colors: ambient, diffuse, specular, emissive (each 3x f32)
    mat.ambient  = {r.read_f32(), r.read_f32(), r.read_f32()};
    mat.diffuse  = {r.read_f32(), r.read_f32(), r.read_f32()};
    mat.specular = {r.read_f32(), r.read_f32(), r.read_f32()};
    mat.emissive = {r.read_f32(), r.read_f32(), r.read_f32()};
    mat.glossiness = r.read_f32();
    mat.alpha = r.read_f32();

    return mat;
}

// ============================================================================
// NiSourceTexture: the actual texture file/pixel-data reference.
// Inherits NiTexture -> NiObjectNET (NiTexture itself adds no fields).
// Verified field-by-field against nif.xml (public Gamebryo format spec — matches the
// CLAUDE.md "NIF/Ni* use NifSkope/nif.xml, not Ghidra" convention), niobject NiSourceTexture:
//   Use External: byte
//   [Use External==0 && until 10.0.1.3] Use Internal: byte  -- absent, LU is 20.3.0.9
//   File Name: FilePath, cond Use External==1
//     -- FilePath is `SizedString until 20.0.0.5` / `NiFixedString (u32 string-table index)
//        since 20.1.0.3` -- LU (20.3.0.9) uses the string-table-index form, NOT a raw string32.
//   File Name: FilePath, cond Use External==0 && since 10.1.0.0 (embedded-image original name)
//   Pixel Data: Ref<NiPixelFormat>, cond Use External==1, since 10.1.0.0
//   Pixel Data: Ref<NiPixelFormat>, cond Use External==0, since 10.0.1.4
//   Format Prefs: FormatPrefs (12 bytes: PixelLayout u32, MipMapFormat u32, AlphaFormat u32)
//   Is Static: byte
//   Direct Render: bool, since 10.1.0.103
//   Persist Render Data: bool, since 20.2.0.4
// ============================================================================

NifTextureRef parse_source_texture(BinaryReader& r, uint32_t version,
                                    const std::vector<std::string>& string_table) {
    NifTextureRef tex;
    auto net = read_ni_object_net(r, version, string_table);
    (void)net; // name not currently surfaced on NifTextureRef

    tex.use_external = r.read_u8() != 0; // "Use External": byte, not bool-sized differently

    if (tex.use_external) {
        // FilePath, since 20.1.0.3 (LU: 20.3.0.9) is a NiFixedString: u32 string-table index.
        if (version >= 0x14010003) {
            tex.filename = resolve_string(string_table, r.read_s32());
        } else {
            tex.filename = r.read_string32();
        }
    } else if (version >= 0x0A010000) {
        // Embedded-image original filename (same FilePath encoding), then Pixel Data ref.
        if (version >= 0x14010003) {
            tex.filename = resolve_string(string_table, r.read_s32());
        } else {
            tex.filename = r.read_string32();
        }
    }

    // Pixel Data ref: present for (external, since 10.1.0.0) or (internal, since 10.0.1.4) —
    // both hold for LU's 20.3.0.9 regardless of use_external.
    tex.pixel_data_ref = r.read_s32();

    tex.pixel_layout = r.read_u32();
    tex.mipmap_format = r.read_u32();
    tex.alpha_format = r.read_u32();
    tex.is_static = r.read_u8() != 0;
    if (version >= 0x0A010067) { // since="10.1.0.103"
        tex.direct_render = r.read_bool();
    }
    if (version >= 0x14020004) { // since="20.2.0.4"
        tex.persist_render_data = r.read_bool();
    }

    return tex;
}

// ============================================================================
// NiTexturingProperty: the texture-slot table (base/dark/detail/gloss/glow/bump/decal/normal/
// parallax/shader). Inherits NiProperty -> NiObjectNET (no transform, no extra NiProperty
// fields — NiProperty itself is fieldless).
// Verified field-by-field against nif.xml at LU's version (20.3.0.9):
//   Flags: TexturingFlags (ushort), since 20.1.0.2 -- ALWAYS present at this version (not
//     gated the way NiMaterialProperty's bare NiProperty flags are; this is NiTexturingProperty's
//     OWN typed Flags field, unconditional past 10.0.1.2).
//   Apply Mode: ApplyMode (u32), since 3.3.0.13 until 20.1.0.1 -- absent for LU (past 20.1.0.1).
//   Texture Count: uint, default 7 -- ALWAYS present.
//   Has Base/Dark/Detail/Gloss/Glow Texture: bool + TexDesc each.
//   Has Bump Map Texture: bool cond TextureCount>5, + TexDesc + 2 floats (luma scale/offset) +
//     Matrix22 (bump matrix, 4 floats) if present.
//   Has Normal/Parallax Texture: bool cond TextureCount>6/7, since 20.2.0.5 -- LU is 20.3.0.9
//     so these slots exist when Texture Count (7 by default) supports them.
//   Has Decal 0-3 Texture: bool cond TextureCount > 6/7/8/9 (since 20.2.0.5 thresholds) + TexDesc.
//   Num Shader Textures: uint, since 10.0.1.0 -- ALWAYS present (no "has" bool gating it).
//   Shader Textures: ShaderTexDesc[Num Shader Textures].
// TexDesc at LU's version (>= 20.1.0.3, so Clamp Mode/Filter Mode are REPLACED by a single
// packed Flags field; UV Set field removed at this version too; Max Anisotropy absent, since
// only 20.5.0.4):
//   Source: Ref<NiSourceTexture> (i32)
//   Flags: TexturingMapFlags (ushort), since 20.1.0.3
//   Has Texture Transform: bool, since 10.1.0.0
//   [if has transform] Translation/Scale/Center: TexCoord (2 floats each), Rotation: float,
//     Transform Method: TransformMethod (u32).
// ============================================================================

struct TexDescInfo {
    int32_t source_ref = -1;
    bool has_clamp_mode = false;
    uint8_t clamp_mode = 3;
    NifTextureTransform transform;
};

TexDescInfo read_tex_desc(BinaryReader& r, uint32_t version) {
    TexDescInfo desc;
    desc.source_ref = r.read_s32(); // Source: Ref<NiSourceTexture>

    if (version >= 0x14010003) { // since="20.1.0.3"
        uint16_t flags = r.read_u16(); // TexturingMapFlags: low two bits are TexClampMode.
        desc.has_clamp_mode = true;
        desc.clamp_mode = static_cast<uint8_t>(flags & 0x0003u);
    } else {
        uint32_t clamp_mode = r.read_u32(); // Clamp Mode
        desc.has_clamp_mode = true;
        desc.clamp_mode = static_cast<uint8_t>(clamp_mode & 0x00000003u);
        r.read_u32(); // Filter Mode
        if (version >= 0x0A010000 && version < 0x14010003) {
            r.read_u32(); // UV Set (removed at 20.1.0.3+)
        }
    }

    if (version >= 0x14050004) { // since="20.5.0.4"
        r.read_u16(); // Max Anisotropy
    }

    if (version >= 0x0A010000) { // "Has Texture Transform" since 10.1.0.0
        desc.transform.enabled = r.read_bool();
        if (desc.transform.enabled) {
            desc.transform.translation.u = r.read_f32();
            desc.transform.translation.v = r.read_f32();
            desc.transform.scale.u = r.read_f32();
            desc.transform.scale.v = r.read_f32();
            desc.transform.rotation = r.read_f32();
            desc.transform.method = r.read_u32();
            desc.transform.center.u = r.read_f32();
            desc.transform.center.v = r.read_f32();
        }
    }
    return desc;
}

NifTexturingProperty parse_texturing_property(BinaryReader& r, uint32_t version,
                                              const std::vector<std::string>& string_table) {
    NifTexturingProperty prop;
    auto net = read_ni_object_net(r, version, string_table);
    (void)net;

    // Flags: TexturingFlags (ushort) since 20.1.0.2 — replaces the plain ushort used
    // until="10.0.1.2"; both are 2 bytes, so a single unconditional read covers all NIF
    // versions this reader targets (LU is 20.3.0.9; nothing between 10.0.1.2 and 20.1.0.2
    // leaves the field absent per nif.xml).
    r.read_u16(); // Flags

    if (version >= 0x03030013 && version < 0x14010001) { // since 3.3.0.13, until 20.1.0.1
        r.read_u32(); // Apply Mode
    }

    uint32_t textureCount = r.read_u32(); // Texture Count

    bool hasBase = r.read_bool();
    if (hasBase) {
        TexDescInfo desc = read_tex_desc(r, version);
        prop.base_texture_source_ref = desc.source_ref;
        prop.base_texture_has_clamp_mode = desc.has_clamp_mode;
        prop.base_texture_clamp_mode = desc.clamp_mode;
        prop.base_texture_transform = desc.transform;
    }

    bool hasDark = r.read_bool();
    if (hasDark) {
        TexDescInfo desc = read_tex_desc(r, version);
        prop.dark_texture_source_ref = desc.source_ref;
        prop.dark_texture_has_clamp_mode = desc.has_clamp_mode;
        prop.dark_texture_clamp_mode = desc.clamp_mode;
        prop.dark_texture_transform = desc.transform;
    }

    bool hasDetail = r.read_bool();
    if (hasDetail) {
        TexDescInfo desc = read_tex_desc(r, version);
        prop.detail_texture_source_ref = desc.source_ref;
        prop.detail_texture_has_clamp_mode = desc.has_clamp_mode;
        prop.detail_texture_clamp_mode = desc.clamp_mode;
        prop.detail_texture_transform = desc.transform;
    }

    bool hasGloss = r.read_bool();
    if (hasGloss) {
        TexDescInfo desc = read_tex_desc(r, version);
        prop.gloss_texture_source_ref = desc.source_ref;
        prop.gloss_texture_has_clamp_mode = desc.has_clamp_mode;
        prop.gloss_texture_clamp_mode = desc.clamp_mode;
        prop.gloss_texture_transform = desc.transform;
    }

    bool hasGlow = r.read_bool();
    if (hasGlow) {
        TexDescInfo desc = read_tex_desc(r, version);
        prop.glow_texture_source_ref = desc.source_ref;
        prop.glow_texture_has_clamp_mode = desc.has_clamp_mode;
        prop.glow_texture_clamp_mode = desc.clamp_mode;
        prop.glow_texture_transform = desc.transform;
    }

    if (textureCount > 5) {
        bool hasBump = r.read_bool();
        if (hasBump) {
            read_tex_desc(r, version);
            r.read_f32(); // Bump Map Luma Scale
            r.read_f32(); // Bump Map Luma Offset
            r.read_f32(); r.read_f32(); r.read_f32(); r.read_f32(); // Bump Map Matrix (2x2)
        }
    }

    if (version >= 0x14020005) { // since="20.2.0.5"
        if (textureCount > 6) {
            bool hasNormal = r.read_bool();
            if (hasNormal) read_tex_desc(r, version);
        }
        if (textureCount > 7) {
            bool hasParallax = r.read_bool();
            if (hasParallax) {
                read_tex_desc(r, version);
                r.read_f32(); // Parallax Offset
            }
        }
    }

    // Decal 0-3: nif.xml has two threshold sets depending on version — TextureCount >
    // 6/7/8/9 until="20.2.0.4", but TextureCount > 8/9/10/11 since="20.2.0.5". LU (20.3.0.9)
    // uses the higher thresholds; verified byte-exact against a real NiTexturingProperty block
    // (cre_cp_spider.nif, TextureCount=9 → only Decal 0 present, matching 9>8 and not 9>9).
    uint32_t decalBase = (version >= 0x14020005) ? 8 : 6;
    for (int decalIdx = 0; decalIdx < 4; ++decalIdx) {
        uint32_t threshold = decalBase + static_cast<uint32_t>(decalIdx);
        if (textureCount <= threshold) break; // nif.xml conditions are strictly increasing
        bool hasDecal = r.read_bool();
        if (hasDecal) read_tex_desc(r, version);
    }

    if (version >= 0x0A000100) { // "Num Shader Textures" since 10.0.1.0, unconditional
        uint32_t numShaderTex = r.read_u32();
        for (uint32_t i = 0; i < numShaderTex; ++i) {
            bool hasMap = r.read_bool();
            if (hasMap) {
                read_tex_desc(r, version);
                r.read_u32(); // Map ID
            }
        }
    }

    return prop;
}

// ============================================================================
// NiAlphaProperty: authored alpha state.
// Inherits NiProperty -> NiObjectNET. For LU's version, NiProperty itself adds
// no fields; NiAlphaProperty stores flags plus an optional threshold byte.
// ============================================================================

NifAlphaProperty parse_alpha_property(BinaryReader& r, uint32_t version,
                                      const std::vector<std::string>& string_table,
                                      size_t block_end) {
    NifAlphaProperty prop;
    auto net = read_ni_object_net(r, version, string_table);
    (void)net;

    if (r.pos() + 2 <= block_end) {
        prop.flags = r.read_u16();
    }
    if (r.pos() + 1 <= block_end) {
        prop.threshold = r.read_u8();
    }
    return prop;
}

template <typename T>
T parse_packed_flags_property(BinaryReader& r, uint32_t version,
                              const std::vector<std::string>& string_table,
                              size_t block_end) {
    T prop;
    auto net = read_ni_object_net(r, version, string_table);
    (void)net;
    if (r.pos() + 2 <= block_end) {
        prop.flags = r.read_u16();
    }
    return prop;
}

NifStencilProperty parse_stencil_property(BinaryReader& r, uint32_t version,
                                          const std::vector<std::string>& string_table,
                                          size_t block_end) {
    NifStencilProperty prop;
    auto net = read_ni_object_net(r, version, string_table);
    (void)net;

    // LU uses the 20.1.0.3+ packed layout: flags, stencil ref, stencil mask.
    if (version >= 0x14010003) {
        if (r.pos() + 2 <= block_end) prop.flags = r.read_u16();
        if (r.pos() + 4 <= block_end) prop.stencil_ref = r.read_u32();
        if (r.pos() + 4 <= block_end) prop.stencil_mask = r.read_u32();
    }
    return prop;
}

// ============================================================================
// NiTextKeyExtraData: timestamped event markers
// Inherits NiExtraData -> NiObject.
// Layout: NiObjectNET fields (name only, no controller in NiExtraData) +
//         u32 num_keys, then (float time, u32+string text)[num_keys]
// ============================================================================

NifTextKeyExtraData parse_text_key_extra_data(BinaryReader& r, uint32_t version,
                                               const std::vector<std::string>& string_table) {
    NifTextKeyExtraData result;

    // NiObject has no fields. NiExtraData has:
    // - name (string table index for v >= 20.1.0.3)
    if (version >= 0x14010003) {
        int32_t name_idx = r.read_s32();
        result.name = resolve_string(string_table, name_idx);
    } else {
        result.name = r.read_string32();
    }

    // Number of text keys
    uint32_t num_keys = r.read_u32();
    result.keys.resize(bounded_count(r, num_keys, "keys"));
    for (uint32_t i = 0; i < num_keys; ++i) {
        result.keys[i].time = r.read_f32();
        if (version >= 0x14010003) {
            int32_t str_idx = r.read_s32();
            result.keys[i].text = resolve_string(string_table, str_idx);
        } else {
            result.keys[i].text = r.read_string32();
        }
    }

    return result;
}

// ============================================================================
// NiControllerSequence: animation clip definition.
//
// Historical note: this used to assume the nif.xml NiObjectNET layout here,
// but LU KF files store a leaner NiSequence-derived payload.
//   string sequence_name
//   u32 num_controlled_blocks
//   ControllerLink[num_controlled_blocks]  — each for v >= 20.1.0.3:
//     i32 controller_ref
//     i32 interpolator_ref
//     i32 priority
//     i32 node_name       (string table index)
//     i32 property_type   (string table index)
//     i32 controller_type (string table index)
//     i32 controller_id   (string table index)
//   i32 unknown/palette ref
//   f32 weight
//   i32 text_keys_ref
//   u32 cycle_type
//   f32 frequency
//   f32 start_time
//   f32 stop_time
//   i32 manager_ref
//   i32 accum_root_name  (string table index for v >= 20.1.0.3)
//   i32 string palette/state
// ============================================================================

// Note: LU KF NiControllerSequence blocks are NiSequence-derived payloads.
// They begin with a string-table name and controlled-block count, not a
// NiObjectNET name/extra/controller header.
NifControllerSequence parse_controller_sequence(BinaryReader& r, uint32_t version,
                                                 const std::vector<std::string>& string_table,
                                                 size_t block_end) {
    NifControllerSequence seq;

    if (version >= 0x14010003) {
        seq.name = resolve_string(string_table, r.read_s32());
    } else {
        seq.name = r.read_string32();
    }

    // Controlled blocks
    seq.num_controlled_blocks = r.read_u32();
    seq.controlled_blocks.reserve(seq.num_controlled_blocks);

    for (uint32_t i = 0; i < seq.num_controlled_blocks; ++i) {
        NifControlledBlock cb;

        cb.controller_ref = r.read_s32();
        cb.interpolator_ref = r.read_s32();

        if (version >= 0x0A010006) {
            cb.priority = static_cast<uint8_t>(std::clamp(r.read_s32(), 0, 255));
        }

        if (version >= 0x14010003) {
            // v >= 20.1.0.3: names via string table indices
            cb.node_name       = resolve_string(string_table, r.read_s32());
            cb.property_type   = resolve_string(string_table, r.read_s32());
            cb.controller_type = resolve_string(string_table, r.read_s32());
            cb.controller_id   = resolve_string(string_table, r.read_s32());
            cb.interpolator_id = {};
        } else {
            cb.node_name       = r.read_string32();
            cb.property_type   = r.read_string32();
            cb.controller_type = r.read_string32();
            cb.controller_id   = r.read_string32();
            cb.interpolator_id = r.read_string32();
        }

        seq.controlled_blocks.push_back(std::move(cb));
    }

    // Sequence-level fields (after all controlled blocks)
    const size_t tail_bytes = block_end > r.pos() ? block_end - r.pos() : 0;
    if (tail_bytes >= 40) {
        (void)r.read_s32();
    }

    seq.weight        = r.read_f32();
    seq.text_keys_ref = r.read_s32();
    seq.cycle_type    = r.read_u32();
    seq.frequency     = r.read_f32();
    seq.start_time    = r.read_f32();
    seq.stop_time     = r.read_f32();
    seq.manager_ref   = r.read_s32();

    if (version >= 0x14010003) {
        seq.accum_root_name = resolve_string(string_table, r.read_s32());
    } else {
        seq.accum_root_name = r.read_string32();
    }

    if (block_end >= r.pos() + 4) {
        (void)r.read_s32();
    }

    return seq;
}

// ============================================================================
// NiTransformInterpolator: default transform + data ref
// ============================================================================

NifTransformInterpolator parse_transform_interpolator(BinaryReader& r) {
    NifTransformInterpolator interp;

    // Default translation
    interp.translation.x = r.read_f32();
    interp.translation.y = r.read_f32();
    interp.translation.z = r.read_f32();

    // Default rotation (quaternion: w, x, y, z in NIF format)
    interp.rotation.w = r.read_f32();
    interp.rotation.x = r.read_f32();
    interp.rotation.y = r.read_f32();
    interp.rotation.z = r.read_f32();

    // Default scale
    interp.scale = r.read_f32();

    // Data ref: block index of NiTransformData (-1 = none)
    interp.data_ref = r.read_s32();

    return interp;
}

// ============================================================================
// Key reading helpers for NiTransformData and NiFloatData
// Key types: 1=LINEAR, 2=QUADRATIC, 3=TBC, 4=EULER (rotation only)
// ============================================================================

// Read float keys (LINEAR: time + value, QUADRATIC: time + value + fwd + bwd,
// TBC: time + value + tension + bias + continuity)
void read_float_keys(BinaryReader& r, uint32_t num_keys, uint32_t key_type,
                     std::vector<NifKeyFloat>& keys) {
    keys.resize(bounded_count(r, num_keys, "keys"));
    for (uint32_t i = 0; i < num_keys; ++i) {
        keys[i].time = r.read_f32();
        keys[i].value = r.read_f32();
        if (key_type == 2) {
            // QUADRATIC: forward + backward tangents
            r.read_f32(); // forward
            r.read_f32(); // backward
        } else if (key_type == 3) {
            // TBC: tension, bias, continuity
            r.read_f32();
            r.read_f32();
            r.read_f32();
        }
    }
}

// Read Vec3 keys
void read_vec3_keys(BinaryReader& r, uint32_t num_keys, uint32_t key_type,
                    std::vector<NifKeyVec3>& keys) {
    keys.resize(bounded_count(r, num_keys, "keys"));
    for (uint32_t i = 0; i < num_keys; ++i) {
        keys[i].time = r.read_f32();
        keys[i].value.x = r.read_f32();
        keys[i].value.y = r.read_f32();
        keys[i].value.z = r.read_f32();
        if (key_type == 2) {
            // QUADRATIC: forward + backward tangent Vec3s (3 floats each).
            // Stored in NifKeyVec3.forward_tangent / backward_tangent.
            keys[i].forward_tangent.x  = r.read_f32();
            keys[i].forward_tangent.y  = r.read_f32();
            keys[i].forward_tangent.z  = r.read_f32();
            keys[i].backward_tangent.x = r.read_f32();
            keys[i].backward_tangent.y = r.read_f32();
            keys[i].backward_tangent.z = r.read_f32();
        } else if (key_type == 3) {
            // TBC: tension, bias, continuity
            r.read_f32();
            r.read_f32();
            r.read_f32();
        }
    }
}

// Read quaternion keys (LINEAR: time + wxyz, QUADRATIC: time + wxyz only -- no tangents for quats)
void read_quat_keys(BinaryReader& r, uint32_t num_keys, uint32_t key_type,
                    std::vector<NifKeyQuat>& keys) {
    keys.resize(bounded_count(r, num_keys, "keys"));
    for (uint32_t i = 0; i < num_keys; ++i) {
        keys[i].time = r.read_f32();
        if (key_type != 4) {
            // LINEAR (1) or QUADRATIC (2) -- both store w, x, y, z
            keys[i].value.w = r.read_f32();
            keys[i].value.x = r.read_f32();
            keys[i].value.y = r.read_f32();
            keys[i].value.z = r.read_f32();
            if (key_type == 3) {
                // TBC: tension, bias, continuity
                r.read_f32();
                r.read_f32();
                r.read_f32();
            }
        }
    }
}

// ============================================================================
// NiTransformData: rotation, translation, and scale keyframe arrays
// ============================================================================

NifTransformData parse_transform_data(BinaryReader& r) {
    NifTransformData td;

    // Rotation keys
    td.num_rotation_keys = r.read_u32();
    if (td.num_rotation_keys > 0) {
        td.rotation_key_type = r.read_u32();
        if (td.rotation_key_type == 4) {
            // EULER key type: 3 separate float key arrays (X, Y, Z axes).
            // Each sub-array: u32 count, u32 key_type, then count key records.
            // Verified from nif.xml NiRotData / QuatKey EULER handling.
            std::vector<NifKeyFloat>* euler_axes[3] = {
                &td.euler_x_keys, &td.euler_y_keys, &td.euler_z_keys
            };
            for (int axis = 0; axis < 3; ++axis) {
                uint32_t sub_count = r.read_u32();
                if (sub_count > 0) {
                    uint32_t sub_type = r.read_u32();
                    read_float_keys(r, sub_count, sub_type, *euler_axes[axis]);
                }
            }
        } else {
            read_quat_keys(r, td.num_rotation_keys, td.rotation_key_type, td.rotation_keys);
        }
    }

    // Translation keys
    td.num_translation_keys = r.read_u32();
    if (td.num_translation_keys > 0) {
        td.translation_key_type = r.read_u32();
        read_vec3_keys(r, td.num_translation_keys, td.translation_key_type, td.translation_keys);
    }

    // Scale keys
    td.num_scale_keys = r.read_u32();
    if (td.num_scale_keys > 0) {
        td.scale_key_type = r.read_u32();
        read_float_keys(r, td.num_scale_keys, td.scale_key_type, td.scale_keys);
    }

    return td;
}

// ============================================================================
// NiFloatInterpolator: default float value + data ref
// ============================================================================

NifFloatInterpolator parse_float_interpolator(BinaryReader& r) {
    NifFloatInterpolator interp;
    interp.default_value = r.read_f32();
    interp.data_ref = r.read_s32();
    return interp;
}

// ============================================================================
// NiFloatData: float keyframe array
// ============================================================================

NifFloatData parse_float_data(BinaryReader& r) {
    NifFloatData fd;
    fd.num_keys = r.read_u32();
    if (fd.num_keys > 0) {
        fd.key_type = r.read_u32();
        read_float_keys(r, fd.num_keys, fd.key_type, fd.keys);
    }
    return fd;
}

// ============================================================================
// Set of block types that are NiNode-like (have NiAVObject + children)
// ============================================================================

static const std::unordered_set<std::string> NODE_TYPES = {
    "NiNode", "NiBillboardNode", "NiSwitchNode", "RootCollisionNode",
    "BSFadeNode"
};

} // anonymous namespace

// ============================================================================
// Main NIF parser
// ============================================================================

NifFile nif_parse(std::span<const uint8_t> data) {
    NifFile nif;

    size_t pos = find_header_end(data);
    nif.header_line.assign(reinterpret_cast<const char*>(data.data()), pos);

    // Validate the magic before reading any binary. A NIF's first line is always
    // a "<...> File Format, Version X.X.X.X" banner (NetImmerse originally,
    // Gamebryo after the rename). Without this check, non-NIF input that merely
    // contains an early newline within the first 256 bytes — notably the LXFML
    // XML brick-model files stored under a .nif extension in res/BrickModels/,
    // whose "<?xml ...?>\r\n" satisfies find_header_end — would parse its XML as
    // a binary header and fail much later with a confusing, misleading error
    // (e.g. "block type name too long"). Reject it clearly and up front instead.
    if (nif.header_line.find("File Format") == std::string::npos) {
        throw NifError("NIF: not a NIF file (missing 'File Format' header banner)");
    }
    BinaryReader r(data);
    r.seek(pos);

    // Binary header
    nif.version = r.read_u32();
    nif.endian = r.read_u8(); // endian flag (1 = little-endian)

    nif.user_version = r.read_u32();
    nif.num_blocks = r.read_u32();

    // User version 2 (if user_version >= 10)
    if (nif.user_version >= 10) {
        nif.user_version_2 = r.read_u32();
    }

    // Export info strings (if user_version >= 3): 3 x u8-length-prefixed ASCII strings.
    // Authoring metadata: [0]=exporter name, [1]=exporter version, [2]=scene name/comment.
    // Verified from nif.xml ExportInfo compound.
    if (nif.user_version >= 3) {
        for (int i = 0; i < 3; ++i) {
            uint8_t len = r.read_u8();
            nif.export_info[i].resize(len);
            for (uint8_t j = 0; j < len; ++j) {
                nif.export_info[i][j] = static_cast<char>(r.read_u8());
            }
        }
    }

    // Block type names: u16 count, then u32-length-prefixed strings
    uint16_t num_block_types = r.read_u16();
    nif.block_types.resize(bounded_count(r, num_block_types, "block_types"));
    for (uint16_t i = 0; i < num_block_types; ++i) {
        uint32_t len = r.read_u32();
        if (len > 256) throw NifError("NIF: block type name too long");
        std::string name(len, '\0');
        for (uint32_t j = 0; j < len; ++j) {
            name[j] = static_cast<char>(r.read_u8());
        }
        nif.block_types[i] = std::move(name);
    }

    // Block type index per block: u16[num_blocks]
    nif.block_type_indices.resize(nif.num_blocks);
    for (uint32_t i = 0; i < nif.num_blocks; ++i) {
        nif.block_type_indices[i] = r.read_u16();
    }

    // Block sizes (version >= 20.2.0.7): u32[num_blocks]
    if (nif.version >= 0x14020007) {
        nif.block_sizes.resize(nif.num_blocks);
        for (uint32_t i = 0; i < nif.num_blocks; ++i) {
            nif.block_sizes[i] = r.read_u32();
        }
    }

    // String table: u32 count, u32 max_length, then u32-length-prefixed strings
    uint32_t num_strings = r.read_u32();
    nif.string_table_max_len = r.read_u32(); // max string length (informational, preserved)
    nif.string_table.resize(bounded_count(r, num_strings, "string_table"));
    for (uint32_t i = 0; i < num_strings; ++i) {
        uint32_t len = r.read_u32();
        if (len > 10000) {
            // Sanity bound
            nif.string_table[i] = {};
            continue;
        }
        std::string s(len, '\0');
        for (uint32_t j = 0; j < len; ++j) {
            s[j] = static_cast<char>(r.read_u8());
        }
        nif.string_table[i] = std::move(s);
    }

    // Groups: u32 count, then u32[count] group sizes
    uint32_t num_groups = r.read_u32();
    nif.groups.resize(bounded_count(r, num_groups, "groups"));
    for (uint32_t i = 0; i < num_groups; ++i) {
        nif.groups[i] = r.read_u32();
    }

    // ========================================================================
    // Parse blocks using block_sizes to guarantee correct advancement.
    // Even if a block parser reads the wrong number of bytes, we ALWAYS seek
    // to block_start + block_size before processing the next block.
    // ========================================================================
    if (!nif.block_sizes.empty()) {
        nif.block_data.reserve(nif.num_blocks);
    }
    for (uint32_t block_idx = 0; block_idx < nif.num_blocks; ++block_idx) {
        size_t block_start = r.pos();
        uint32_t block_size = block_idx < nif.block_sizes.size() ? nif.block_sizes[block_idx] : 0;
        size_t block_end = block_start + block_size;

        // Preserve the block's raw bytes (byte-perfect round-trip via nif_write, and
        // lossless carry-through for block types without a dedicated parser). Indexed 1:1
        // with block_type_indices, so push unconditionally when the size table exists.
        if (block_idx < nif.block_sizes.size()) {
            if (block_end > data.size()) throw NifError("NIF: block extends past end of file");
            nif.block_data.emplace_back(data.begin() + block_start, data.begin() + block_end);
        }

        uint16_t type_idx = nif.block_type_indices[block_idx];
        const std::string& type_name = (type_idx < nif.block_types.size())
            ? nif.block_types[type_idx] : "";

        try {
            // NiSortAdjustNode carries subtree sorting state after NiNode data.
            if (type_name == "NiSortAdjustNode") {
                NifNode node = parse_ni_sort_adjust_node(
                    r, nif.version, nif.string_table, block_end);
                node.block_index = block_idx;
                nif.nodes.push_back(std::move(node));
            }
            // NiNode and NiNode-like types (scene hierarchy)
            else if (NODE_TYPES.count(type_name)) {
                NifNode node = parse_ni_node(r, nif.version, nif.string_table);
                node.type_name = type_name; // Preserve the specific type name
                node.block_index = block_idx;
                nif.nodes.push_back(std::move(node));
            }
            // NiLODNode (scene hierarchy with LOD)
            else if (type_name == "NiLODNode") {
                NifNode node = parse_ni_lod_node(r, nif.version, nif.string_table, block_end);
                node.block_index = block_idx;
                nif.nodes.push_back(std::move(node));
            }
            // NiTriShape / NiTriStrips (geometry containers)
            else if (type_name == "NiTriShape" || type_name == "NiTriStrips") {
                NifNode node = parse_ni_tri_shape(r, nif.version, type_name, nif.string_table);
                node.block_index = block_idx;
                nif.nodes.push_back(std::move(node));
            }
            // NiTriShapeData (indexed triangle geometry)
            else if (type_name == "NiTriShapeData") {
                NifMesh mesh = parse_tri_shape_data(r, nif.version);
                mesh.block_index = block_idx;
                nif.meshes.push_back(std::move(mesh));
            }
            // NiTriStripsData (triangle strip geometry)
            else if (type_name == "NiTriStripsData") {
                NifMesh mesh = parse_tri_strips_data(r, nif.version);
                mesh.block_index = block_idx;
                nif.meshes.push_back(std::move(mesh));
            }
            // NiMaterialProperty (material colors)
            else if (type_name == "NiMaterialProperty") {
                NifMaterial mat = parse_material_property(r, nif.version, nif.string_table);
                mat.block_index = block_idx;
                nif.materials.push_back(std::move(mat));
            }
            // NiSourceTexture (texture file/pixel-data reference)
            else if (type_name == "NiSourceTexture") {
                NifTextureRef tex = parse_source_texture(r, nif.version, nif.string_table);
                tex.block_index = block_idx;
                nif.textures.push_back(std::move(tex));
            }
            // NiTexturingProperty (texture-slot table attached to NiTriShape/NiTriStrips)
            else if (type_name == "NiTexturingProperty") {
                NifTexturingProperty prop =
                    parse_texturing_property(r, nif.version, nif.string_table);
                prop.block_index = block_idx;
                nif.texturing_properties.push_back(std::move(prop));
            }
            // NiAlphaProperty (alpha blend/test state)
            else if (type_name == "NiAlphaProperty") {
                NifAlphaProperty prop =
                    parse_alpha_property(r, nif.version, nif.string_table, block_end);
                prop.block_index = block_idx;
                nif.alpha_properties.push_back(std::move(prop));
            }
            else if (type_name == "NiVertexColorProperty") {
                NifVertexColorProperty prop = parse_packed_flags_property<NifVertexColorProperty>(
                    r, nif.version, nif.string_table, block_end);
                prop.block_index = block_idx;
                nif.vertex_color_properties.push_back(std::move(prop));
            }
            else if (type_name == "NiZBufferProperty") {
                NifZBufferProperty prop = parse_packed_flags_property<NifZBufferProperty>(
                    r, nif.version, nif.string_table, block_end);
                prop.block_index = block_idx;
                nif.z_buffer_properties.push_back(std::move(prop));
            }
            else if (type_name == "NiSpecularProperty") {
                NifSpecularProperty prop = parse_packed_flags_property<NifSpecularProperty>(
                    r, nif.version, nif.string_table, block_end);
                prop.block_index = block_idx;
                nif.specular_properties.push_back(std::move(prop));
            }
            else if (type_name == "NiShadeProperty") {
                NifShadeProperty prop = parse_packed_flags_property<NifShadeProperty>(
                    r, nif.version, nif.string_table, block_end);
                prop.block_index = block_idx;
                nif.shade_properties.push_back(std::move(prop));
            }
            else if (type_name == "NiStencilProperty") {
                NifStencilProperty prop = parse_stencil_property(
                    r, nif.version, nif.string_table, block_end);
                prop.block_index = block_idx;
                nif.stencil_properties.push_back(std::move(prop));
            }
            // NiRangeLODData (LOD center and near/far distance table)
            else if (type_name == "NiRangeLODData") {
                NifRangeLODData data = parse_range_lod_data(r);
                data.block_index = block_idx;
                nif.range_lod_data.push_back(std::move(data));
            }
            // NiSkinInstance (mesh -> skeleton/bone palette binding)
            else if (type_name == "NiSkinInstance") {
                NifSkinInstance skin = parse_skin_instance(r);
                skin.block_index = block_idx;
                nif.skin_instances.push_back(std::move(skin));
            }
            // NiSkinData (bind transforms and per-bone vertex weights)
            else if (type_name == "NiSkinData") {
                NifSkinData skin = parse_skin_data(r);
                skin.block_index = block_idx;
                nif.skin_data.push_back(std::move(skin));
            }
            // ---- Animation blocks (found in .kf files) ----
            // NiControllerSequence (animation clip definition)
            else if (type_name == "NiControllerSequence") {
                NifControllerSequence seq =
                    parse_controller_sequence(r, nif.version, nif.string_table, block_end);
                seq.block_index = block_idx;
                nif.sequences.push_back(std::move(seq));
            }
            // NiTextKeyExtraData (timestamped event markers)
            else if (type_name == "NiTextKeyExtraData") {
                NifTextKeyExtraData tkd = parse_text_key_extra_data(r, nif.version, nif.string_table);
                tkd.block_index = block_idx;
                nif.text_key_data.push_back(std::move(tkd));
            }
            // NiTransformInterpolator (keyframe transform reference)
            else if (type_name == "NiTransformInterpolator") {
                NifTransformInterpolator interp = parse_transform_interpolator(r);
                interp.block_index = block_idx;
                nif.transform_interpolators.push_back(std::move(interp));
            }
            // NiTransformData (transform keyframe data)
            else if (type_name == "NiTransformData") {
                NifTransformData td = parse_transform_data(r);
                td.block_index = block_idx;
                nif.transform_data.push_back(std::move(td));
            }
            // NiFloatInterpolator (float keyframe reference)
            else if (type_name == "NiFloatInterpolator") {
                NifFloatInterpolator interp = parse_float_interpolator(r);
                interp.block_index = block_idx;
                nif.float_interpolators.push_back(std::move(interp));
            }
            // NiFloatData (float keyframe data)
            else if (type_name == "NiFloatData") {
                NifFloatData fd = parse_float_data(r);
                fd.block_index = block_idx;
                nif.float_data.push_back(std::move(fd));
            }
        } catch (...) {
            // Parse error in this block -- skip to next via block_end below
        }

        // ALWAYS advance to block_end, regardless of how much the parser read.
        // This prevents cascading failures from one bad block.
        if (block_size > 0) {
            r.seek(block_end);
        }
    }

    // Footer (nif.xml niobject Footer): Num Roots (uint) + Roots (Ref<NiObject>[Num Roots]).
    // Only reachable reliably when the block-size table positioned us past the last block —
    // without it (pre-20.2.0.7, never shipped by LU) the stream position after the loop is
    // wherever the last typed parser stopped, so don't misread arbitrary bytes as roots.
    if (!nif.block_sizes.empty()) {
        uint32_t num_roots = r.read_u32();
        if (num_roots > nif.num_blocks) throw NifError("NIF: unreasonable footer root count");
        nif.roots.resize(bounded_count(r, num_roots, "roots"));
        for (uint32_t i = 0; i < num_roots; ++i) {
            nif.roots[i] = r.read_s32();
        }
    }

    return nif;
}

} // namespace lu::assets
