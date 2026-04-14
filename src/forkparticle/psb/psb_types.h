#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <array>
#include <stdexcept>

namespace lu::assets {

// PSB (Particle System Binary) — ForkParticle proprietary emitter format.
// 11,366 .psb files in the client under forkp/effects/.
//
// REVERSE ENGINEERING SOURCE
//   Primary:   legouniverse.exe FUN_01092450 — maps every PSB field to the
//              runtime ForkParticle emitter struct (complete field-by-field).
//   Secondary: legouniverse.exe FUN_010cdbf0 — emitter initializer; reads
//              PSB+0x68 (flags), PSB+0x10C (blend mode via FUN_01092380),
//              PSB+0x124 (num_textures loop), PSB+0x12C (texture array ptr),
//              PSB+0x138 (frame count), PSB+0x13C (anim data ptr),
//              PSB+0x194 (name present flag), PSB+0x198 (name ptr).
//   Tertiary:  Statistical analysis of all 11,366 unpacked client PSBs.
//              NO public ForkParticle SDK documentation exists.
//
// BINARY LAYOUT (sequential, little-endian):
//
// HEADER (0x00–0x0F, 16 bytes):
//   [0x00] u32 header_size      — Always 80 (0x50)
//   [0x04] u32 data_size        — Always 420 (0x1A4); size of main param block
//   [0x08] u32 particle_id      — Emitter index within the effect
//   [0x0C] u32 section_offset   — Always 112 (0x70); legacy ForkParticle field
//
// COLOR BLOCK (0x10–0x4F, 64 bytes = 4 × RGBA float):
//   [0x10] f32×4 start_color    — Particle color at birth
//   [0x20] f32×4 middle_color   — Particle color at midlife
//   [0x30] f32×4 end_color      — Particle color at death
//   [0x40] f32×4 birth_color    — Additional birth tint (often white/transparent)
//
// TIMING BLOCK (0x50–0x6B, 28 bytes):
//   [0x50] f32 birth_delay      — Seconds before first emission
//   [0x54] f32 life_min         — Minimum particle lifetime (seconds)
//   [0x58] f32 life_max         — Maximum particle lifetime (seconds)
//   [0x5C] f32 birth_rate       — Particles emitted per second
//   [0x60] f32 death_delay      — Seconds after emission ends before emitter dies
//   [0x64] f32 emit_period      — Period/duration of one emission cycle (seconds).
//                                 0 = single burst; positive = repeating. Range
//                                 −500..1000 across 11,366 files; 68.5% are 0.
//                                 Stored in emitter+0x13C (FUN_01092450).
//   [0x68] u32 flags            — Emitter flags; low bits drive blend mode
//                                 (FUN_010cdbf0: bits 0x8/0x10/0x20/0x40/
//                                 0x80000/0x800000/0x1000000/0x2000000/0x4000000
//                                 map to blend mode values 1–9)
//
// VELOCITY BLOCK (0x6C–0x87, 28 bytes):
//   [0x6C] f32 emit_speed       — Base emission speed (units/s)
//   [0x70] f32 speed_x          — Initial X velocity (units/s)
//   [0x74] f32 speed_y          — Initial Y velocity (units/s)
//   [0x78] f32 speed_z          — Initial Z velocity (units/s)
//   [0x7C] f32 gravity          — Downward gravity acceleration (units/s²)
//   [0x80] f32 spread_angle     — Emission cone half-angle (degrees)
//   [0x84] f32 rotation_speed   — Per-particle angular velocity at birth
//                                 (degrees/s). Stored in emitter+0x174 adjacent
//                                 to initial_rotation (FUN_01092450). Range
//                                 −110..10; 33.9% zero.
//
// SIZE BLOCK (0x88–0x97, 16 bytes):
//   [0x88] f32 size_start       — Particle size at birth. Multiplied by engine
//                                 constant before use (FUN_01092450 → emitter+0x150).
//   [0x8C] f32 size_end         — Particle size at death (same scaling).
//   [0x90] f32 size_mult        — Size multiplier. Written by ForkParticle Designer
//                                 but NOT read by the game runtime (no access in
//                                 FUN_01092450 or FUN_010cdbf0). Likely pre-baked.
//   [0x94] f32 size_alpha       — Alpha blend scalar. Same — written but not read.
//
// ROTATION BLOCK (0x98–0xA7, 16 bytes):
//   [0x98] f32 initial_rotation — Initial particle rotation angle (degrees).
//                                 Converted to radians by engine: stored as
//                                 (π/180) × value in emitter+0x170 (FUN_01092450).
//                                 Range −736..1000 across all files; 62% zero.
//   [0x9C] f32 pad_rotation_1   — Always 0.0 in all 11,366 files. Reserved.
//   [0xA0] f32 pad_rotation_2   — Always 0.0. Reserved.
//   [0xA4] f32 pad_rotation_3   — Always 0.0. Reserved.
//
// COLOR2 BLOCK (0xA8–0xB7, 16 bytes = 1 × RGBA float):
//   [0xA8] f32×4 color2         — Secondary RGBA color (usage context unclear;
//                                 may be emitter tint or billboard color).
//                                 Alpha always ≥ 0.6 across all client files.
//
// ACCELERATION BLOCK (0xB8–0xCF, 24 bytes):
//   [0xB8] f32 accel_x          — X acceleration (units/s²). Range 0..55.
//   [0xBC] f32 accel_y          — Y acceleration. Range 0..80.
//   [0xC0] f32 accel_z          — Z acceleration. Range 0..55.
//   [0xC4] f32 pad_accel        — Almost always 0.0 (99.9% of files). NOT read
//                                 by runtime. Reserved/padding.
//   [0xC8] f32 format_const_100 — Always exactly 100.0 in all 11,366 files.
//                                 NOT read by runtime (absent from FUN_01092450).
//                                 Likely a ForkParticle format version marker or
//                                 export constant.
//   [0xCC] f32 max_draw_dist    — Maximum render distance. Particles beyond this
//                                 distance from the camera are culled. Range
//                                 0..20000 game units; 8.6% zero (= no cull).
//                                 Stored verbatim: emitter+0xCC ← PSB+0xCC.
//
// SPIN BLOCK (0xD0–0xEB, 28 bytes):
//   [0xD0] f32 spin_start       — Initial spin angle offset (degrees). Range −1767..200.
//   [0xD4] f32 spin_min         — Minimum spin rate (degrees/s). Range 0..58.
//   [0xD8] f32 spin_max         — Maximum spin rate (degrees/s). Range −360..360.
//   [0xDC] f32 spin_var         — Spin rate variation (degrees/s). Range 0..360.
//   [0xE0] f32 spin_damp        — Spin damping or secondary spin parameter.
//                                 Stored in emitter+0x164 (FUN_01092450). Range
//                                 0..49; 30.5% zero. Exact semantic unclear.
//   [0xE4] f32 spin_speed       — Continuous spin speed (degrees/s). Range 0..6000.
//   [0xE8] u32 spin_flags       — Spin mode flags (low byte: 0..267 observed).
//                                 Low byte read in FUN_010cdbf0: sets emitter+0x180.
//
// BOUNDS BLOCK (0xEC–0x107, 28 bytes):
//   Six floats define an AABB (axis-aligned bounding box) for particle culling.
//   When min > max on any axis (sentinel ≈ ±100000), culling is disabled.
//   NOT read by FUN_01092450 or FUN_010cdbf0; likely used by frustum-cull code.
//   [0xEC] f32 bounds_min_x
//   [0xF0] f32 bounds_min_y
//   [0xF4] f32 bounds_min_z
//   [0xF8] f32 bounds_max_x
//   [0xFC] f32 bounds_max_y
//   [0x100] f32 bounds_max_z
//   [0x104] f32 pad_bounds      — Always 0.0. Reserved.
//
// METADATA BLOCK (0x108–0x19F):
//   [0x108] f32 emit_rate_final — Final emission rate or cutoff; range 0..60;
//                                 46.9% zero. Stored in emitter+0x17C.
//   [0x10C] u32 texture_blend_mode — Texture blending enum (0..6). Passed to
//                                 FUN_01092380 which maps: 1→add, 2→screen,
//                                 3→multiply, 4→subtract, 6→alpha/blend; others→none.
//   [0x110] f32 playback_scale  — Playback speed multiplier. Range 0.05..8.0;
//                                 NEVER zero in any client file. Stored in emitter+0x194.
//   [0x114] u32 loop_count      — Number of loops (0=play once, ≥1=repeat).
//                                 Range 0..8; 94.4% zero. Stored in emitter+0x198.
//   [0x118] u32 file_total_size — Total PSB file size in bytes. Verified exact match
//                                 against actual file sizes for all tested files.
//                                 Stored in emitter+0x19C.
//   [0x11C] u32 emitter_params_size — Size of emitter parameter struct in ForkParticle
//                                 Designer. Almost always 412 (0x19C); a few older
//                                 files have 0. Constant across all game-ready files.
//   [0x120] u32 data_block_size — Always 420 = data_size. Mirrors header+0x04.
//   [0x124] u32 num_textures    — Number of texture entries in the texture array.
//                                 Range 1..32 (NEVER zero). Stored in emitter+0x1A0.
//   [0x128] u32 runtime_ptr_A   — Heap pointer set by ForkParticle Designer at
//                                 export time; overwritten by engine at load.
//                                 Not a file offset — values vary per export run.
//   [0x12C] u32 texture_data_offset — File offset to the texture array. At runtime,
//                                 patched to a memory pointer by the engine.
//                                 Stored in emitter+0x1A8. Range: 420..50924.
//                                 Texture array: num_textures entries × 64 bytes.
//                                 Each entry: u32 type (1=texture path), then a
//                                 null-terminated path string (up to 60 bytes).
//
//   [0x130] u32 flag_extra_130  — 0 or 1 across all client files; purpose unclear.
//   [0x134] u32 extra_size_134  — Usually equals file_total_size (matches for 91% of
//                                 11,366 files). Likely a validation copy or secondary
//                                 size field written by ForkParticle Designer.
//   [0x138] u32 anim_data_offset — File offset to animation frame data. 0 = static
//                                 texture. Checked in FUN_010cdbf0/FUN_01092880:
//                                 enables animated texture when non-zero. Patched to
//                                 a memory pointer at runtime.
//   [0x13C] u32 texture_base_offset — Always 832 (0x340) in all 11,366 client files.
//                                 Points to the same region as texture_data_offset in
//                                 static files; likely the texture/animation base struct
//                                 offset. Runtime-patched. Stored in emitter+0x1D0 when
//                                 animation is active (FUN_01092880 @ 01092880).
//   [0x140–0x183]: 68 bytes, always 0.0 in all client files. Runtime-initialized
//                                 emitter state (overwritten by engine at load).
//                                 FUN_010cdbf0 reads PSB+0x170-0x17C to set
//                                 emitter+0x1AC-0x1B8 but all values are zero here.
//   [0x184] u32                 — Always equals file_total_size. Second copy written
//                                 by ForkParticle Designer; not read by game runtime.
//   [0x188] f32 scale_188       — Copied to emitter+0x1C0 (FUN_01092450). Range
//                                 0.0..1.0 across clients. Purpose not identified.
//   [0x18C] f32 scale_18c       — Copied to emitter+0x1C4. Range 0.0..3.4.
//   [0x190] f32 scale_190       — Copied to emitter+0x1BC. Range 2.2..28.4.
//   [0x194] u32 emitter_name_present — 0 = no name; non-zero = emitter has a name.
//   [0x198] u32 emitter_name_offset  — File offset to null-terminated emitter name
//                                 string (patched to pointer at runtime). Stored
//                                 in emitter+0x1EC when emitter_name_present ≠ 0.
//
// TEXTURE ARRAY (at file offset = texture_data_offset):
//   num_textures entries, each 64 bytes:
//   [+0x00] u32 type            — Entry type: 1 = texture path
//   [+0x04] char[60] path       — Null-terminated texture path string
//
// The remaining bytes after the texture array hold animation frames, name strings,
// and other variable-length data referenced by the offset fields above.

struct PsbError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct PsbColor {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
};

struct PsbFile {
    // ── HEADER (0x00–0x0F) ─────────────────────────────────────────────────
    uint32_t header_size    = 0;  // Always 80
    uint32_t data_size      = 0;  // Always 420
    uint32_t particle_id    = 0;
    uint32_t section_offset = 0;  // Always 112

