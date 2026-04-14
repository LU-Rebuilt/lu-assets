#include <gtest/gtest.h>
#include "gamebryo/nif/nif_reader.h"

#include <cstring>
#include <vector>

using namespace lu::assets;

namespace {

struct NifBuilder {
    std::vector<uint8_t> data;
    void u8(uint8_t v) { data.push_back(v); }
    void u16(uint16_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 2);
    }
    void s32(int32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void u32(uint32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void f32(float v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void str(const std::string& s) {
        data.insert(data.end(), s.begin(), s.end());
    }
    // Write a u32-length-prefixed string
    void str32(const std::string& s) {
        u32(static_cast<uint32_t>(s.size()));
        str(s);
    }
    // Identity 3x3 rotation matrix (9 floats)
    void identity_matrix() {
        f32(1); f32(0); f32(0);
        f32(0); f32(1); f32(0);
        f32(0); f32(0); f32(1);
    }
};

// Build the NIF header (everything before the blocks).
// string_table: strings to include in the string table.
// block_type_names: unique block type names.
// block_type_indices: per-block type index into block_type_names.
// block_sizes: per-block size in bytes.
void write_header(NifBuilder& b,
                  const std::vector<std::string>& string_table,
                  const std::vector<std::string>& block_type_names,
                  const std::vector<uint16_t>& block_type_indices,
                  const std::vector<uint32_t>& block_sizes) {
    // Text header line
    b.str("Gamebryo File Format, Version 20.3.0.9\n");

    // Binary header
    b.u32(0x14030009); // version
    b.u8(1);           // endian flag
    b.u32(12);         // user_version
    b.u32(static_cast<uint32_t>(block_type_indices.size())); // num_blocks

    // user_version_2 (user_version >= 10)
    b.u32(34);

    // Export info strings (user_version >= 3): 3 empty
    b.u8(0); b.u8(0); b.u8(0);

    // Block type names
    b.u16(static_cast<uint16_t>(block_type_names.size()));
    for (const auto& name : block_type_names) {
        b.str32(name);
    }

    // Block type indices
    for (uint16_t idx : block_type_indices) {
        b.u16(idx);
    }

    // Block sizes (version >= 20.2.0.7)
    for (uint32_t sz : block_sizes) {
        b.u32(sz);
    }

    // String table
    b.u32(static_cast<uint32_t>(string_table.size()));
    uint32_t max_len = 0;
    for (const auto& s : string_table) {
        if (s.size() > max_len) max_len = static_cast<uint32_t>(s.size());
    }
    b.u32(max_len);
    for (const auto& s : string_table) {
        b.str32(s);
    }

    // Groups: 0
    b.u32(0);
}

// Build a minimal valid NIF (version 20.3.0.9) with 0 blocks.
std::vector<uint8_t> build_minimal_nif() {
    NifBuilder b;
    write_header(b, {}, {}, {}, {});
    return b.data;
}

// Write NiObjectNET fields for version 20.3.0.9 (string table indices).
// name_idx: index into the string table (-1 = none).
// controller_ref: block index of controller (-1 = none).
void write_ni_object_net(NifBuilder& b, int32_t name_idx, int32_t controller_ref) {
    b.s32(name_idx);    // name string table index
    b.u32(0);           // num_extra_data = 0
    b.s32(controller_ref); // controller ref
}

// Write NiAVObject fields: flags(u16), transform, properties, collision.
void write_ni_av_object(NifBuilder& b, int32_t name_idx, uint16_t flags) {
    write_ni_object_net(b, name_idx, -1);
    b.u16(flags);           // flags: ALWAYS u16
    b.f32(1.0f); b.f32(2.0f); b.f32(3.0f); // translation
    b.identity_matrix();    // rotation (identity)
    b.f32(1.0f);            // scale
    b.u32(0);               // num_properties = 0
    b.s32(-1);              // collision_ref = -1
}

// Build a NIF with a single NiNode block.
// Returns the data and the expected block size.
std::pair<std::vector<uint8_t>, uint32_t> build_ninode_nif() {
    // The NiNode block data (we build it separately to measure its size)
    NifBuilder block;
    // NiAVObject fields
    write_ni_av_object(block, 0, 0x000E); // name_idx=0, flags=0x000E
    // NiNode fields
    block.u32(0); // num_children = 0
    block.u32(0); // num_effects = 0

    uint32_t block_size = static_cast<uint32_t>(block.data.size());

    NifBuilder b;
    write_header(b,
        {"TestNode"},            // string table
        {"NiNode"},              // block types
        {0},                     // block type indices
        {block_size});           // block sizes

    // Append block data
    b.data.insert(b.data.end(), block.data.begin(), block.data.end());

    return {b.data, block_size};
}

// Build a NIF with a NiNode that has children, to test the u16 flags fix.
// If flags were read as u32, the parser would consume 2 extra bytes from translation,
// causing all subsequent fields to be misaligned and the children count to be garbage.
std::pair<std::vector<uint8_t>, uint32_t> build_ninode_with_children_nif() {
    NifBuilder block;
    // NiAVObject: name_idx=0, flags=0x000E (u16!)
    write_ni_av_object(block, 0, 0x000E);
    // NiNode: 2 children
    block.u32(2);
    block.s32(1);  // child 0 -> block 1
    block.s32(2);  // child 1 -> block 2
    // Effects: 0
    block.u32(0);

    uint32_t block_size = static_cast<uint32_t>(block.data.size());

    NifBuilder b;
    write_header(b,
        {"ParentNode"},
        {"NiNode"},
        {0},
        {block_size});

    b.data.insert(b.data.end(), block.data.begin(), block.data.end());
    return {b.data, block_size};
}

// Build a NIF with an NiMaterialProperty block.
// NiProperty::Flags are NOT present in version 20.3.0.9 (only <= 10.0.0.0).
std::pair<std::vector<uint8_t>, uint32_t> build_material_nif() {
    NifBuilder block;
    // NiObjectNET: name_idx=0, controller=-1
    write_ni_object_net(block, 0, -1);
    // NO flags field for version 20.3.0.9
    // ambient
    block.f32(0.5f); block.f32(0.5f); block.f32(0.5f);
    // diffuse
    block.f32(0.8f); block.f32(0.8f); block.f32(0.8f);
    // specular
    block.f32(1.0f); block.f32(1.0f); block.f32(1.0f);
    // emissive
    block.f32(0.0f); block.f32(0.0f); block.f32(0.0f);
    // glossiness
    block.f32(25.0f);
    // alpha
    block.f32(0.9f);

    uint32_t block_size = static_cast<uint32_t>(block.data.size());

    NifBuilder b;
    write_header(b,
        {"TestMaterial"},
        {"NiMaterialProperty"},
        {0},
        {block_size});

    b.data.insert(b.data.end(), block.data.begin(), block.data.end());
    return {b.data, block_size};
}

} // anonymous namespace

TEST(NIF, ParseEmptyDataThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(nif_parse(empty), NifError);
}

TEST(NIF, ParseNoHeaderNewlineThrows) {
    std::vector<uint8_t> no_newline(256, 'A');
    EXPECT_THROW(nif_parse(no_newline), NifError);
}

TEST(NIF, ParseMinimalNifZeroBlocks) {
    auto data = build_minimal_nif();
    auto nif = nif_parse(data);

    EXPECT_EQ(nif.version, 0x14030009u);
    EXPECT_EQ(nif.user_version, 12u);
    EXPECT_EQ(nif.user_version_2, 34u);
    EXPECT_EQ(nif.num_blocks, 0u);
    EXPECT_TRUE(nif.meshes.empty());
    EXPECT_TRUE(nif.nodes.empty());
    EXPECT_TRUE(nif.materials.empty());
    EXPECT_TRUE(nif.block_types.empty());
    EXPECT_TRUE(nif.block_type_indices.empty());
    EXPECT_TRUE(nif.string_table.empty());
}

TEST(NIF, StringTablePopulated) {
    NifBuilder b;
    write_header(b, {"hello", "world", "test"}, {}, {}, {});
    auto nif = nif_parse(b.data);

    ASSERT_EQ(nif.string_table.size(), 3u);
    EXPECT_EQ(nif.string_table[0], "hello");
    EXPECT_EQ(nif.string_table[1], "world");
    EXPECT_EQ(nif.string_table[2], "test");
}

TEST(NIF, UserVersion2Stored) {
    auto data = build_minimal_nif();
    auto nif = nif_parse(data);
    EXPECT_EQ(nif.user_version_2, 34u);
}

TEST(NIF, ParseNiNodeBasic) {
    auto [data, block_size] = build_ninode_nif();
    auto nif = nif_parse(data);

    ASSERT_EQ(nif.nodes.size(), 1u);
    const auto& node = nif.nodes[0];
    EXPECT_EQ(node.name, "TestNode");
    EXPECT_EQ(node.type_name, "NiNode");
    EXPECT_EQ(node.flags, 0x000Eu);
    EXPECT_FLOAT_EQ(node.translation.x, 1.0f);
    EXPECT_FLOAT_EQ(node.translation.y, 2.0f);
    EXPECT_FLOAT_EQ(node.translation.z, 3.0f);
    EXPECT_FLOAT_EQ(node.scale, 1.0f);
    EXPECT_TRUE(node.children.empty());
    EXPECT_EQ(node.collision_ref, -1);
}

// This is the critical test for the u16 flags bug fix.
// If flags were read as u32, the 2 extra bytes consumed would shift
// the translation read, causing num_children to read garbage and hang.
TEST(NIF, NiNodeFlagsAreU16NotU32) {
    auto [data, block_size] = build_ninode_with_children_nif();
    auto nif = nif_parse(data);

    ASSERT_EQ(nif.nodes.size(), 1u);
    const auto& node = nif.nodes[0];
    EXPECT_EQ(node.name, "ParentNode");
    EXPECT_EQ(node.flags, 0x000Eu);
    // If flags were u32, translation.x would be wrong and children would be garbage
    EXPECT_FLOAT_EQ(node.translation.x, 1.0f);
    EXPECT_FLOAT_EQ(node.translation.y, 2.0f);
    EXPECT_FLOAT_EQ(node.translation.z, 3.0f);
    ASSERT_EQ(node.children.size(), 2u);
    EXPECT_EQ(node.children[0], 1);
    EXPECT_EQ(node.children[1], 2);
}

TEST(NIF, ParseNiMaterialProperty) {
    auto [data, block_size] = build_material_nif();
    auto nif = nif_parse(data);

    ASSERT_EQ(nif.materials.size(), 1u);
    const auto& mat = nif.materials[0];
    EXPECT_EQ(mat.name, "TestMaterial");
    EXPECT_EQ(mat.flags, 0u);  // No flags in version 20.3.0.9
    EXPECT_FLOAT_EQ(mat.ambient.x, 0.5f);
    EXPECT_FLOAT_EQ(mat.diffuse.x, 0.8f);
    EXPECT_FLOAT_EQ(mat.specular.x, 1.0f);
    EXPECT_FLOAT_EQ(mat.emissive.x, 0.0f);
    EXPECT_FLOAT_EQ(mat.glossiness, 25.0f);
    EXPECT_FLOAT_EQ(mat.alpha, 0.9f);
}

TEST(NIF, StructDefaultValues) {
    NifVertex v;
    EXPECT_FLOAT_EQ(v.u, 0.0f);
    EXPECT_FLOAT_EQ(v.v, 0.0f);
    EXPECT_FLOAT_EQ(v.r, 1.0f);
    EXPECT_FLOAT_EQ(v.g, 1.0f);
    EXPECT_FLOAT_EQ(v.b, 1.0f);
    EXPECT_FLOAT_EQ(v.a, 1.0f);

    NifMaterial m;
    EXPECT_FLOAT_EQ(m.glossiness, 10.0f);
    EXPECT_FLOAT_EQ(m.alpha, 1.0f);
    EXPECT_EQ(m.flags, 0u);

    NifNode n;
    EXPECT_FLOAT_EQ(n.scale, 1.0f);
    EXPECT_EQ(n.data_ref, -1);
    EXPECT_EQ(n.collision_ref, -1);
    EXPECT_EQ(n.skin_instance_ref, -1);
    EXPECT_EQ(n.controller_ref, -1);
    EXPECT_EQ(n.flags, 0u);

    NifMesh mesh;
    EXPECT_FLOAT_EQ(mesh.bound_radius, 0.0f);
    EXPECT_EQ(mesh.consistency_flags, 0u);
    EXPECT_EQ(mesh.additional_data_ref, -1);
    EXPECT_FALSE(mesh.has_tangents);
}

// ============================================================================
// Animation block tests
// ============================================================================

// Build a NIF with NiTextKeyExtraData block.
std::pair<std::vector<uint8_t>, uint32_t> build_text_key_extra_data_nif() {
    NifBuilder block;
    // NiExtraData base: name = string table index 0
    block.s32(0); // name string table index

    // 2 text keys
    block.u32(2);
    // Key 0: time=0.0, text=string table index 1
    block.f32(0.0f);
    block.s32(1);
    // Key 1: time=1.5, text=string table index 2
    block.f32(1.5f);
    block.s32(2);

    uint32_t block_size = static_cast<uint32_t>(block.data.size());

    NifBuilder b;
    write_header(b,
        {"TextKeys", "start", "end"},    // string table
        {"NiTextKeyExtraData"},           // block types
        {0},                              // block type indices
        {block_size});                    // block sizes

    b.data.insert(b.data.end(), block.data.begin(), block.data.end());
    return {b.data, block_size};
}

TEST(NIF, ParseNiTextKeyExtraData) {
    auto [data, block_size] = build_text_key_extra_data_nif();
    auto nif = nif_parse(data);

    ASSERT_EQ(nif.text_key_data.size(), 1u);
    const auto& tkd = nif.text_key_data[0];
    EXPECT_EQ(tkd.name, "TextKeys");
    EXPECT_EQ(tkd.block_index, 0u);
    ASSERT_EQ(tkd.keys.size(), 2u);
    EXPECT_FLOAT_EQ(tkd.keys[0].time, 0.0f);
    EXPECT_EQ(tkd.keys[0].text, "start");
    EXPECT_FLOAT_EQ(tkd.keys[1].time, 1.5f);
    EXPECT_EQ(tkd.keys[1].text, "end");
}

// Build a NIF with NiTransformInterpolator block.
std::pair<std::vector<uint8_t>, uint32_t> build_transform_interpolator_nif() {
    NifBuilder block;
    // Default translation
    block.f32(1.0f); block.f32(2.0f); block.f32(3.0f);
    // Default rotation (w, x, y, z)
    block.f32(1.0f); block.f32(0.0f); block.f32(0.0f); block.f32(0.0f);
    // Default scale
    block.f32(1.0f);
    // Data ref (block index of NiTransformData, -1 = none)
    block.s32(1);

    uint32_t block_size = static_cast<uint32_t>(block.data.size());

    NifBuilder b;
    write_header(b,
        {},                              // string table (none needed)
        {"NiTransformInterpolator"},     // block types
        {0},                             // block type indices
        {block_size});                   // block sizes

    b.data.insert(b.data.end(), block.data.begin(), block.data.end());
    return {b.data, block_size};
}

TEST(NIF, ParseNiTransformInterpolator) {
    auto [data, block_size] = build_transform_interpolator_nif();
    auto nif = nif_parse(data);

    ASSERT_EQ(nif.transform_interpolators.size(), 1u);
    const auto& interp = nif.transform_interpolators[0];
    EXPECT_FLOAT_EQ(interp.translation.x, 1.0f);
    EXPECT_FLOAT_EQ(interp.translation.y, 2.0f);
    EXPECT_FLOAT_EQ(interp.translation.z, 3.0f);
    EXPECT_FLOAT_EQ(interp.rotation.w, 1.0f);
    EXPECT_FLOAT_EQ(interp.rotation.x, 0.0f);
    EXPECT_FLOAT_EQ(interp.scale, 1.0f);
    EXPECT_EQ(interp.data_ref, 1);
    EXPECT_EQ(interp.block_index, 0u);
}

// Build a NIF with NiTransformData block (linear keys).
std::pair<std::vector<uint8_t>, uint32_t> build_transform_data_nif() {
    NifBuilder block;

    // Rotation keys: 2 LINEAR keys (type 1)
    block.u32(2); // num_rotation_keys
    block.u32(1); // key_type = LINEAR
    // Key 0: time=0.0, quat w=1 x=0 y=0 z=0
    block.f32(0.0f); block.f32(1.0f); block.f32(0.0f); block.f32(0.0f); block.f32(0.0f);
    // Key 1: time=1.0, quat w=0.707 x=0.707 y=0 z=0
    block.f32(1.0f); block.f32(0.707f); block.f32(0.707f); block.f32(0.0f); block.f32(0.0f);

    // Translation keys: 2 LINEAR keys (type 1)
    block.u32(2);
    block.u32(1); // LINEAR
    // Key 0: time=0.0, xyz=(0,0,0)
    block.f32(0.0f); block.f32(0.0f); block.f32(0.0f); block.f32(0.0f);
    // Key 1: time=1.0, xyz=(10,0,0)
    block.f32(1.0f); block.f32(10.0f); block.f32(0.0f); block.f32(0.0f);

    // Scale keys: 1 LINEAR key (type 1)
    block.u32(1);
    block.u32(1); // LINEAR
    // Key 0: time=0.0, value=1.0
    block.f32(0.0f); block.f32(1.0f);

    uint32_t block_size = static_cast<uint32_t>(block.data.size());

    NifBuilder b;
    write_header(b,
        {},                      // string table
        {"NiTransformData"},     // block types
        {0},                     // block type indices
        {block_size});           // block sizes

    b.data.insert(b.data.end(), block.data.begin(), block.data.end());
    return {b.data, block_size};
}

TEST(NIF, ParseNiTransformData) {
    auto [data, block_size] = build_transform_data_nif();
    auto nif = nif_parse(data);

    ASSERT_EQ(nif.transform_data.size(), 1u);
    const auto& td = nif.transform_data[0];
    EXPECT_EQ(td.num_rotation_keys, 2u);
    EXPECT_EQ(td.rotation_key_type, 1u);
    ASSERT_EQ(td.rotation_keys.size(), 2u);
    EXPECT_FLOAT_EQ(td.rotation_keys[0].time, 0.0f);
    EXPECT_FLOAT_EQ(td.rotation_keys[0].value.w, 1.0f);
    EXPECT_FLOAT_EQ(td.rotation_keys[1].time, 1.0f);
    EXPECT_FLOAT_EQ(td.rotation_keys[1].value.x, 0.707f);

    EXPECT_EQ(td.num_translation_keys, 2u);
    EXPECT_EQ(td.translation_key_type, 1u);
    ASSERT_EQ(td.translation_keys.size(), 2u);
    EXPECT_FLOAT_EQ(td.translation_keys[0].time, 0.0f);
    EXPECT_FLOAT_EQ(td.translation_keys[0].value.x, 0.0f);
    EXPECT_FLOAT_EQ(td.translation_keys[1].time, 1.0f);
    EXPECT_FLOAT_EQ(td.translation_keys[1].value.x, 10.0f);

    EXPECT_EQ(td.num_scale_keys, 1u);
    EXPECT_EQ(td.scale_key_type, 1u);
    ASSERT_EQ(td.scale_keys.size(), 1u);
    EXPECT_FLOAT_EQ(td.scale_keys[0].time, 0.0f);
    EXPECT_FLOAT_EQ(td.scale_keys[0].value, 1.0f);
}

// Build a NIF with NiFloatInterpolator + NiFloatData blocks.
std::pair<std::vector<uint8_t>, std::vector<uint32_t>> build_float_anim_nif() {
    // Block 0: NiFloatInterpolator
    NifBuilder block0;
    block0.f32(0.5f);  // default value
    block0.s32(1);      // data_ref -> block 1
    uint32_t sz0 = static_cast<uint32_t>(block0.data.size());

    // Block 1: NiFloatData (2 LINEAR keys)
    NifBuilder block1;
    block1.u32(2); // num_keys
    block1.u32(1); // key_type = LINEAR
    block1.f32(0.0f); block1.f32(0.0f);   // time=0, value=0
    block1.f32(1.0f); block1.f32(1.0f);   // time=1, value=1
    uint32_t sz1 = static_cast<uint32_t>(block1.data.size());

    NifBuilder b;
    write_header(b,
        {},
        {"NiFloatInterpolator", "NiFloatData"},
        {0, 1},
        {sz0, sz1});

    b.data.insert(b.data.end(), block0.data.begin(), block0.data.end());
    b.data.insert(b.data.end(), block1.data.begin(), block1.data.end());
    return {b.data, {sz0, sz1}};
}

TEST(NIF, ParseNiFloatInterpolatorAndData) {
    auto [data, sizes] = build_float_anim_nif();
    auto nif = nif_parse(data);

    ASSERT_EQ(nif.float_interpolators.size(), 1u);
    const auto& interp = nif.float_interpolators[0];
    EXPECT_FLOAT_EQ(interp.default_value, 0.5f);
    EXPECT_EQ(interp.data_ref, 1);
    EXPECT_EQ(interp.block_index, 0u);

    ASSERT_EQ(nif.float_data.size(), 1u);
    const auto& fd = nif.float_data[0];
    EXPECT_EQ(fd.num_keys, 2u);
    EXPECT_EQ(fd.key_type, 1u);
    ASSERT_EQ(fd.keys.size(), 2u);
    EXPECT_FLOAT_EQ(fd.keys[0].time, 0.0f);
    EXPECT_FLOAT_EQ(fd.keys[0].value, 0.0f);
    EXPECT_FLOAT_EQ(fd.keys[1].time, 1.0f);
    EXPECT_FLOAT_EQ(fd.keys[1].value, 1.0f);
    EXPECT_EQ(fd.block_index, 1u);
}

// Build a NIF mimicking a .kf file: NiControllerSequence + NiTextKeyExtraData +
// NiTransformInterpolator + NiTransformData. Verifies that all animation block
// types parse together in a multi-block file.
std::vector<uint8_t> build_kf_like_nif() {
    // Block 0: NiControllerSequence
    NifBuilder b0;
    // NiObjectNET: name_idx=0 ("idle"), no extra data, controller=-1
    b0.s32(0); b0.u32(0); b0.s32(-1);
    // num_controlled_blocks
    b0.u32(1);
    // Controlled block for v20.3.0.9 (>= 10.1.0.6 and >= 20.1.0.3):
    //   interpolator_ref, controller_ref, priority, then 5 string-table indices
    b0.s32(-1);  // interpolator_ref
    b0.s32(-1);  // controller_ref
    b0.u8(0);    // priority
    b0.s32(-1);  // node_name (string table idx, -1 = empty)
    b0.s32(-1);  // property_type
    b0.s32(-1);  // controller_type
    b0.s32(-1);  // controller_id
    b0.s32(-1);  // interpolator_id
    // Sequence-level fields
    b0.f32(1.0f);  // weight
    b0.s32(-1);    // text_keys_ref
    b0.u32(0);     // cycle_type
    b0.f32(1.0f);  // frequency
    b0.f32(0.0f);  // start_time
    b0.f32(2.0f);  // stop_time
    // manager ref
    b0.s32(-1);
    // accum root name (string table idx 1 = "Scene Root")
    b0.s32(1);
    uint32_t sz0 = static_cast<uint32_t>(b0.data.size());

    // Block 1: NiTextKeyExtraData
    NifBuilder b1;
    b1.s32(2); // name = string table index 2 ("TextKeys")
    b1.u32(1); // 1 key
    b1.f32(0.0f); b1.s32(3); // time=0, text="start"
    uint32_t sz1 = static_cast<uint32_t>(b1.data.size());

    // Block 2: NiTransformInterpolator
    NifBuilder b2;
    b2.f32(0); b2.f32(0); b2.f32(0);          // translation
    b2.f32(1); b2.f32(0); b2.f32(0); b2.f32(0); // rotation
    b2.f32(1);                                   // scale
    b2.s32(3);                                   // data_ref -> block 3
    uint32_t sz2 = static_cast<uint32_t>(b2.data.size());

    // Block 3: NiTransformData (0 keys in all channels)
    NifBuilder b3;
    b3.u32(0); // 0 rotation keys
    b3.u32(0); // 0 translation keys
    b3.u32(0); // 0 scale keys
    uint32_t sz3 = static_cast<uint32_t>(b3.data.size());

    NifBuilder b;
    write_header(b,
        {"idle", "Scene Root", "TextKeys", "start"},
        {"NiControllerSequence", "NiTextKeyExtraData",
         "NiTransformInterpolator", "NiTransformData"},
        {0, 1, 2, 3},
        {sz0, sz1, sz2, sz3});

    b.data.insert(b.data.end(), b0.data.begin(), b0.data.end());
    b.data.insert(b.data.end(), b1.data.begin(), b1.data.end());
    b.data.insert(b.data.end(), b2.data.begin(), b2.data.end());
    b.data.insert(b.data.end(), b3.data.begin(), b3.data.end());
    return b.data;
}

TEST(NIF, ParseKFLikeFile) {
    auto data = build_kf_like_nif();
    auto nif = nif_parse(data);

    // NiControllerSequence
    ASSERT_EQ(nif.sequences.size(), 1u);
    EXPECT_EQ(nif.sequences[0].name, "idle");
    EXPECT_EQ(nif.sequences[0].num_controlled_blocks, 1u);
    EXPECT_FLOAT_EQ(nif.sequences[0].weight, 1.0f);
    EXPECT_EQ(nif.sequences[0].cycle_type, 0u);
    EXPECT_FLOAT_EQ(nif.sequences[0].frequency, 1.0f);
    EXPECT_FLOAT_EQ(nif.sequences[0].start_time, 0.0f);
    EXPECT_FLOAT_EQ(nif.sequences[0].stop_time, 2.0f);
    EXPECT_EQ(nif.sequences[0].manager_ref, -1);
    EXPECT_EQ(nif.sequences[0].accum_root_name, "Scene Root");
    EXPECT_EQ(nif.sequences[0].block_index, 0u);

    // NiTextKeyExtraData
    ASSERT_EQ(nif.text_key_data.size(), 1u);
    EXPECT_EQ(nif.text_key_data[0].name, "TextKeys");
    ASSERT_EQ(nif.text_key_data[0].keys.size(), 1u);
    EXPECT_EQ(nif.text_key_data[0].keys[0].text, "start");

    // NiTransformInterpolator
    ASSERT_EQ(nif.transform_interpolators.size(), 1u);
    EXPECT_EQ(nif.transform_interpolators[0].data_ref, 3);

    // NiTransformData (empty)
    ASSERT_EQ(nif.transform_data.size(), 1u);
    EXPECT_EQ(nif.transform_data[0].num_rotation_keys, 0u);
    EXPECT_EQ(nif.transform_data[0].num_translation_keys, 0u);
    EXPECT_EQ(nif.transform_data[0].num_scale_keys, 0u);
}

// Test that animation struct defaults are correct.
TEST(NIF, AnimStructDefaultValues) {
    NifControllerSequence seq;
    EXPECT_FLOAT_EQ(seq.weight, 1.0f);
    EXPECT_EQ(seq.cycle_type, 0u);
    EXPECT_FLOAT_EQ(seq.frequency, 1.0f);
    EXPECT_FLOAT_EQ(seq.start_time, 0.0f);
    EXPECT_FLOAT_EQ(seq.stop_time, 0.0f);
    EXPECT_EQ(seq.text_keys_ref, -1);
    EXPECT_EQ(seq.manager_ref, -1);

    NifTransformInterpolator interp;
    EXPECT_FLOAT_EQ(interp.scale, 1.0f);
    EXPECT_EQ(interp.data_ref, -1);

    NifFloatInterpolator fi;
    EXPECT_FLOAT_EQ(fi.default_value, 0.0f);
    EXPECT_EQ(fi.data_ref, -1);

    NifTransformData td;
    EXPECT_EQ(td.num_rotation_keys, 0u);
    EXPECT_EQ(td.num_translation_keys, 0u);
    EXPECT_EQ(td.num_scale_keys, 0u);

    NifFloatData fd;
    EXPECT_EQ(fd.num_keys, 0u);
}

TEST(NIF, BlockSizeSafetyNet) {
    // Build a NiNode block but give it a block_size that is LARGER than the actual
    // data written. The parser should still advance to block_end, not get stuck.
    NifBuilder block;
    write_ni_av_object(block, 0, 0x000E);
    block.u32(0); // num_children
    block.u32(0); // num_effects

    uint32_t actual_size = static_cast<uint32_t>(block.data.size());
    // Pad with 16 extra bytes (simulating unknown trailing data)
    uint32_t padded_size = actual_size + 16;
    for (int i = 0; i < 16; ++i) block.u8(0xCC);

    NifBuilder b;
    write_header(b, {"SafetyNode"}, {"NiNode"}, {0}, {padded_size});
    b.data.insert(b.data.end(), block.data.begin(), block.data.end());

    auto nif = nif_parse(b.data);
    ASSERT_EQ(nif.nodes.size(), 1u);
    EXPECT_EQ(nif.nodes[0].name, "SafetyNode");
}

// ============================================================================
// Tests for newly stored fields (previously consumed but discarded)
// ============================================================================

// A NiNode with extra_data_refs: verify the refs are stored in the output.
TEST(NIF, ExtraDataRefsStored) {
    NifBuilder block;
    // NiObjectNET: name_idx=0, extra_data: 2 refs, controller=-1
    block.s32(0);        // name index
    block.u32(2);        // num_extra_data
    block.s32(7);        // extra_data block ref 0
    block.s32(8);        // extra_data block ref 1
    block.s32(-1);       // controller_ref
    // NiAVObject
    block.u16(0x000E);   // flags
    block.f32(0); block.f32(0); block.f32(0); // translation
    block.identity_matrix(); // rotation
    block.f32(1.0f);     // scale
    block.u32(0);        // num_properties
    block.s32(-1);       // collision_ref
    // NiNode
    block.u32(0);        // num_children
    block.u32(0);        // num_effects

    NifBuilder b;
    write_header(b, {"Node0"}, {"NiNode"}, {0},
                 {static_cast<uint32_t>(block.data.size())});
    b.data.insert(b.data.end(), block.data.begin(), block.data.end());

    auto nif = nif_parse(b.data);
    ASSERT_EQ(nif.nodes.size(), 1u);
    ASSERT_EQ(nif.nodes[0].extra_data_refs.size(), 2u);
    EXPECT_EQ(nif.nodes[0].extra_data_refs[0], 7);
    EXPECT_EQ(nif.nodes[0].extra_data_refs[1], 8);
}

// A NiNode with effect_refs: verify the refs are stored.
TEST(NIF, EffectRefsStored) {
    NifBuilder block;
    block.s32(0); block.u32(0); block.s32(-1); // NiObjectNET (no extra data)
    block.u16(0); block.f32(0); block.f32(0); block.f32(0); // flags + translation
    block.identity_matrix(); block.f32(1.0f); // rotation + scale
    block.u32(0); block.s32(-1); // properties + collision
    block.u32(0);  // num_children
    block.u32(3);  // num_effects
    block.s32(10); block.s32(11); block.s32(12); // effect block refs

    NifBuilder b;
    write_header(b, {"Node0"}, {"NiNode"}, {0},
                 {static_cast<uint32_t>(block.data.size())});
    b.data.insert(b.data.end(), block.data.begin(), block.data.end());

    auto nif = nif_parse(b.data);
    ASSERT_EQ(nif.nodes.size(), 1u);
    ASSERT_EQ(nif.nodes[0].effect_refs.size(), 3u);
    EXPECT_EQ(nif.nodes[0].effect_refs[0], 10);
    EXPECT_EQ(nif.nodes[0].effect_refs[1], 11);
    EXPECT_EQ(nif.nodes[0].effect_refs[2], 12);
}

// A NiTriShape with material binding: verify material_name_indices and active_material.
TEST(NIF, MaterialRefsStored) {
    // String table: 0="Shape0", 1="Mat_Brick", 2="Mat_Stud"
    NifBuilder block;
    // NiObjectNET
    block.s32(0); block.u32(0); block.s32(-1);
    // NiAVObject
    block.u16(0); block.f32(0); block.f32(0); block.f32(0);
    block.identity_matrix(); block.f32(1.0f);
    block.u32(0); block.s32(-1);
    // NiTriShape-specific
    block.s32(-1); // data_ref
    block.s32(-1); // skin_instance_ref
    // Material binding (v >= 10.0.1.0, which 20.3.0.9 satisfies)
    block.u32(2);  // num_materials
    block.s32(1);  // material_name_indices[0] → "Mat_Brick"
    block.s32(2);  // material_name_indices[1] → "Mat_Stud"
    block.s32(-1); // material_extra_data_refs[0]
    block.s32(-1); // material_extra_data_refs[1]
    block.s32(1);  // active_material = 1 (second material is active)
    // NiTriShape v20.2.0.7+ extra refs
    block.s32(-1); // shader_prop_ref
    block.s32(-1); // alpha_prop_ref

    NifBuilder b;
    write_header(b, {"Shape0", "Mat_Brick", "Mat_Stud"}, {"NiTriShape"}, {0},
                 {static_cast<uint32_t>(block.data.size())});
    b.data.insert(b.data.end(), block.data.begin(), block.data.end());

    auto nif = nif_parse(b.data);
    ASSERT_EQ(nif.nodes.size(), 1u);
    ASSERT_EQ(nif.nodes[0].material_name_indices.size(), 2u);
    EXPECT_EQ(nif.nodes[0].material_name_indices[0], 1); // index into string table
    EXPECT_EQ(nif.nodes[0].material_name_indices[1], 2);
    EXPECT_EQ(nif.nodes[0].active_material, 1);
}

// Export info strings are stored in nif.export_info[0..2].
TEST(NIF, ExportInfoStored) {
    // Build a NIF with non-empty export info strings.
    // We need to use write_header which goes through the normal header path.
    // Instead, build the raw bytes manually to include export info.
    NifBuilder b;
    // Header line
    const char* hdr = "Gamebryo File Format, Version 20.3.0.9\n";
    b.str(std::string(hdr));
    b.u32(0x14030009); // version
    b.u8(1);           // endian
    b.u32(12);         // user_version (>= 10, so user_version_2 follows)
    b.u32(0);          // num_blocks
    b.u32(34);         // user_version_2
    // Export info (user_version=12 >= 3): 3 x u8-length-prefixed strings
    b.u8(7); b.str("TestApp");   // export_info[0]
    b.u8(5); b.str("1.2.3");     // export_info[1]
    b.u8(8); b.str("MyScene!");  // export_info[2]
    // Block types: 0 types
    b.u16(0); // num_block_types
    // Block type indices: 0 blocks
    // Block sizes: 0 blocks
    // String table
    b.u32(0); // num_strings
    b.u32(0); // max_string_length
    // Groups
    b.u32(0); // num_groups

    auto nif = nif_parse(b.data);
    EXPECT_EQ(nif.export_info[0], "TestApp");
    EXPECT_EQ(nif.export_info[1], "1.2.3");
    EXPECT_EQ(nif.export_info[2], "MyScene!");
}

// QUADRATIC translation keys store forward/backward tangent vectors.
TEST(NIF, QuadraticTranslationTangentsStored) {
    NifBuilder b3;
    // NiTransformData: 2 QUADRATIC translation keys, no rotation/scale
    b3.u32(0); // num_rotation_keys = 0
    b3.u32(2); b3.u32(2); // num_translation_keys=2, key_type=QUADRATIC
    // Key 0: time=0, value=(1,2,3), fwd=(0.1,0.2,0.3), bwd=(-0.1,-0.2,-0.3)
    b3.f32(0.0f); b3.f32(1.0f); b3.f32(2.0f); b3.f32(3.0f);
    b3.f32(0.1f); b3.f32(0.2f); b3.f32(0.3f);   // forward
    b3.f32(-0.1f); b3.f32(-0.2f); b3.f32(-0.3f); // backward
    // Key 1: time=1, value=(4,5,6), fwd=(0,0,1), bwd=(0,0,-1)
    b3.f32(1.0f); b3.f32(4.0f); b3.f32(5.0f); b3.f32(6.0f);
    b3.f32(0.0f); b3.f32(0.0f); b3.f32(1.0f);
    b3.f32(0.0f); b3.f32(0.0f); b3.f32(-1.0f);
    b3.u32(0); // num_scale_keys=0

    NifBuilder b;
    write_header(b, {}, {"NiTransformData"}, {0},
                 {static_cast<uint32_t>(b3.data.size())});
    b.data.insert(b.data.end(), b3.data.begin(), b3.data.end());

    auto nif = nif_parse(b.data);
    ASSERT_EQ(nif.transform_data.size(), 1u);
    const auto& td = nif.transform_data[0];
    ASSERT_EQ(td.num_translation_keys, 2u);
    EXPECT_EQ(td.translation_key_type, 2u);
    ASSERT_EQ(td.translation_keys.size(), 2u);

    EXPECT_NEAR(td.translation_keys[0].value.x, 1.0f, 1e-5f);
    EXPECT_NEAR(td.translation_keys[0].forward_tangent.x, 0.1f, 1e-5f);
    EXPECT_NEAR(td.translation_keys[0].backward_tangent.x, -0.1f, 1e-5f);
    EXPECT_NEAR(td.translation_keys[1].value.z, 6.0f, 1e-5f);
    EXPECT_NEAR(td.translation_keys[1].forward_tangent.z, 1.0f, 1e-5f);
    EXPECT_NEAR(td.translation_keys[1].backward_tangent.z, -1.0f, 1e-5f);
}

// NifNode default values: new fields must be empty/default.
TEST(NIF, NifNodeNewFieldDefaults) {
    NifNode node;
    EXPECT_TRUE(node.extra_data_refs.empty());
    EXPECT_TRUE(node.effect_refs.empty());
    EXPECT_TRUE(node.material_name_indices.empty());
    EXPECT_TRUE(node.material_extra_data_refs.empty());
    EXPECT_EQ(node.active_material, -1);
}
