#include "gamebryo/nif/nif_reader.h"
#include "common/binary_reader/binary_reader.h"

#include <cmath>
#include <cstring>
#include <algorithm>
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
        result.extra_data_refs.resize(num_extra);
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

    // Rotation matrix: 3x3 = 9 floats (row-major)
    // Convert to quaternion for storage.
    float m[3][3];
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            m[row][col] = r.read_f32();
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
        node.properties.resize(num_props);
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
    node.children.resize(num_children);
    for (uint32_t i = 0; i < num_children; ++i) {
        node.children[i] = r.read_s32();
    }

    // Effects: count + array of i32 block refs.
    // Block indices of attached NiDynamicEffect objects (lights, environment maps, etc.)
    uint32_t num_effects = r.read_u32();
    node.effect_refs.resize(num_effects);
    for (uint32_t i = 0; i < num_effects; ++i) {
        node.effect_refs[i] = r.read_s32();
    }

    return node;
}

// ============================================================================
// NiLODNode: same as NiNode but with LOD center and range data
// ============================================================================

NifNode parse_ni_lod_node(BinaryReader& r, uint32_t version,
                          const std::vector<std::string>& string_table) {
    NifNode node = parse_ni_node(r, version, string_table);
    node.type_name = "NiLODNode";

    // LOD data: block ref to NiRangeLODData (version >= 10.1.0.0) or inline
    if (version >= 0x0A010000) {
        node.lod_data_ref = r.read_s32();
    } else {
        // Inline: LOD center + levels
        node.lod_center.x = r.read_f32();
        node.lod_center.y = r.read_f32();
        node.lod_center.z = r.read_f32();
        uint32_t num_levels = r.read_u32();
        node.lod_ranges.resize(num_levels);
        for (uint32_t i = 0; i < num_levels; ++i) {
            node.lod_ranges[i].first = r.read_f32();  // near
            node.lod_ranges[i].second = r.read_f32(); // far
        }
    }

    return node;
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
        node.material_name_indices.resize(num_materials);
        for (uint32_t i = 0; i < num_materials; ++i) {
            node.material_name_indices[i] = r.read_s32();
        }
        node.material_extra_data_refs.resize(num_materials);
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
    mesh.vertices.resize(num_verts);
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
        mesh.triangles.resize(num_triangles);
        for (uint16_t i = 0; i < num_triangles; ++i) {
            mesh.triangles[i].a = r.read_u16();
            mesh.triangles[i].b = r.read_u16();
            mesh.triangles[i].c = r.read_u16();
        }
    }

    // Match groups (version >= 10.0.1.0)
    if (version >= 0x0A000100) {
        uint16_t num_match_groups = r.read_u16();
        mesh.match_groups.resize(num_match_groups);
        for (uint16_t mg = 0; mg < num_match_groups; ++mg) {
            uint16_t count = r.read_u16();
            mesh.match_groups[mg].indices.resize(count);
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
    result.keys.resize(num_keys);
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
// Field order for version 20.3.0.9 (verified against nif.xml ControllerLink):
//   NiObjectNET (name, extra data chain, controller ref)
//   u32 num_controlled_blocks
//   ControllerLink[num_controlled_blocks]  — each for v >= 20.1.0.3:
//     i32 interpolator_ref
//     i32 controller_ref
//     u8  priority
//     i32 node_name       (string table index)
//     i32 property_type   (string table index)
//     i32 controller_type (string table index)
//     i32 controller_id   (string table index)
//     i32 interpolator_id (string table index)
//   f32 weight
//   i32 text_keys_ref
//   u32 cycle_type
//   f32 frequency
//   f32 start_time
//   f32 stop_time
//   i32 manager_ref
//   i32 accum_root_name  (string table index for v >= 20.1.0.3)
// ============================================================================

NifControllerSequence parse_controller_sequence(BinaryReader& r, uint32_t version,
                                                 const std::vector<std::string>& string_table) {
    NifControllerSequence seq;

    // NiObjectNET: name, extra data, controller
    auto net = read_ni_object_net(r, version, string_table);
    seq.name = std::move(net.name);

    // Controlled blocks
    seq.num_controlled_blocks = r.read_u32();
    seq.controlled_blocks.reserve(seq.num_controlled_blocks);

    for (uint32_t i = 0; i < seq.num_controlled_blocks; ++i) {
        NifControlledBlock cb;

        if (version >= 0x0A010006) {
            cb.interpolator_ref = r.read_s32();
        }
        cb.controller_ref = r.read_s32();

        if (version >= 0x0A010006) {
            cb.priority = r.read_u8();
        }

        if (version >= 0x14010003) {
            // v >= 20.1.0.3: names via string table indices
            cb.node_name       = resolve_string(string_table, r.read_s32());
            cb.property_type   = resolve_string(string_table, r.read_s32());
            cb.controller_type = resolve_string(string_table, r.read_s32());
            cb.controller_id   = resolve_string(string_table, r.read_s32());
            cb.interpolator_id = resolve_string(string_table, r.read_s32());
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
    keys.resize(num_keys);
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
    keys.resize(num_keys);
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
    keys.resize(num_keys);
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
    "NiSortAdjustNode", "BSFadeNode"
};

} // anonymous namespace

// ============================================================================
// Main NIF parser
// ============================================================================

NifFile nif_parse(std::span<const uint8_t> data) {
    NifFile nif;

    size_t pos = find_header_end(data);
    BinaryReader r(data);
    r.seek(pos);

    // Binary header
    nif.version = r.read_u32();
    r.read_u8(); // endian flag (1 = little-endian)

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
    nif.block_types.resize(num_block_types);
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
    r.read_u32(); // max string length (informational)
    nif.string_table.resize(num_strings);
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
    for (uint32_t i = 0; i < num_groups; ++i) {
        r.read_u32(); // group size
    }

    // ========================================================================
    // Parse blocks using block_sizes to guarantee correct advancement.
    // Even if a block parser reads the wrong number of bytes, we ALWAYS seek
    // to block_start + block_size before processing the next block.
    // ========================================================================
    for (uint32_t block_idx = 0; block_idx < nif.num_blocks; ++block_idx) {
        size_t block_start = r.pos();
        uint32_t block_size = block_idx < nif.block_sizes.size() ? nif.block_sizes[block_idx] : 0;
        size_t block_end = block_start + block_size;

        uint16_t type_idx = nif.block_type_indices[block_idx];
        const std::string& type_name = (type_idx < nif.block_types.size())
            ? nif.block_types[type_idx] : "";

        try {
            // NiNode and NiNode-like types (scene hierarchy)
            if (NODE_TYPES.count(type_name)) {
                NifNode node = parse_ni_node(r, nif.version, nif.string_table);
                node.type_name = type_name; // Preserve the specific type name
                node.block_index = block_idx;
                nif.nodes.push_back(std::move(node));
            }
            // NiLODNode (scene hierarchy with LOD)
            else if (type_name == "NiLODNode") {
                NifNode node = parse_ni_lod_node(r, nif.version, nif.string_table);
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
                nif.materials.push_back(std::move(mat));
            }
            // ---- Animation blocks (found in .kf files) ----
            // NiControllerSequence (animation clip definition)
            else if (type_name == "NiControllerSequence") {
                NifControllerSequence seq = parse_controller_sequence(r, nif.version, nif.string_table);
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

    return nif;
}

} // namespace lu::assets