    // ── COLOR BLOCK (0x10–0x4F) ────────────────────────────────────────────
    PsbColor start_color;
    PsbColor middle_color;
    PsbColor end_color;
    PsbColor birth_color;  // 0x40: ACTUALLY death_color (used at END of life in phase 3)

    // ── TIMING / COLOR PHASE BLOCK (0x50–0x6B) ──────────────────────────────
    // NOTE: Ghidra RE of FUN_0109bb30 revealed that 0x50 and 0x54 are
    // color transition thresholds, NOT timing fields. See docs/psb_field_mapping.md.
    float    birth_delay  = 0.0f;  // 0x50: ACTUALLY color_midpoint_1 (0-1 threshold for start→middle)
    float    life_min     = 0.0f;  // 0x54: ACTUALLY color_midpoint_2 (0-1 threshold for middle→end)
    float    life_max     = 0.0f;  // 0x58: particle_lifetime (seconds, all particles same)
    float    birth_rate   = 0.0f;  // 0x5C: particles per second
    float    death_delay  = 0.0f;  // 0x60: seconds after emission stops before emitter dies
    float    emit_period  = 0.0f;  // 0x64: emission cycle period (0=burst)
    uint32_t flags        = 0;     // 0x68: emitter flags (blend mode bits + behavior)

