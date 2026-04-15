#include "forkparticle/psb/psb_reader.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <vector>
#include <span>

// ── helpers ────────────────────────────────────────────────────────────────

static std::vector<uint8_t> make_bytes() { return {}; }

static void append_u32(std::vector<uint8_t>& b, uint32_t v) {
    uint8_t buf[4];
    std::memcpy(buf, &v, 4);
    b.insert(b.end(), buf, buf + 4);
}
static void append_f32(std::vector<uint8_t>& b, float v) {
    uint8_t buf[4];
    std::memcpy(buf, &v, 4);
    b.insert(b.end(), buf, buf + 4);
}
static void append_zeros(std::vector<uint8_t>& b, size_t n) {
    b.insert(b.end(), n, 0);
}

// Build a minimal valid 420-byte PSB data block followed by a texture array.
// All unspecified fields are zero. The texture array holds one entry with the
// given path at the fixed offset 420 (right after the data block).
static std::vector<uint8_t> make_minimal_psb(
    uint32_t particle_id = 1,
    float    life_var    = 1.0f,
    float    initial_scale = 2.0f,
    const std::string& tex_path = "forkp/textures/dds/test.dds")
{
    constexpr uint32_t DATA_SIZE    = 420;
    constexpr uint32_t NUM_TEXTURES = 1;
    constexpr uint32_t TEX_OFFSET  = 420; // immediately after data block

    std::vector<uint8_t> b;
    b.reserve(DATA_SIZE + 64);

    // HEADER (0x00–0x0F)
    append_u32(b, 80);           // header_size
    append_u32(b, DATA_SIZE);    // data_size
    append_u32(b, particle_id);  // particle_id
    append_u32(b, 112);          // section_offset

    // COLOR BLOCK (0x10–0x4F, 4 × RGBA, all zero)
    append_zeros(b, 64);

    // PARTICLE PROPERTIES (0x50–0x6B)
    append_f32(b, 0.0f);         // color_ratio_1    0x50: *PCOLORRATIO
    append_f32(b, 0.5f);         // color_ratio_2    0x54: *PCOLORRATIO2
    append_f32(b, 1.0f);         // life_min         0x58: *PLIFEMIN
    append_f32(b, life_var);     // life_var          0x5C: *PLIFEVAR
    append_f32(b, 0.0f);         // vel_min           0x60: *PVELMIN
    append_f32(b, 0.0f);         // vel_var            0x64: *PVELVAR
    append_u32(b, 0x00000001);   // flags             0x68: *PFLAGS

    // SCALE PROPERTIES (0x6C–0x78)
    append_f32(b, initial_scale);// initial_scale     0x6C: *PISCALE
    append_zeros(b, 12);         // trans_scale, final_scale, scale_ratio

    // ROTATION + DRAG (0x7C–0x84)
    append_zeros(b, 12);         // rot_min, rot_var, drag

    // SCALE VECTOR (0x88–0x97)
    append_f32(b, 1.0f);         // scale[0]          0x88: *SCALE
    append_f32(b, 2.0f);         // scale[1]          0x8C
    append_f32(b, 1.0f);         // scale[2]          0x90 (not read by runtime)
    append_f32(b, 1.0f);         // scale[3]          0x94 (not read by runtime)

    // ROTATION VECTOR (0x98–0xA7)
    append_f32(b, 0.0f);         // rotation[0]       0x98: *ROTATION
    append_zeros(b, 12);         // rotation[1-3] (always zero)

    // TINT COLOR (0xA8–0xB7)
    append_zeros(b, 16);

    // SCALE VARIATION (0xB8–0xC3)
    append_zeros(b, 12);         // iscale_var, tscale_var, fscale_var

    // EMITTER PROPERTIES (0xC4–0xEB)
    append_f32(b, 0.0f);         // sim_life          0xC4: *ESIMLIFE
    append_f32(b, 100.0f);       // emitter_life      0xC8: *ELIFE (default 100)
    append_f32(b, 500.0f);       // emit_rate         0xCC: *ERATE
    append_f32(b, 0.0f);         // gravity           0xD0: *EGRAVITY
    append_zeros(b, 12);         // plane_w/h/d       0xD4-0xDC
    append_f32(b, 0.0f);         // cone_radius       0xE0: *ECONERAD
    append_f32(b, 0.0f);         // max_particles     0xE4: *EMAXPARTICLE
    append_u32(b, 0);            // volume_type       0xE8: *EVOLUME

    // BOUNDS BLOCK (0xEC–0x107)
    append_zeros(b, 24);         // bounds_min/max
    append_f32(b, 0.0f);         // num_burst         0x104: *NBURST

    // METADATA BLOCK (0x108–0x1A3)
    append_f32(b, 0.0f);         // anim_speed        0x108: *ANMSPEED
    append_u32(b, 1);            // blend_mode        0x10C: *EBLENDMODE (add)
    append_f32(b, 1.0f);         // time_delta_mult   0x110: *TDELTAMULT
    append_u32(b, 0);            // num_point_forces  0x114: *NUMPOINTFORCES
    append_u32(b, static_cast<uint32_t>(DATA_SIZE + 64)); // file_total_size 0x118
    append_u32(b, 412);          // emitter_params_size 0x11C
    append_u32(b, DATA_SIZE);    // data_block_size   0x120
    append_u32(b, NUM_TEXTURES); // num_assets        0x124: *NUMASSETS
    append_u32(b, 0xDEADBEEF);   // runtime_ptr_A     0x128 (junk)
    append_u32(b, TEX_OFFSET);   // texture_data_offset 0x12C

    // 0x130-0x13F
    append_u32(b, 0);            // num_emission_assets 0x130
    append_u32(b, static_cast<uint32_t>(DATA_SIZE + 64)); // extra_size_134
    append_u32(b, 0);            // anim_data_offset (static)
    append_u32(b, 832);          // texture_base_offset (always 832)

    // 0x140-0x183: 68 bytes zero (runtime state)
    append_zeros(b, 68);

    // 0x184: file_total_size duplicate
    append_u32(b, static_cast<uint32_t>(DATA_SIZE + 64));

    // 0x188-0x190: path properties
    append_f32(b, 1.0f);         // path_dist_min     0x188
    append_f32(b, 1.0f);         // path_dist_var     0x18C
    append_f32(b, 10.0f);        // path_speed        0x190

    // 0x194-0x1A3 (last 16 bytes of the 420-byte data block)
    append_u32(b, 0);            // emitter_name_present  (0x194)
    append_u32(b, 0);            // emitter_name_offset   (0x198)
    append_u32(b, 0);            // reserved              (0x19C)
    append_u32(b, 0);            // reserved              (0x1A0)

    // Should be at byte 420 now
    assert(b.size() == DATA_SIZE);

    // Texture array: 1 entry × 64 bytes
    append_u32(b, 1);            // type = TEXTURE_ENTRY_TYPE_PATH
    // path (60 bytes, null-padded)
    size_t path_len = std::min(tex_path.size(), size_t(59));
    b.insert(b.end(), tex_path.begin(), tex_path.begin() + path_len);
    b.push_back('\0');
    append_zeros(b, 60 - path_len - 1);  // remaining padding

    return b;
}

