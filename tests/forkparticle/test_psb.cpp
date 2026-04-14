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
//
// Caller can override individual bytes if needed.
static std::vector<uint8_t> make_minimal_psb(
    uint32_t particle_id = 1,
    float    birth_rate  = 1.0f,
    float    emit_speed  = 2.0f,
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

    // TIMING BLOCK (0x50–0x6B)
    append_f32(b, 0.0f);         // birth_delay
    append_f32(b, 0.5f);         // life_min
    append_f32(b, 1.0f);         // life_max
    append_f32(b, birth_rate);   // birth_rate
    append_f32(b, 0.0f);         // death_delay
    append_f32(b, 0.0f);         // emit_period
    append_u32(b, 0x00000001);   // flags

    // VELOCITY BLOCK (0x6C–0x87)
    append_f32(b, emit_speed);   // emit_speed
    append_zeros(b, 24);         // speed_x/y/z, gravity, spread_angle, rotation_speed

    // SIZE BLOCK (0x88–0x97)
    append_f32(b, 1.0f);         // size_start
    append_f32(b, 2.0f);         // size_end
    append_f32(b, 1.0f);         // size_mult (not read at runtime)
    append_f32(b, 1.0f);         // size_alpha (not read at runtime)

    // ROTATION BLOCK (0x98–0xA7)
    append_f32(b, 0.0f);         // initial_rotation
    append_zeros(b, 12);         // pad_rotation 1/2/3 (always zero)

    // COLOR2 BLOCK (0xA8–0xB7)
    append_zeros(b, 16);

    // ACCELERATION BLOCK (0xB8–0xCF)
    append_zeros(b, 8);          // accel_x/y
    append_f32(b, 0.0f);         // accel_z
    append_f32(b, 0.0f);         // pad_accel (0xC4)
    append_f32(b, 100.0f);       // format_const_100 (0xC8, always 100)
    append_f32(b, 500.0f);       // max_draw_dist

    // SPIN BLOCK (0xD0–0xEB)
    append_zeros(b, 24);         // spin_start/min/max/var/damp/speed
    append_u32(b, 0);            // spin_flags

    // BOUNDS BLOCK (0xEC–0x107)
    append_zeros(b, 28);         // bounds_min/max + pad

    // METADATA BLOCK (0x108–0x1A3)
    append_f32(b, 0.0f);         // emit_rate_final    0x108
    append_u32(b, 1);            // texture_blend_mode 0x10C (add)
    append_f32(b, 1.0f);         // playback_scale     0x110
    append_u32(b, 0);            // loop_count         0x114
    append_u32(b, static_cast<uint32_t>(DATA_SIZE + 64)); // file_total_size 0x118
    append_u32(b, 412);          // emitter_params_size 0x11C
    append_u32(b, DATA_SIZE);    // data_block_size    0x120
    append_u32(b, NUM_TEXTURES); // num_textures       0x124
    append_u32(b, 0xDEADBEEF);   // runtime_ptr_A      0x128 (junk)
    append_u32(b, TEX_OFFSET);   // texture_data_offset 0x12C

    // 0x130-0x13F
    append_u32(b, 0);            // flag_extra_130
    append_u32(b, static_cast<uint32_t>(DATA_SIZE + 64)); // extra_size_134
    append_u32(b, 0);            // anim_data_offset (static)
    append_u32(b, 832);          // texture_base_offset (always 832)

    // 0x140-0x183: 68 bytes zero (runtime state)
    append_zeros(b, 68);

    // 0x184: file_total_size duplicate
    append_u32(b, static_cast<uint32_t>(DATA_SIZE + 64));

    // 0x188-0x190: scale fields
    append_f32(b, 1.0f);         // scale_188
    append_f32(b, 1.0f);         // scale_18c
    append_f32(b, 10.0f);        // scale_190

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

// ── timing fields ───────────────────────────────────────────────────────────

TEST(PsbParse, ParsesTimingBlock) {
    auto b = make_minimal_psb(1, /*birth_rate=*/3.5f);
    auto psb = lu::assets::psb_parse(b);

    EXPECT_FLOAT_EQ(psb.life_min,   0.5f);
    EXPECT_FLOAT_EQ(psb.life_max,   1.0f);
    EXPECT_FLOAT_EQ(psb.birth_rate, 3.5f);
    EXPECT_EQ      (psb.flags, 0x00000001u);
}

// ── velocity fields ─────────────────────────────────────────────────────────

TEST(PsbParse, ParsesEmitSpeed) {
    auto b = make_minimal_psb(1, 1.0f, /*emit_speed=*/7.25f);
    auto psb = lu::assets::psb_parse(b);
    EXPECT_FLOAT_EQ(psb.emit_speed, 7.25f);
}

// ── acceleration block ──────────────────────────────────────────────────────

TEST(PsbParse, ParsesMaxDrawDist) {
    auto b = make_minimal_psb();
    auto psb = lu::assets::psb_parse(b);
    EXPECT_FLOAT_EQ(psb.max_draw_dist, 500.0f);
}

// ── metadata fields ─────────────────────────────────────────────────────────

TEST(PsbParse, ParsesMetadata) {
    auto b = make_minimal_psb();
    auto psb = lu::assets::psb_parse(b);

    EXPECT_EQ(psb.texture_blend_mode,  1u);       // add
    EXPECT_FLOAT_EQ(psb.playback_scale, 1.0f);
    EXPECT_EQ(psb.loop_count,           0u);
    EXPECT_EQ(psb.num_textures,         1u);
    EXPECT_FLOAT_EQ(psb.scale_188,      1.0f);
    EXPECT_FLOAT_EQ(psb.scale_18c,      1.0f);
    EXPECT_FLOAT_EQ(psb.scale_190,     10.0f);
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

TEST_F(PsbGolden, TimingBlock) {
    EXPECT_FLOAT_EQ(psb.birth_rate,  0.4f);
    EXPECT_EQ      (psb.flags,       0x00000001u);
}

TEST_F(PsbGolden, VelocityBlock) {
    EXPECT_FLOAT_EQ(psb.emit_speed, 0.2f);
}

TEST_F(PsbGolden, RotationBlock) {
    EXPECT_FLOAT_EQ(psb.initial_rotation, 0.0f);
}

TEST_F(PsbGolden, AccelerationBlock) {
    EXPECT_FLOAT_EQ(psb.max_draw_dist, 40.0f);
}

TEST_F(PsbGolden, SpinBlock) {
    EXPECT_EQ(psb.spin_flags, 0u);
}

TEST_F(PsbGolden, Metadata) {
    EXPECT_EQ      (psb.texture_blend_mode,  6u);    // alpha/blend
    EXPECT_FLOAT_EQ(psb.playback_scale,      1.3f);
    EXPECT_EQ      (psb.loop_count,          0u);
    EXPECT_EQ      (psb.file_total_size,     2136u);
    EXPECT_EQ      (psb.emitter_params_size, 412u);
    EXPECT_EQ      (psb.num_textures,        2u);
    EXPECT_EQ      (psb.texture_data_offset, 2008u);
}

TEST_F(PsbGolden, AnimDataOffset) {
    EXPECT_EQ(psb.anim_data_offset,    1176u);  // has animation data
    EXPECT_EQ(psb.texture_base_offset, 832u);   // always 832
}

TEST_F(PsbGolden, ScaleFields) {
    EXPECT_FLOAT_EQ(psb.scale_188, 1.0f);
    EXPECT_FLOAT_EQ(psb.scale_18c, 1.0f);
    EXPECT_FLOAT_EQ(psb.scale_190, 10.0f);
}

TEST_F(PsbGolden, TexturePaths) {
    ASSERT_EQ(psb.textures.size(), 2u);
    EXPECT_EQ(psb.textures[0], "casts_texture_page.tga");
    EXPECT_EQ(psb.textures[1], "casts_texture_page.tga");
}

TEST_F(PsbGolden, EmitterNameAbsent) {
    EXPECT_TRUE(psb.emitter_name.empty());
}
