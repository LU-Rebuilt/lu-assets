#pragma once

#include "netdevil/zone/luz/luz_types.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// References:
//   - nif.xml (github.com/niftools/nifxml) — NIF block type and field definitions
//   - NifSkope (github.com/niftools/nifskope) — format validation and visual inspection

// ============================================================================
// NIF (Gamebryo/NetImmerse) model parser for LEGO Universe.
// ============================================================================
//
// Target version: 20.3.0.9 (0x14030009), user_version 12, user_version_2 34.
//
// Format verified against Ghidra RE of legouniverse.exe.
// Field sizes cross-checked by tracing block bytes against known-good NIFs.
//
// ---- FILE HEADER ----
// Text header: ";Gamebryo File Format, Version X.X.X.X\n" (ASCII, newline-terminated)
// u32  version           Binary version number (e.g. 0x14030009 = 20.3.0.9)
// u8   endian_flag       1 = little-endian (always for LU)
// u32  user_version      Gamebryo user version (12 for LU)
// u32  num_blocks        Total number of blocks in the file
// u32  user_version_2    (only if user_version >= 10)
// u8+data  export_info[3]  Three u8-length-prefixed export info strings (if user_version >= 3)
// u16  num_block_types   Number of unique block type names
// (u32+data)[num_block_types]  Block type name strings (u32-length-prefixed)
// u16[num_blocks]       Block type index per block
// u32[num_blocks]       Block sizes in bytes (version >= 20.2.0.7)
// u32  num_strings       String table entry count
// u32  max_string_length Maximum string length in the table
// (u32+data)[num_strings]  String table entries (u32-length-prefixed)
// u32  num_groups        Group count
// u32[num_groups]        Group sizes
//
// ---- NiObjectNET (base for most blocks) ----
// i32  name_idx          String table index (-1 = none)
// u32  num_extra_data    Number of extra data block refs
// i32[num_extra_data]   Extra data block indices
// i32  controller_ref    Controller block index (-1 = none)
//
// ---- NiAVObject (inherits NiObjectNET) ----
// u16  flags             *** ALWAYS u16, NOT u32 ***
// f32  translation[3]    XYZ position
// f32  rotation[9]       3x3 rotation matrix, row-major
// f32  scale             Uniform scale
// u32  num_properties    Number of property block refs
// i32[num_properties]   Property block indices
// i32  collision_ref     Collision object block index (-1 = none)
//
// ---- NiNode (inherits NiAVObject) ----
// u32  num_children      Number of child block refs
// i32[num_children]     Child block indices
// u32  num_effects       Number of effect block refs
// i32[num_effects]      Effect block indices
//
// ---- NiTriShape / NiTriStrips (inherits NiAVObject) ----
// i32  data_ref          Block index of geometry data
// i32  skin_instance_ref Skin instance block index (-1 = none)
// u32  num_materials     Number of materials (v >= 10.0.1.0)
// i32[num_materials]    Material name string indices
// i32[num_materials]    Material extra data indices
// i32  active_material   Active material index
// i32  shader_prop_ref   (v >= 20.2.0.7)
// i32  alpha_prop_ref    (v >= 20.2.0.7)
//
// ---- NiTriShapeData ----
// u32  group_id          (v >= 20.3.0.9)
// u16  num_vertices
// u16  keep_flags        (v >= 10.1.0.0)
// bool has_vertices
// Vec3[num_vertices]    Vertex positions (if has_vertices)
// u16  vector_flags      (v >= 10.0.1.0, low 6 bits = UV set count, bit 12 = tangent space)
// bool has_normals
// Vec3[num_vertices]    Normals (if has_normals)
// Vec3[num_vertices]    Tangents (if has_normals && tangent_space)
// Vec3[num_vertices]    Bitangents (if has_normals && tangent_space)
// Vec3 bound_center      Bounding sphere center
// f32  bound_radius      Bounding sphere radius
// bool has_vertex_colors
// RGBA[num_vertices]    Vertex colors as 4x f32 (if has_vertex_colors)
// UV[num_uv_sets][num_vertices]  UV coordinates as 2x f32 per set
// u16  consistency_flags (v >= 10.0.1.0)
// i32  additional_data   Additional data ref (v >= 20.0.0.4)
// u16  num_triangles     Triangle count
// u32  num_triangle_points  Total index count (informational)
// bool has_triangles
// Triangle[num_triangles]  Index triples as 3x u16 (if has_triangles)
// u16  num_match_groups  Match group count (v >= 10.0.1.0)
// (u16 + u16[count])[num_match_groups]  Match group vertex index lists
//
// ---- NiMaterialProperty (inherits NiObjectNET) ----
// u16  flags             *** ALWAYS u16, NOT u32 ***
// Vec3 ambient           Ambient color (3x f32)
// Vec3 diffuse           Diffuse color (3x f32)
// Vec3 specular          Specular color (3x f32)
// Vec3 emissive          Emissive color (3x f32)
// f32  glossiness        Specular exponent
// f32  alpha             Opacity (1 = opaque)
//
// Block type frequency across 10,051 client meshes:
//   165,173 NiNode          -- Scene hierarchy node
//    63,104 NiTriShape      -- Triangle mesh container (refs NiTriShapeData)
//    63,104 NiTriShapeData  -- Indexed triangle geometry
//    29,072 NiMaterialProperty -- Material colors (ambient, diffuse, specular, emissive)
//    25,676 NiVertexColorProperty -- Vertex color source/mode flags
//    22,173 NiTriStrips     -- Triangle strip container
//    22,173 NiTriStripsData -- Strip geometry
//    15,879 NiTransformController -- Transform animation controller
//    15,877 NiTransformInterpolator -- Keyframe interpolation
//    15,775 NiSpecularProperty -- Specular enable flag
//    15,547 NiCamera        -- Camera definition
//    12,430 NiTransformData -- Transform keyframe data
//    12,286 NiLODNode       -- Level-of-detail hierarchy
//    12,277 NiRangeLODData  -- LOD distance ranges
//    11,809 NiStringExtraData -- String metadata
//    11,265 NiAlphaProperty -- Alpha blending/testing
//     8,653 NiShadeProperty -- Shade model flags
//     8,641 NiZBufferProperty -- Depth buffer flags
//     8,496 NiAmbientLight  -- Ambient light color
//     3,522 NiTexturingProperty -- Texture mapping
//     3,390 NiSourceTexture -- Texture file reference
//     1,807 NiSkinInstance/Data/Partition -- Skeletal deformation

