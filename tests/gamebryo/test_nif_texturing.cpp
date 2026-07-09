#include <gtest/gtest.h>
#include "gamebryo/nif/nif_reader.h"

#include <vector>

using namespace lu::assets;

namespace {

// Mirrors the NifBuilder/write_header/write_ni_object_net helpers in test_nif.cpp — kept local
// (not shared) since the two test files build unrelated block layouts and a shared header would
// just be indirection for four small helper functions.
struct NifBuilder {
    std::vector<uint8_t> data;
    void u8(uint8_t v) { data.push_back(v); }
    void u16(uint16_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 2);
    }
    void s32(int32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void u32(uint32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void f32(float v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void str(const std::string& s) { data.insert(data.end(), s.begin(), s.end()); }
    void str32(const std::string& s) { u32(static_cast<uint32_t>(s.size())); str(s); }
};

void write_header(NifBuilder& b,
                   const std::vector<std::string>& string_table,
                   const std::vector<std::string>& block_type_names,
                   const std::vector<uint16_t>& block_type_indices,
                   const std::vector<uint32_t>& block_sizes) {
    b.str("Gamebryo File Format, Version 20.3.0.9\n");
    b.u32(0x14030009);
    b.u8(1);
    b.u32(12);
    b.u32(static_cast<uint32_t>(block_type_indices.size()));
    b.u32(34); // user_version_2
    b.u8(0); b.u8(0); b.u8(0); // export info strings

    b.u16(static_cast<uint16_t>(block_type_names.size()));
    for (const auto& name : block_type_names) b.str32(name);
    for (uint16_t idx : block_type_indices) b.u16(idx);
    for (uint32_t sz : block_sizes) b.u32(sz);

    b.u32(static_cast<uint32_t>(string_table.size()));
    uint32_t max_len = 0;
    for (const auto& s : string_table) max_len = std::max<uint32_t>(max_len, s.size());
    b.u32(max_len);
    for (const auto& s : string_table) b.str32(s);

    b.u32(0); // groups
}

void write_ni_object_net(NifBuilder& b, int32_t name_idx, int32_t controller_ref) {
    b.s32(name_idx);
    b.u32(0); // num_extra_data
    b.s32(controller_ref);
}

// Writes a NiSourceTexture block body (post-NiObjectNET), version 20.3.0.9 layout: Use
// External(byte) + FilePath(NiFixedString: string-table index, since 20.1.0.3) + Pixel Data
// ref(i32) + FormatPrefs(3x u32) + Is Static(byte) + Direct Render(bool, since 10.1.0.103) +
// Persist Render Data(bool, since 20.2.0.4).
void write_source_texture_body(NifBuilder& b, int32_t filenameStringIdx) {
    write_ni_object_net(b, -1, -1);
    b.u8(1);           // Use External = true
    b.s32(filenameStringIdx); // FilePath (NiFixedString)
    b.s32(-1);          // Pixel Data ref
    b.u32(0); b.u32(0); b.u32(0); // FormatPrefs
    b.u8(1);            // Is Static
    b.u8(1);            // Direct Render
    b.u8(0);            // Persist Render Data
}

// Writes a TexDesc (version 20.3.0.9: Source ref + packed Flags(u16) + optional transform).
void write_tex_desc(NifBuilder& b, int32_t sourceRef, uint16_t flags = 0) {
    b.s32(sourceRef);
    b.u16(flags);  // Flags (packed clamp+filter)
    b.u8(0);   // Has Texture Transform = false
}

void write_tex_desc_transform(NifBuilder& b, int32_t sourceRef, uint16_t flags = 0) {
    b.s32(sourceRef);
    b.u16(flags);  // Flags (packed clamp+filter)
    b.u8(1);       // Has Texture Transform = true
    b.f32(0.25f); b.f32(-0.5f); // Translation
    b.f32(2.0f); b.f32(0.5f);   // Scale
    b.f32(0.75f);                // Rotation
    b.u32(1);                    // Transform Method
    b.f32(0.5f); b.f32(0.25f);  // Center
}

} // namespace

// Regression test for the exact bug found against cre_cp_spider.nif: at version 20.3.0.9,
// the Decal N texture thresholds are `TextureCount > 8/9/10/11` (since="20.2.0.5"), NOT the
// older `TextureCount > 6/7/8/9` (until="20.2.0.4"). With TextureCount=9, only Decal 0
// qualifies (9>8); a reader using the older thresholds would try to read three decal bools
// (0/1/2) instead of one, desyncing the stream and either misreading Num Shader Textures or
// running past the block boundary entirely (which is what happened: the original bug read
// ~1.2MB past a 45-byte block before the seek-to-block-end safety net masked it downstream).
TEST(NifTexturing, DecalThresholdsUseVersion20_2_0_5Rule) {
    NifBuilder texBlock;
    write_source_texture_body(texBlock, 0); // string_table[0] = filename
    uint32_t texBlockSize = static_cast<uint32_t>(texBlock.data.size());

    NifBuilder propBlock;
    write_ni_object_net(propBlock, -1, -1);
    propBlock.u16(0x0005);      // Flags (TexturingFlags)
    propBlock.u32(9);           // Texture Count = 9 (matches the real file this regresses)
    propBlock.u8(1);            // Has Base Texture
    write_tex_desc(propBlock, 0); // base -> source block 0
    propBlock.u8(1);            // Has Dark Texture
    write_tex_desc(propBlock, 0); // dark -> source block 0 (same texture, doesn't matter)
    propBlock.u8(0);            // Has Detail
    propBlock.u8(0);            // Has Gloss
    propBlock.u8(0);            // Has Glow
    propBlock.u8(0);            // Has Bump (TextureCount=9 > 5, so this bool is read)
    propBlock.u8(0);            // Has Normal (TextureCount=9 > 6, since 20.2.0.5)
    propBlock.u8(0);            // Has Parallax (TextureCount=9 > 7, since 20.2.0.5)
    propBlock.u8(0);            // Has Decal 0 (TextureCount=9 > 8 -- the only decal slot present)
    // NOT read: Decal 1/2/3 -- TextureCount=9 does not exceed 9/10/11.
    propBlock.u32(0);           // Num Shader Textures = 0 -- exactly 45 bytes total, matching
                                 // the real block_size found in cre_cp_spider.nif.
    uint32_t propBlockSize = static_cast<uint32_t>(propBlock.data.size());

    NifBuilder b;
    write_header(b,
        {"MegaMaelstrom2.dds"},
        {"NiSourceTexture", "NiTexturingProperty"},
        {0, 1},
        {texBlockSize, propBlockSize});
    b.data.insert(b.data.end(), texBlock.data.begin(), texBlock.data.end());
    b.data.insert(b.data.end(), propBlock.data.begin(), propBlock.data.end());

    b.u32(0); // footer: Num Roots = 0 (required by the container spec)
    auto nif = nif_parse(b.data);

    ASSERT_EQ(nif.textures.size(), 1u);
    EXPECT_EQ(nif.textures[0].filename, "MegaMaelstrom2.dds");
    EXPECT_TRUE(nif.textures[0].use_external);

    ASSERT_EQ(nif.texturing_properties.size(), 1u);
    EXPECT_EQ(nif.texturing_properties[0].base_texture_source_ref, 0);
    EXPECT_EQ(nif.texturing_properties[0].dark_texture_source_ref, 0);
}

TEST(NifTexturing, TexDescFlagsExposeClampMode) {
    NifBuilder texBlock;
    write_source_texture_body(texBlock, 0);
    uint32_t texBlockSize = static_cast<uint32_t>(texBlock.data.size());

    NifBuilder propBlock;
    write_ni_object_net(propBlock, -1, -1);
    propBlock.u16(0);
    propBlock.u32(2);
    propBlock.u8(1);
    write_tex_desc(propBlock, 0, 3); // Wrap S, wrap T.
    propBlock.u8(1);
    write_tex_desc(propBlock, 0, 1); // Clamp S, wrap T.
    propBlock.u8(0);
    propBlock.u8(0);
    propBlock.u8(0);
    propBlock.u32(0);
    uint32_t propBlockSize = static_cast<uint32_t>(propBlock.data.size());

    NifBuilder b;
    write_header(b,
        {"AddressedTexture.dds"},
        {"NiSourceTexture", "NiTexturingProperty"},
        {0, 1},
        {texBlockSize, propBlockSize});
    b.data.insert(b.data.end(), texBlock.data.begin(), texBlock.data.end());
    b.data.insert(b.data.end(), propBlock.data.begin(), propBlock.data.end());

    b.u32(0);
    auto nif = nif_parse(b.data);

    ASSERT_EQ(nif.texturing_properties.size(), 1u);
    const auto& prop = nif.texturing_properties[0];
    EXPECT_TRUE(prop.base_texture_has_clamp_mode);
    EXPECT_EQ(prop.base_texture_clamp_mode, 3);
    EXPECT_TRUE(prop.dark_texture_has_clamp_mode);
    EXPECT_EQ(prop.dark_texture_clamp_mode, 1);
}

TEST(NifTexturing, TexDescPreservesTextureTransform) {
    NifBuilder texBlock;
    write_source_texture_body(texBlock, 0);
    uint32_t texBlockSize = static_cast<uint32_t>(texBlock.data.size());

    NifBuilder propBlock;
    write_ni_object_net(propBlock, -1, -1);
    propBlock.u16(0);
    propBlock.u32(1);
    propBlock.u8(1);
    write_tex_desc_transform(propBlock, 0, 3);
    propBlock.u8(0);
    propBlock.u8(0);
    propBlock.u8(0);
    propBlock.u8(0);
    propBlock.u32(0);
    uint32_t propBlockSize = static_cast<uint32_t>(propBlock.data.size());

    NifBuilder b;
    write_header(b,
        {"TransformedTexture.dds"},
        {"NiSourceTexture", "NiTexturingProperty"},
        {0, 1},
        {texBlockSize, propBlockSize});
    b.data.insert(b.data.end(), texBlock.data.begin(), texBlock.data.end());
    b.data.insert(b.data.end(), propBlock.data.begin(), propBlock.data.end());

    b.u32(0);
    auto nif = nif_parse(b.data);

    ASSERT_EQ(nif.texturing_properties.size(), 1u);
    const auto& transform = nif.texturing_properties[0].base_texture_transform;
    EXPECT_TRUE(transform.enabled);
    EXPECT_FLOAT_EQ(transform.translation.u, 0.25f);
    EXPECT_FLOAT_EQ(transform.translation.v, -0.5f);
    EXPECT_FLOAT_EQ(transform.scale.u, 2.0f);
    EXPECT_FLOAT_EQ(transform.scale.v, 0.5f);
    EXPECT_FLOAT_EQ(transform.rotation, 0.75f);
    EXPECT_EQ(transform.method, 1u);
    EXPECT_FLOAT_EQ(transform.center.u, 0.5f);
    EXPECT_FLOAT_EQ(transform.center.v, 0.25f);
}

// ResolveBaseTextureFilename walks node.properties[] -> NiTexturingProperty (by block_index) ->
// base_texture_source_ref -> NifTextureRef (by block_index). This exercises the full chain via
// a NiTriShape referencing a NiTexturingProperty referencing a NiSourceTexture, matching how a
// real textured mesh's NIF is structured.
TEST(NifTexturing, ResolveBaseTextureFilenameWalksFullChain) {
    NifBuilder texBlock;
    write_source_texture_body(texBlock, 0);
    uint32_t texBlockSize = static_cast<uint32_t>(texBlock.data.size());

    NifBuilder propBlock;
    write_ni_object_net(propBlock, -1, -1);
    propBlock.u16(0);  // Flags
    propBlock.u32(1);  // Texture Count = 1 (only base slot)
    propBlock.u8(1);   // Has Base Texture
    write_tex_desc(propBlock, 0); // -> source block 0
    propBlock.u8(0);   // Has Dark
    propBlock.u8(0);   // Has Detail
    propBlock.u8(0);   // Has Gloss
    propBlock.u8(0);   // Has Glow
    // TextureCount=1, not > 5/6/7/8/9/10/11, so no bump/normal/parallax/decal bools are read.
    propBlock.u32(0);  // Num Shader Textures = 0
    uint32_t propBlockSize = static_cast<uint32_t>(propBlock.data.size());

    // NiTriShape: NiAVObject fields (NiObjectNET + flags(u16) + translation(3f) +
    // rotation(9f) + scale(f32) + properties[1]={block 1} + collision_ref), then NiTriShape's
    // own fields: data_ref, skin_instance_ref, material binding (since>=10.0.1.0: count + name
    // idx array + extra data ref array + active_material), shader/alpha property refs
    // (since>=20.2.0.7) — matches parse_ni_tri_shape exactly (nif_reader.cpp). data_ref/
    // skin_instance_ref are -1 (no mesh data needed; ResolveBaseTextureFilename only reads
    // node.properties[]).
    NifBuilder triShapeBlock;
    triShapeBlock.s32(-1);       // NiObjectNET: name_idx
    triShapeBlock.u32(0);        // NiObjectNET: num_extra_data
    triShapeBlock.s32(-1);       // NiObjectNET: controller_ref
    triShapeBlock.u16(0x000E);   // flags (u16)
    triShapeBlock.f32(0); triShapeBlock.f32(0); triShapeBlock.f32(0); // translation
    for (int i = 0; i < 9; ++i) triShapeBlock.f32(i % 4 == 0 ? 1.0f : 0.0f); // identity rotation
    triShapeBlock.f32(1.0f);     // scale
    triShapeBlock.u32(1);        // num_properties = 1
    triShapeBlock.s32(1);        // properties[0] = block 1 (the NiTexturingProperty)
    triShapeBlock.s32(-1);       // collision_ref
    triShapeBlock.s32(-1);       // data_ref
    triShapeBlock.s32(-1);       // skin_instance_ref
    triShapeBlock.u32(0);        // num_materials = 0 (material binding, since >= 10.0.1.0)
    triShapeBlock.s32(-1);       // active_material
    triShapeBlock.s32(-1);       // shader property ref (since >= 20.2.0.7)
    triShapeBlock.s32(-1);       // alpha property ref (since >= 20.2.0.7)
    uint32_t triShapeBlockSize = static_cast<uint32_t>(triShapeBlock.data.size());

    NifBuilder b;
    write_header(b,
        {"SpiderTexture.dds"},
        {"NiSourceTexture", "NiTexturingProperty", "NiTriShape"},
        {0, 1, 2},
        {texBlockSize, propBlockSize, triShapeBlockSize});
    b.data.insert(b.data.end(), texBlock.data.begin(), texBlock.data.end());
    b.data.insert(b.data.end(), propBlock.data.begin(), propBlock.data.end());
    b.data.insert(b.data.end(), triShapeBlock.data.begin(), triShapeBlock.data.end());

    b.u32(0); // footer: Num Roots = 0 (required by the container spec)
    auto nif = nif_parse(b.data);

    ASSERT_EQ(nif.nodes.size(), 1u);
    ASSERT_EQ(nif.texturing_properties.size(), 1u);
    ASSERT_EQ(nif.textures.size(), 1u);

    std::string resolved = nif.ResolveBaseTextureFilename(nif.nodes[0]);
    EXPECT_EQ(resolved, "SpiderTexture.dds");
}

TEST(NifTexturing, ResolveBaseTextureFilenameReturnsEmptyWhenNoTexturingProperty) {
    NifBuilder triShapeBlock;
    triShapeBlock.s32(-1);
    triShapeBlock.u32(0);
    triShapeBlock.s32(-1);
    triShapeBlock.u16(0x000E);
    triShapeBlock.f32(0); triShapeBlock.f32(0); triShapeBlock.f32(0);
    for (int i = 0; i < 9; ++i) triShapeBlock.f32(i % 4 == 0 ? 1.0f : 0.0f);
    triShapeBlock.f32(1.0f);
    triShapeBlock.u32(0);  // num_properties = 0 -- no texturing property attached
    triShapeBlock.s32(-1); // collision_ref
    triShapeBlock.s32(-1); // data_ref
    triShapeBlock.s32(-1); // skin_instance_ref
    triShapeBlock.u32(0);  // num_materials = 0
    triShapeBlock.s32(-1); // active_material
    triShapeBlock.s32(-1); // shader property ref
    triShapeBlock.s32(-1); // alpha property ref
    uint32_t triShapeBlockSize = static_cast<uint32_t>(triShapeBlock.data.size());

    NifBuilder b;
    write_header(b, {}, {"NiTriShape"}, {0}, {triShapeBlockSize});
    b.data.insert(b.data.end(), triShapeBlock.data.begin(), triShapeBlock.data.end());

    b.u32(0); // footer: Num Roots = 0 (required by the container spec)
    auto nif = nif_parse(b.data);
    ASSERT_EQ(nif.nodes.size(), 1u);
    EXPECT_EQ(nif.ResolveBaseTextureFilename(nif.nodes[0]), "");
}

// Use External = false with an embedded Pixel Data ref (no filename) — ResolveBaseTextureFilename
// should return empty, since there's no external file path to load, matching the real client's
// behavior of pulling embedded pixel data from a NiPixelData block instead (not modeled here).
TEST(NifTexturing, InternalTextureHasNoFilename) {
    NifBuilder texBlock;
    write_ni_object_net(texBlock, -1, -1);
    texBlock.u8(0);   // Use External = false
    texBlock.s32(-1); // FilePath (embedded-image original name) -- string idx -1 = none
    texBlock.s32(2);  // Pixel Data ref -> some NiPixelData block (not built in this test)
    texBlock.u32(0); texBlock.u32(0); texBlock.u32(0); // FormatPrefs
    texBlock.u8(1);   // Is Static
    texBlock.u8(0);   // Direct Render
    texBlock.u8(0);   // Persist Render Data
    uint32_t texBlockSize = static_cast<uint32_t>(texBlock.data.size());

    NifBuilder b;
    write_header(b, {}, {"NiSourceTexture"}, {0}, {texBlockSize});
    b.data.insert(b.data.end(), texBlock.data.begin(), texBlock.data.end());

    b.u32(0); // footer: Num Roots = 0 (required by the container spec)
    auto nif = nif_parse(b.data);
    ASSERT_EQ(nif.textures.size(), 1u);
    EXPECT_FALSE(nif.textures[0].use_external);
    EXPECT_EQ(nif.textures[0].filename, "");
    EXPECT_EQ(nif.textures[0].pixel_data_ref, 2);
}