// ── structural tests ────────────────────────────────────────────────────────

TEST(PsbParse, RejectsTooSmall) {
    std::vector<uint8_t> tiny(8, 0);
    EXPECT_THROW(lu::assets::psb_parse(tiny), lu::assets::PsbError);
}

TEST(PsbParse, RejectsTruncatedDataBlock) {
    // Header says data_size=420 but file is only 100 bytes
    std::vector<uint8_t> b(100, 0);
    // Write data_size = 420 at offset 4
    uint32_t ds = 420;
    std::memcpy(b.data() + 4, &ds, 4);
    EXPECT_THROW(lu::assets::psb_parse(b), lu::assets::PsbError);
}

// ── header field parsing ────────────────────────────────────────────────────

TEST(PsbParse, ParsesHeader) {
    auto b = make_minimal_psb(/*particle_id=*/42);
    auto psb = lu::assets::psb_parse(b);

    EXPECT_EQ(psb.header_size,    80u);
    EXPECT_EQ(psb.data_size,      420u);
    EXPECT_EQ(psb.particle_id,    42u);
    EXPECT_EQ(psb.section_offset, 112u);
}

// ── particle properties ────────────────────────────────────────────────────

TEST(PsbParse, ParsesParticleProperties) {
    auto b = make_minimal_psb(1, /*life_var=*/3.5f);
    auto psb = lu::assets::psb_parse(b);

    EXPECT_FLOAT_EQ(psb.color_ratio_2, 0.5f);
    EXPECT_FLOAT_EQ(psb.life_min,      1.0f);
    EXPECT_FLOAT_EQ(psb.life_var,      3.5f);
    EXPECT_EQ      (psb.flags, 0x00000001u);
}

// ── scale properties ───────────────────────────────────────────────────────

TEST(PsbParse, ParsesInitialScale) {
    auto b = make_minimal_psb(1, 1.0f, /*initial_scale=*/7.25f);
    auto psb = lu::assets::psb_parse(b);
    EXPECT_FLOAT_EQ(psb.initial_scale, 7.25f);
}

// ── emitter properties ─────────────────────────────────────────────────────

TEST(PsbParse, ParsesEmitRate) {
    auto b = make_minimal_psb();
    auto psb = lu::assets::psb_parse(b);
    EXPECT_FLOAT_EQ(psb.emit_rate, 500.0f);
}

// ── metadata fields ─────────────────────────────────────────────────────────