struct NifError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct NifVertex {
    Vec3 position;
    Vec3 normal;
    Vec3 tangent;
    Vec3 bitangent;
    float u = 0, v = 0;       // First UV set
    float r = 1, g = 1, b = 1, a = 1; // Vertex color (if present)
};

// A single UV coordinate (used for extra UV sets beyond the first).
struct NifUV {
    float u = 0, v = 0;
};

struct NifTriangle {
    uint16_t a, b, c;
};

// Material properties extracted from NiMaterialProperty blocks.
// Verified field order from Ghidra RE and NifSkope nifxml.
struct NifMaterial {
    std::string name;
    uint16_t flags = 0;                    // Material flags (u16 always)
    Vec3 ambient  = {0.7f, 0.7f, 0.7f};   // Ambient color
    Vec3 diffuse  = {0.7f, 0.7f, 0.7f};   // Diffuse color
    Vec3 specular = {1.0f, 1.0f, 1.0f};   // Specular color
    Vec3 emissive = {0.0f, 0.0f, 0.0f};   // Emissive color
    float glossiness = 10.0f;              // Specular exponent
    float alpha = 1.0f;                    // Transparency (1 = opaque)
};

// Texture reference extracted from NiSourceTexture blocks.
struct NifTextureRef {
    std::string filename;     // Texture file path (relative)
    uint32_t block_index = 0;
};

// Node in the scene hierarchy (NiNode, NiLODNode, NiTriShape, NiTriStrips).
struct NifNode {
    std::string name;
    uint32_t block_index = 0;
    std::string type_name;      // "NiNode", "NiLODNode", "NiTriShape", etc.
    uint16_t flags = 0;         // NiAVObject flags (u16 always)
    Vec3 translation;
    Quat rotation;
    float scale = 1.0f;
    // NiObjectNET extra data refs: block indices of attached NiExtraData blocks.
    // Used for scene-graph queries (e.g. find collision data or string tags on a node).
    std::vector<int32_t> extra_data_refs;
    std::vector<int32_t> children;      // Block indices of child nodes (-1 = none)
    std::vector<int32_t> properties;    // Block indices of properties (-1 = none)
    std::vector<int32_t> effect_refs;   // Block indices of attached NiDynamicEffect blocks
    int32_t collision_ref = -1;         // Block index of collision object
    int32_t data_ref = -1;              // Block index of geometry data (NiTriShapeData/NiTriStripsData)
    int32_t skin_instance_ref = -1;     // Block index of skin instance
    int32_t controller_ref = -1;        // Block index of controller
    // NiTriShape/NiTriStrips material binding (v >= 10.0.1.0).
    // material_name_indices[i] → string table index of material name i.
    // material_extra_data_refs[i] → block index of per-material extra data.
    std::vector<int32_t> material_name_indices;
    std::vector<int32_t> material_extra_data_refs;
    int32_t active_material = -1;       // Index into material_name_indices