    // ── VELOCITY BLOCK (0x6C–0x84) ────────────────────────────────────────
    float emit_speed    = 0.0f;    // 0x6C: base emission speed
    float speed_x       = 0.0f;    // 0x70: velocity offset X
    float speed_y       = 0.0f;    // 0x74: velocity offset Y
    float speed_z       = 0.0f;    // 0x78: ACTUALLY size_transition (0-1 threshold for size phases)
    float gravity       = 0.0f;    // 0x7C: downward gravity
    float spread_angle  = 0.0f;
    float rotation_speed = 0.0f;  // 0x84: initial angular velocity (deg/s)

    // ── SIZE BLOCK (0x88–0x97) ─────────────────────────────────────────────
    float size_start = 0.0f;  // Scaled by engine constant before use
    float size_end   = 0.0f;
    float size_mult  = 1.0f;  // Written by exporter; NOT read by game runtime
    float size_alpha = 1.0f;  // Written by exporter; NOT read by game runtime

    // ── ROTATION BLOCK (0x98–0xA7) ─────────────────────────────────────────
    float initial_rotation = 0.0f;  // Degrees; engine converts to radians
    float pad_rotation_1 = 0.0f;   // 0x9C: always 0 in client files
    float pad_rotation_2 = 0.0f;   // 0xA0: always 0 in client files
    float pad_rotation_3 = 0.0f;   // 0xA4: always 0 in client files