TEST(PsbParse, ParsesMetadata) {
    auto b = make_minimal_psb();
    auto psb = lu::assets::psb_parse(b);

    EXPECT_EQ(psb.blend_mode,           1u);       // add
    EXPECT_FLOAT_EQ(psb.time_delta_mult, 1.0f);
    EXPECT_EQ(psb.num_point_forces,      0u);
    EXPECT_EQ(psb.num_assets,            1u);
    EXPECT_FLOAT_EQ(psb.path_dist_min,   1.0f);
    EXPECT_FLOAT_EQ(psb.path_dist_var,   1.0f);
    EXPECT_FLOAT_EQ(psb.path_speed,      10.0f);
}

TEST(PsbParse, AnimDataOffsetZeroWhenStatic) {
    auto b = make_minimal_psb();
    auto psb = lu::assets::psb_parse(b);
    EXPECT_EQ(psb.anim_data_offset, 0u);
}

// ── texture extraction ──────────────────────────────────────────────────────

TEST(PsbParse, ExtractsTexturePath) {
    auto b = make_minimal_psb(1, 1.0f, 1.0f, "forkp/textures/dds/fire.dds");
    auto psb = lu::assets::psb_parse(b);

    ASSERT_EQ(psb.textures.size(), 1u);
    EXPECT_EQ(psb.textures[0], "forkp/textures/dds/fire.dds");
}

TEST(PsbParse, EmitterNameEmptyWhenFlagZero) {
    auto b = make_minimal_psb();
    auto psb = lu::assets::psb_parse(b);
    EXPECT_TRUE(psb.emitter_name.empty());
}

// ── golden test: maelstrom_imagination3.psb ─────────────────────────────────
//
// Source: client res/forkp/effects/
//         character/sorcerer/maelstrom_imagination3.psb
// Verified field-by-field against the raw binary.

#include <fstream>
#include <filesystem>

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// Absolute path set by CMake via TEST_DATA_DIR compile definition.
// The file is checked into tests/forkparticle/.
static constexpr const char* GOLDEN_PSB =
    TEST_DATA_DIR "/maelstrom_imagination3.psb";

class PsbGolden : public ::testing::Test {
protected:
    void SetUp() override {
        data = read_file(GOLDEN_PSB);
        if (data.empty()) {
            GTEST_SKIP() << "Golden PSB not found at " << GOLDEN_PSB;
        }
        psb = lu::assets::psb_parse(data);
    }
    std::vector<uint8_t> data;
    lu::assets::PsbFile  psb;
};

TEST_F(PsbGolden, FileSize) {
    EXPECT_EQ(data.size(), 2136u);
}

TEST_F(PsbGolden, Header) {
    EXPECT_EQ(psb.header_size,    80u);
    EXPECT_EQ(psb.data_size,      420u);
    EXPECT_EQ(psb.particle_id,    169u);
    EXPECT_EQ(psb.section_offset, 112u);
}

TEST_F(PsbGolden, ParticleProperties) {
    EXPECT_FLOAT_EQ(psb.life_var,  0.4f);
    EXPECT_EQ      (psb.flags,     0x00000001u);
}

TEST_F(PsbGolden, ScaleProperties) {
    EXPECT_FLOAT_EQ(psb.initial_scale, 0.2f);
}

TEST_F(PsbGolden, RotationBlock) {
    EXPECT_FLOAT_EQ(psb.rotation[0], 0.0f);
}

TEST_F(PsbGolden, EmitterProperties) {
    EXPECT_FLOAT_EQ(psb.emit_rate, 40.0f);
}

TEST_F(PsbGolden, VolumeType) {
    EXPECT_EQ(psb.volume_type, 0u);
}

TEST_F(PsbGolden, Metadata) {
    EXPECT_EQ      (psb.blend_mode,          6u);    // alpha/blend
    EXPECT_FLOAT_EQ(psb.time_delta_mult,     1.3f);
    EXPECT_EQ      (psb.num_point_forces,    0u);
    EXPECT_EQ      (psb.file_total_size,     2136u);
    EXPECT_EQ      (psb.emitter_params_size, 412u);
    EXPECT_EQ      (psb.num_assets,          2u);
    EXPECT_EQ      (psb.texture_data_offset, 2008u);
}

TEST_F(PsbGolden, AnimDataOffset) {
    EXPECT_EQ(psb.anim_data_offset,    1176u);  // has animation data
    EXPECT_EQ(psb.texture_base_offset, 832u);   // always 832
}

TEST_F(PsbGolden, PathProperties) {
    EXPECT_FLOAT_EQ(psb.path_dist_min, 1.0f);
    EXPECT_FLOAT_EQ(psb.path_dist_var, 1.0f);
    EXPECT_FLOAT_EQ(psb.path_speed,    10.0f);
}

TEST_F(PsbGolden, TexturePaths) {
    ASSERT_EQ(psb.textures.size(), 2u);
    EXPECT_EQ(psb.textures[0], "casts_texture_page.tga");
    EXPECT_EQ(psb.textures[1], "casts_texture_page.tga");
}

TEST_F(PsbGolden, EmitterNameAbsent) {
    EXPECT_TRUE(psb.emitter_name.empty());
}