    // NiLODNode specific
    int32_t lod_data_ref = -1;          // Block index of NiRangeLODData (v >= 10.1.0.0)
    Vec3 lod_center;
    std::vector<std::pair<float, float>> lod_ranges; // (near, far) pairs
};

// Match group: list of vertex indices that share the same position (used for welding).
struct NifMatchGroup {
    std::vector<uint16_t> indices;
};

struct NifMesh {
    std::string block_type;     // "NiTriShapeData" or "NiTriStripsData"
    uint32_t block_index = 0;
    int lod_level = -1;         // -1 = unknown, 0 = highest detail

    // Geometry header fields
    uint32_t group_id = 0;      // Group ID (v >= 20.3.0.9)
    uint16_t keep_flags = 0;    // Keep/compress flags
    uint16_t vector_flags = 0;  // Vector flags (UV set count + tangent space bit)
    bool has_tangents = false;  // True if tangent space data present

    // Bounding sphere
    Vec3 bound_center;
    float bound_radius = 0;

    // Vertex data
    bool has_vertex_colors = false;
    std::vector<NifVertex> vertices;
    // Extra UV sets beyond the first (index 0 = second UV channel, etc.).
    // Each inner vector has one NifUV per vertex.
    std::vector<std::vector<NifUV>> extra_uv_sets;
    std::vector<NifTriangle> triangles;

    // Consistency and additional data
    uint16_t consistency_flags = 0;  // CT_MUTABLE=0, CT_STATIC=0x4000, CT_VOLATILE=0x8000
    int32_t additional_data_ref = -1;

    // Match groups (shared vertex indices)
    std::vector<NifMatchGroup> match_groups;
};

// ============================================================================
// Animation block types (found in .kf files and animation NIFs)
//
// Block type frequency across 22,557 client .kf animation files:
//    22,557 NiControllerSequence   -- Animation clip definition
//    22,553 NiTextKeyExtraData     -- Event markers (timestamps + text)
//    15,877 NiTransformInterpolator -- Keyframe transform interpolation reference
//    12,430 NiTransformData        -- Actual transform keyframe data
//     4,102 NiFloatInterpolator    -- Float keyframe interpolation reference
//     3,891 NiFloatData            -- Actual float keyframe data
// ============================================================================

struct NifTextKey {
    float time;
    std::string text;
};

struct NifKeyFloat {
    float time;
    float value;
};

struct NifKeyVec3 {
    float time;
    Vec3 value;
    // QUADRATIC interpolation tangents (only present when key_type == 2).
    // forward_tangent and backward_tangent are the derivative vectors at this keyframe.
    Vec3 forward_tangent;
    Vec3 backward_tangent;
};

struct NifKeyQuat {
    float time;
    Quat value;
};


// NiTextKeyExtraData: timestamped event markers.
// These mark animation events like "start", "end", footstep triggers, etc.
struct NifTextKeyExtraData {
    std::string name;
    std::vector<NifTextKey> keys;
    uint32_t block_index = 0;
};

// NiTransformInterpolator: references NiTransformData for actual keyframes.
// Default values used when no keys are present at a given time.
struct NifTransformInterpolator {
    Vec3 translation;
    Quat rotation;
    float scale = 1.0f;
    int32_t data_ref = -1;
    uint32_t block_index = 0;
};