    // ── COLOR2 BLOCK (0xA8–0xB7) ───────────────────────────────────────────
    PsbColor color2;

    // ── ACCELERATION BLOCK (0xB8–0xCF) ─────────────────────────────────────
    float accel_x          = 0.0f;
    float accel_y          = 0.0f;
    float accel_z          = 0.0f;
    float pad_accel        = 0.0f;   // 0xC4: almost always 0
    float format_const     = 100.0f; // 0xC8: always 100.0 in client files (format version marker)
    float max_draw_dist    = 0.0f;   // 0xCC

    // ── SPIN BLOCK (0xD0–0xEB) ─────────────────────────────────────────────
    float    spin_start  = 0.0f;
    float    spin_min    = 0.0f;
    float    spin_max    = 0.0f;
    float    spin_var    = 0.0f;
    float    spin_damp   = 0.0f;    // 0xE0: stored in emitter+0x164; exact use unclear
    float    spin_speed  = 0.0f;
    uint32_t spin_flags  = 0;

    // ── BOUNDS BLOCK (0xEC–0x107) ──────────────────────────────────────────
    // Axis-aligned bounding box for particle culling.
    // If bounds_min > bounds_max on any axis, culling is disabled (sentinel ≈ ±100000).
    std::array<float, 3> bounds_min = {};  // {min_x, min_y, min_z}
    std::array<float, 3> bounds_max = {};  // {max_x, max_y, max_z}
    float pad_bounds       = 0.0f;  // 0x104: always 0 in client files

    // ── METADATA BLOCK (0x108–0x19F) ───────────────────────────────────────
    float    emit_rate_final     = 0.0f;   // 0x108: ACTUALLY texture_cycle_rate (textures/sec over particle life)
    uint32_t texture_blend_mode  = 0;      // 0x10C: 0=none,1=add,2=screen,3=mul,4=sub,6=alpha
    float    playback_scale      = 1.0f;   // 0x110: never zero; default 1.0
    uint32_t loop_count          = 0;      // 0x114: 0=play once; ≥1=repeat N times
    uint32_t file_total_size     = 0;      // 0x118: matches actual file size
    uint32_t emitter_params_size = 0;      // 0x11C: always 412 in game-ready files
    uint32_t data_block_size    = 0;      // 0x120: always mirrors data_size (420)
    uint32_t num_textures        = 0;      // 0x124: 1..32
    uint32_t runtime_ptr_a       = 0;      // 0x128: heap pointer from Designer; overwritten at load
    uint32_t texture_data_offset = 0;      // 0x12C: file offset to texture array

    uint32_t flag_extra_130      = 0;      // 0x130: 0 or 1; full purpose unclear
    uint32_t extra_size_134     = 0;      // 0x134: usually equals file_total_size; validation copy
    uint32_t anim_data_offset   = 0;      // 0x138: file offset to animation frame data; 0=static
    uint32_t texture_base_offset = 0;     // 0x13C: always 832 in all client files; runtime-patched ptr