// One link between a bone/node name and its interpolator + controller blocks.
// Stored in NiControllerSequence.controlled_blocks for version >= 10.0.1.0.
// For version 20.3.0.9 (LU client), all string fields use string table indices.
// Verified against nif.xml (NifSkope) ControllerLink compound definition.
struct NifControlledBlock {
    int32_t  interpolator_ref  = -1;  // Block index of NiTransformInterpolator/etc.
    int32_t  controller_ref    = -1;  // Block index of controller (-1 = none)
    uint8_t  priority          = 0;   // Blending priority (NI_PRIORITY_DEFAULT = 50)
    std::string node_name;            // Target scene node name (string table)
    std::string property_type;        // Property class name ("" if node controller)
    std::string controller_type;      // Controller class (e.g. "NiTransformController")
    std::string controller_id;        // Variable 1 (sub-controller identifier)
    std::string interpolator_id;      // Variable 2 (interpolator identifier)
};

// NiControllerSequence: animation clip definition.
// Stores all bone/property animation links for one animation clip.
// Verified field order: NiObjectNET, num_controlled_blocks, controlled_blocks[],
// weight, text_keys_ref, cycle_type, frequency, start_time, stop_time,
// manager_ref, accum_root_name. (nif.xml NiControllerSequence, v20.3.0.9)
struct NifControllerSequence {
    std::string name;
    uint32_t num_controlled_blocks = 0;
    std::vector<NifControlledBlock> controlled_blocks;
    float weight = 1.0f;
    int32_t text_keys_ref = -1;       // Block index of NiTextKeyExtraData
    uint32_t cycle_type = 0;          // 0=loop, 2=clamp, 4=reverse
    float frequency = 1.0f;
    float start_time = 0.0f;
    float stop_time = 0.0f;
    int32_t manager_ref = -1;
    std::string accum_root_name;
    uint32_t block_index = 0;
};

// NiTransformData: actual transform keyframe arrays.
// Rotation, translation, and scale keys each have their own key type:
//   1 = LINEAR_KEY, 2 = QUADRATIC_KEY, 3 = TBC_KEY, 4 = EULERKEY (rotation only)
// EULER rotation stores three separate per-axis float key arrays instead of quats.
struct NifTransformData {
    uint32_t num_rotation_keys = 0;
    uint32_t rotation_key_type = 0;
    std::vector<NifKeyQuat>  rotation_keys;     // LINEAR/QUADRATIC/TBC rotation
    // EULER rotation (key_type == 4): three separate float-key arrays (X, Y, Z axes)
    std::vector<NifKeyFloat> euler_x_keys;
    std::vector<NifKeyFloat> euler_y_keys;
    std::vector<NifKeyFloat> euler_z_keys;
    uint32_t num_translation_keys = 0;
    uint32_t translation_key_type = 0;
    std::vector<NifKeyVec3> translation_keys;
    uint32_t num_scale_keys = 0;
    uint32_t scale_key_type = 0;
    std::vector<NifKeyFloat> scale_keys;
    uint32_t block_index = 0;
};

// NiFloatInterpolator: references NiFloatData for actual keyframes.
struct NifFloatInterpolator {
    float default_value = 0.0f;
    int32_t data_ref = -1;
    uint32_t block_index = 0;
};

// NiFloatData: float keyframe array.
struct NifFloatData {
    uint32_t num_keys = 0;
    uint32_t key_type = 0;
    std::vector<NifKeyFloat> keys;
    uint32_t block_index = 0;
};

struct NifFile {
    uint32_t version = 0;       // e.g., 0x14030009
    uint32_t user_version = 0;
    uint32_t user_version_2 = 0;
    uint32_t num_blocks = 0;
    // Export info strings (user_version >= 3): 3 strings from the authoring tool.
    // Typically: [exporter name, exporter version, author comment/scene name].
    // Verified from nif.xml ExportInfo compound.
    std::string export_info[3];
    std::vector<std::string> block_types;
    std::vector<uint16_t> block_type_indices;
    std::vector<uint32_t> block_sizes;

    // String table (populated from header, used to resolve string indices)
    std::vector<std::string> string_table;

    // Parsed data -- scene graph
    std::vector<NifNode> nodes;
    std::vector<NifMesh> meshes;
    std::vector<NifMaterial> materials;
    std::vector<NifTextureRef> textures;

    // Parsed data -- animation (found in .kf files)
    std::vector<NifControllerSequence> sequences;
    std::vector<NifTextKeyExtraData> text_key_data;
    std::vector<NifTransformInterpolator> transform_interpolators;
    std::vector<NifTransformData> transform_data;
    std::vector<NifFloatInterpolator> float_interpolators;
    std::vector<NifFloatData> float_data;
};
} // namespace lu::assets