    // ── DESIGNER STATE BLOCK (0x140–0x183) ──────────────────────────────────
    // Partially populated by ForkParticle Designer; partially used by game runtime.
    float    designer_offset_x  = 0.0f;  // 0x140: spatial offset X (range -100..100)
    float    designer_offset_y  = 0.0f;  // 0x144: spatial offset Y
    float    designer_offset_z  = 0.0f;  // 0x148: spatial offset Z
    uint32_t pad_14c            = 0;     // 0x14C: always 0
    uint8_t  designer_state[16] = {};    // 0x150-0x15F: Designer string fragment / runtime state
    uint8_t  pad_160[16]        = {};    // 0x160-0x16F: always 0
    // Copied to emitter by FUN_010cdbf0:
    float    runtime_1ac = 0.0f;         // 0x170 → emitter+0x1AC
    float    runtime_1b0 = 0.0f;         // 0x174 → emitter+0x1B0
    float    runtime_1b4 = 0.0f;         // 0x178 → emitter+0x1B4
    float    runtime_1b8 = 0.0f;         // 0x17C → emitter+0x1B8
    uint32_t pad_180     = 0;            // 0x180: always 0

    uint32_t file_total_size_dup = 0;    // 0x184: always equals file_total_size

    // Three f32 values copied verbatim to emitter struct (FUN_01092450 @ 01092450):
    //   PSB+0x188 → emitter+0x1C0, PSB+0x18C → emitter+0x1C4, PSB+0x190 → emitter+0x1BC
    // Range: 0.0..28.4 across all client files. Purpose not further identified via RE.
    float    scale_188 = 0.0f;
    float    scale_18c = 0.0f;
    float    scale_190 = 0.0f;

    // 0x194: emitter_name_present (non-zero = has name) — read directly in FUN_010cdbf0.
    // 0x198: emitter_name_offset (file offset to name string; patched to ptr at runtime).
    // 0x19C: reserved zero (last u32 of the 420-byte data block).

    // Texture paths extracted from the texture array at texture_data_offset.
    // Paths are relative to the client res/ directory (e.g. "forkp/textures/dds/foo.dds").
    std::vector<std::string> textures;

    // Per-texture UV rectangles for sprite atlas lookup.
    // Each entry defines a sub-region of the texture page for one sprite variant.
    // Particles are randomly assigned a texture entry at birth.
    // Each rect is {u_min, v_min, u_max, v_max} in 0-1 normalized coords.
    struct UVRect { float u_min=0, v_min=0, u_max=1, v_max=1; };
    std::vector<UVRect> texture_uv_rects;

    // ── Animation Timeline (from struct at texture_base_offset, when anim_data_offset > 0) ──
    // ForkParticle animation timeline controller. 33% of client files have animation.
    // A single animation keyframe — contains full emitter parameter snapshot.
    // The frame block is: 5-u32 header + exact copy of PSB+0x10 through PSB+0x1A0
    // (the full emitter param block), then texture name + UV rect.
    // The runtime interpolates between keyframes over time.
    struct AnimKeyframe {
        // The emitter params in this keyframe (same layout as PSB main block 0x10-0x1A0)
        PsbColor start_color;
        PsbColor middle_color;
        PsbColor end_color;
        PsbColor birth_color;
        float birth_delay = 0, life_min = 0, life_max = 0, birth_rate = 0;
        float death_delay = 0, emit_period = 0;
        uint32_t flags = 0;
        float emit_speed = 0, speed_x = 0, speed_y = 0, speed_z = 0;
        float gravity = 0, spread_angle = 0, rotation_speed = 0;
        float size_start = 0, size_end = 0, size_mult = 0, size_alpha = 0;
        float initial_rotation = 0;
        float pad_rotation[3] = {};
        PsbColor color2;
    };

    struct AnimTimeline {
        std::string name;               // +0x00: char[32] timeline name
        float framerate = 60.0f;        // +0x24: always 60.0 (FPS)
        uint32_t frame_count = 0;       // +0x28: number of keyframes
        uint32_t data_block_offset = 0; // +0x2C: always 64
        uint32_t entry_size = 0;        // +0x3C: per-entry size
        std::vector<uint32_t> frame_offsets; // +0x44: per-frame offsets (relative to struct)
        std::vector<AnimKeyframe> keyframes; // parsed keyframe data
    };
    AnimTimeline anim_timeline;

    std::string emitter_name;  // From PSB+0x198 when PSB+0x194 ≠ 0; often empty
};
} // namespace lu::assets
