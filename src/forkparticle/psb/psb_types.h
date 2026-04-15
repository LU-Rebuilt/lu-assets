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
// REVERSE ENGINEERING SOURCES
//   Primary:   mediafx.exe CEmitterStatsDlg (FUN_0046f890) — maps every PSB
//              offset to its official ForkParticle UI label and field name.
//   Secondary: mediafx.exe PSB text serializer (FUN_004179b0) — writes every
//              field with its *TAG name (*PICOLOR, *PLIFEMIN, *EGRAVITY, etc.)
//   Tertiary:  legouniverse.exe FUN_01092450 — maps PSB fields to runtime
//              ForkParticle emitter struct (field-by-field).
//   Quaternary: legouniverse.exe FUN_010cdbf0 — emitter initializer.
//   Statistical: All 11,366 unpacked client PSBs analyzed.
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
//   [0x10] f32×4 initial_color   — *PICOLOR: Particle color at birth
//   [0x20] f32×4 trans_color_1   — *PTCOLOR1: Particle color at transition point 1
//   [0x30] f32×4 trans_color_2   — *PTCOLOR2: Particle color at transition point 2
//   [0x40] f32×4 final_color     — *PFCOLOR: Particle color at death
//
// PARTICLE PROPERTIES BLOCK (0x50–0x6B, 28 bytes):
//   [0x50] f32 color_ratio_1    — *PCOLORRATIO: Color life percentage 1 (0-1 threshold
//                                 for initial→transitional color blend). "Color life
//                                 percentage1" in MediaFX UI.
//   [0x54] f32 color_ratio_2    — *PCOLORRATIO2: Color life percentage 2 (0-1 threshold
//                                 for transitional→final color blend). "Color life
//                                 percentage2" in MediaFX UI.
//   [0x58] f32 life_min         — *PLIFEMIN: Minimum particle lifetime (seconds).
//                                 "Minimum particle life" in MediaFX UI.
//   [0x5C] f32 life_var         — *PLIFEVAR: Particle lifetime variance (seconds).
//                                 Actual lifetime = life_min + random(0, life_var).
//                                 "Life variance" in MediaFX UI.
//   [0x60] f32 vel_min          — *PVELMIN: Minimum initial velocity (units/s).
//                                 "Minimum initial velocity" in MediaFX UI.
//   [0x64] f32 vel_var          — *PVELVAR: Velocity variance. Actual velocity =
//                                 vel_min + random(0, vel_var). "Velocity variance"
//                                 in MediaFX UI.
//   [0x68] u32 flags            — *PFLAGS: Bit property flags. Low bits drive blend
//                                 mode (FUN_010cdbf0: bits 0x8/0x10/0x20/0x40/
//                                 0x80000/0x800000/0x1000000/0x2000000/0x4000000
//                                 map to blend mode values 1–9). "Bit property flags"
//                                 in MediaFX UI.
//
// SCALE BLOCK (0x6C–0x84, 25 bytes):
//   [0x6C] f32 initial_scale    — *PISCALE: Particle scale at birth. "Initial scale"
//                                 in MediaFX UI. Multiplied by engine constant before
//                                 use (FUN_01092450 → emitter+0x150).
//   [0x70] f32 trans_scale      — *PTSCALE: Particle scale at midlife transition.
//                                 "Transitional scale" in MediaFX UI.
//   [0x74] f32 final_scale      — *PFSCALE: Particle scale at death. "Final scale"
//                                 in MediaFX UI.
//   [0x78] f32 scale_ratio      — *PSCALERATIO: Scale life percentage (0-1 threshold
//                                 for scale phase transition). "Scale life percentage"
//                                 in MediaFX UI.
//
// ROTATION BLOCK (0x7C–0x84, 12 bytes):
//   [0x7C] f32 rot_min          — *PROTMIN: Minimum start rotation (degrees).
//                                 "Minimum rotation" / "Minimum start rotation" in
//                                 MediaFX UI. Converted to radians by engine:
//                                 stored as (π/180) × value in emitter+0x170.
//   [0x80] f32 rot_var          — *PROTVAR: Rotation speed variance (degrees/s).
//                                 "Rotation speed variance" in MediaFX UI.
//   [0x84] f32 drag             — *PDRAG: Drag coefficient variance.
//                                 "Drag coefficient variance" in MediaFX UI.
//                                 Stored in emitter+0x174 (FUN_01092450).
//
// SCALE VECTOR (0x88–0x97, 16 bytes):
//   [0x88] f32×4 scale          — *SCALE: 4-component scale vector (XYZW or
//                                 {min, ?, ?, ?}). "Minimum scale" in MediaFX UI
//                                 (size = 16 bytes = 4 floats). Written by ForkParticle
//                                 Designer but semantics of individual components are
//                                 unclear beyond the first being "minimum scale".
//
// ROTATION VECTOR (0x98–0xA7, 16 bytes):
//   [0x98] f32×4 rotation       — *ROTATION: 4-component rotation vector. May be
//                                 quaternion or Euler+W. Stored as (π/180) × value
//                                 in emitter+0x170 for first component. Components
//                                 2-4 always 0.0 in all 11,366 client files.
//
// TINT COLOR (0xA8–0xB7, 16 bytes):
//   [0xA8] f32×4 tint           — *TINT: Tint RGBA color applied to the particle.
//                                 "Tint color" in MediaFX UI. Alpha always ≥ 0.6
//                                 across all client files.
//
// SCALE VARIATION BLOCK (0xB8–0xC3, 12 bytes):
//   [0xB8] f32 iscale_var       — *ISCALEMIN: Initial scale variation / minimum.
//                                 "IScale variation" in MediaFX UI. Range 0..55.
//   [0xBC] f32 tscale_var       — *TSCALEMIN: Transitional scale variation / minimum.
//                                 "TScale variation" in MediaFX UI. Range 0..80.
//   [0xC0] f32 fscale_var       — *FSCALEMIN: Final scale variation / minimum.
//                                 "FScale variation" in MediaFX UI. Range 0..55.
//
// EMITTER PROPERTIES BLOCK (0xC4–0xEB, 40 bytes):
//   [0xC4] f32 sim_life         — *ESIMLIFE: Emitter simulation lifetime on create.
//                                 "Emitter simulation life on create" in MediaFX UI.
//                                 Almost always 0.0 (99.9% of files).
//   [0xC8] f32 emitter_life     — *ELIFE: Emitter lifetime (seconds). "Emitter Life"
//                                 in MediaFX UI. Previously misidentified as "always
//                                 100.0 format constant" — actually emitter lifetime
//                                 with default value of 100.0.
//   [0xCC] f32 emit_rate        — *ERATE: Emission rate (particles/second). "Emission
//                                 rate" in MediaFX UI. Range 0..20000.
//   [0xD0] f32 gravity          — *EGRAVITY: Downward gravity acceleration (units/s²).
//                                 "Gravity" in MediaFX UI.
//   [0xD4] f32 plane_w          — *EPLANEW: Emission volume plane width. "Plane-W" /
//                                 "Emission param 1" in MediaFX UI.
//   [0xD8] f32 plane_h          — *EPLANEH: Emission volume plane height. "Plane-H" /
//                                 "Emission param 2" in MediaFX UI.
//   [0xDC] f32 plane_d          — *EPLANED: Emission volume plane depth. "Plane-D" /
//                                 "Emission param 3" in MediaFX UI.
//   [0xE0] f32 cone_radius      — *ECONERAD: Emission cone radius. "Emission cone
//                                 radius" in MediaFX UI. Stored in emitter+0x164.
//   [0xE4] f32 max_particles    — *EMAXPARTICLE: Maximum active particles allowed.
//                                 "Maximum particles allowed" in MediaFX UI.
//   [0xE8] u32 volume_type      — *EVOLUME: Emitter volume type enum. "Emitter volume
//                                 type" in MediaFX UI. Values: 0=point, 1=box,
//                                 2=arc, 3=sphere, 4=cone, 5=cylinder (from MediaFX
//                                 UI volume type list).
//
// BOUNDS BLOCK (0xEC–0x107, 28 bytes):
//   [0xEC] f32×3 bounds_min     — *BOUNDINGBOXMIN: AABB min corner for culling.
//   [0xF8] f32×3 bounds_max     — *BOUNDINGBOXMAX: AABB max corner for culling.
//                                 When min > max on any axis (sentinel ≈ ±100000),
//                                 culling is disabled.
//   [0x104] f32 num_burst       — *NBURST: Number of particles on create (burst count).
//                                 "Number of particles on create" in MediaFX UI.
//                                 Stored as float in file but written with %d format
//                                 (integer semantic).
//
// METADATA BLOCK (0x108–0x19F):
//   [0x108] f32 anim_speed      — *ANMSPEED: Particle animation speed (textures/sec
//                                 for flipbook animation). "Particle animation speed"
//                                 in MediaFX UI. Stored in emitter+0x17C.
//   [0x10C] u32 blend_mode      — *EBLENDMODE: Texture blending enum. "Blend mode"
//                                 in MediaFX UI. Passed to FUN_01092380 which maps:
//                                 1→add, 2→screen, 3→multiply, 4→subtract, 6→alpha;
//                                 others→none.
//   [0x110] f32 time_delta_mult — *TDELTAMULT: Delta time multiplier / playback speed.
//                                 "Delta time multiplier" in MediaFX UI.
//                                 Range 0.05..8.0; NEVER zero. Stored in emitter+0x194.
//   [0x114] u32 num_point_forces — *NUMPOINTFORCES: Number of point forces attached
//                                 to this emitter. Force data follows at 0x118.
//   [0x118] u32 file_total_size — Total PSB file size in bytes. Verified exact match
//                                 against actual file sizes for all tested files.
//   [0x11C] u32 emitter_params_size — Size of emitter parameter struct in ForkParticle
//                                 Designer. Almost always 412 (0x19C).
//   [0x120] u32 data_block_size — Always 420 = data_size. Mirrors header+0x04.
//   [0x124] u32 num_assets      — *NUMASSETS: Number of texture/asset entries.
//                                 "Emitter texture assets" in MediaFX UI. Range 1..32.
//   [0x128] u32 runtime_ptr_a   — Heap pointer set by Designer; overwritten by engine.
//   [0x12C] u32 texture_data_offset — File offset to the texture array. Patched to
//                                 memory pointer by engine at load. Texture array:
//                                 num_assets entries × 64 bytes each.
//   [0x130] u32 num_emission_assets — *NUMEMISSIONASSETS: Number of emission assets.
//   [0x134] u32 extra_size_134  — Usually equals file_total_size (validation copy).
//   [0x138] u32 anim_data_offset — File offset to animation frame data. 0 = static
//                                 texture. Enables flipbook animation when non-zero.
//   [0x13C] u32 texture_base_offset — Always 832 (0x340) in all client files. Points
//                                 to animation/texture base struct offset.
//
//   [0x140–0x14B]: 12 bytes — *OFFSET: Emitter spatial offset XYZ.
//   [0x14C–0x183]: 56 bytes — Runtime state, designer strings, always 0 in client files.
//   [0x184] u32              — Always equals file_total_size. Validation copy.
//
//   [0x188] f32 path_dist_min   — "Minimum particle distance from path" in MediaFX UI.
//                                 Range 0.0..1.0. Copied to emitter+0x1C0.
//   [0x18C] f32 path_dist_var   — "Path particle distance variance" in MediaFX UI.
//                                 Range 0.0..3.4. Copied to emitter+0x1C4.
//   [0x190] f32 path_speed      — "Emitter speed on path" in MediaFX UI. Range
//                                 2.2..28.4. Copied to emitter+0x1BC.
//
//   [0x194] u32 emitter_name_present — 0 = no name; non-zero = has name.
//   [0x198] u32 emitter_name_offset  — File offset to null-terminated emitter name.
//
// TEXTURE ARRAY (at file offset = texture_data_offset):
//   num_assets entries, each 64 bytes:
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
    PsbColor initial_color;   // 0x10: *PICOLOR — particle color at birth
    PsbColor trans_color_1;   // 0x20: *PTCOLOR1 — transitional color 1
    PsbColor trans_color_2;   // 0x30: *PTCOLOR2 — transitional color 2
    PsbColor final_color;     // 0x40: *PFCOLOR — particle color at death

    // ── PARTICLE PROPERTIES (0x50–0x6B) ────────────────────────────────────
    float    color_ratio_1 = 0.0f;  // 0x50: *PCOLORRATIO — color life percentage 1 (0-1)
    float    color_ratio_2 = 0.0f;  // 0x54: *PCOLORRATIO2 — color life percentage 2 (0-1)
    float    life_min      = 0.0f;  // 0x58: *PLIFEMIN — minimum particle lifetime (seconds)
    float    life_var      = 0.0f;  // 0x5C: *PLIFEVAR — lifetime variance (seconds)
    float    vel_min       = 0.0f;  // 0x60: *PVELMIN — minimum initial velocity
    float    vel_var       = 0.0f;  // 0x64: *PVELVAR — velocity variance
    uint32_t flags         = 0;     // 0x68: *PFLAGS — bit property flags

    // ── SCALE PROPERTIES (0x6C–0x78) ──────────────────────────────────────
    float initial_scale = 0.0f;     // 0x6C: *PISCALE — particle scale at birth
    float trans_scale   = 0.0f;     // 0x70: *PTSCALE — particle scale at midlife
    float final_scale   = 0.0f;     // 0x74: *PFSCALE — particle scale at death
    float scale_ratio   = 0.0f;     // 0x78: *PSCALERATIO — scale life percentage (0-1)

    // ── ROTATION + DRAG (0x7C–0x84) ───────────────────────────────────────
    float rot_min = 0.0f;           // 0x7C: *PROTMIN — minimum start rotation (degrees)
    float rot_var = 0.0f;           // 0x80: *PROTVAR — rotation speed variance (degrees/s)
    float drag    = 0.0f;           // 0x84: *PDRAG — drag coefficient variance

    // ── SCALE VECTOR (0x88–0x97) ──────────────────────────────────────────
    // *SCALE: 4-component scale vector. First component is "Minimum scale" per MediaFX.
    std::array<float, 4> scale = {0.0f, 0.0f, 0.0f, 0.0f};  // 0x88

    // ── ROTATION VECTOR (0x98–0xA7) ───────────────────────────────────────
    // *ROTATION: 4-component rotation vector. Components 2-4 always 0.0 in client files.
    std::array<float, 4> rotation = {0.0f, 0.0f, 0.0f, 0.0f};  // 0x98

    // ── TINT COLOR (0xA8–0xB7) ────────────────────────────────────────────
    PsbColor tint;  // 0xA8: *TINT — tint color applied to particle

    // ── SCALE VARIATION (0xB8–0xC3) ──────────────────────────────────────
    float iscale_var = 0.0f;  // 0xB8: *ISCALEMIN — initial scale variation
    float tscale_var = 0.0f;  // 0xBC: *TSCALEMIN — transitional scale variation
    float fscale_var = 0.0f;  // 0xC0: *FSCALEMIN — final scale variation

    // ── EMITTER PROPERTIES (0xC4–0xEB) ────────────────────────────────────
    float    sim_life       = 0.0f;    // 0xC4: *ESIMLIFE — emitter simulation life on create
    float    emitter_life   = 100.0f;  // 0xC8: *ELIFE — emitter lifetime (default 100.0)
    float    emit_rate      = 0.0f;    // 0xCC: *ERATE — emission rate (particles/second)
    float    gravity        = 0.0f;    // 0xD0: *EGRAVITY — gravity acceleration
    float    plane_w        = 0.0f;    // 0xD4: *EPLANEW — emission plane width
    float    plane_h        = 0.0f;    // 0xD8: *EPLANEH — emission plane height
    float    plane_d        = 0.0f;    // 0xDC: *EPLANED — emission plane depth
    float    cone_radius    = 0.0f;    // 0xE0: *ECONERAD — emission cone radius
    float    max_particles  = 0.0f;    // 0xE4: *EMAXPARTICLE — max active particles
    uint32_t volume_type    = 0;       // 0xE8: *EVOLUME — emission volume type enum

    // ── BOUNDS BLOCK (0xEC–0x107) ──────────────────────────────────────────
    std::array<float, 3> bounds_min = {};  // 0xEC: *BOUNDINGBOXMIN
    std::array<float, 3> bounds_max = {};  // 0xF8: *BOUNDINGBOXMAX
    float num_burst = 0.0f;                // 0x104: *NBURST — particles on create (burst count)

    // ── METADATA BLOCK (0x108–0x19F) ───────────────────────────────────────
    float    anim_speed           = 0.0f;   // 0x108: *ANMSPEED — particle animation speed
    uint32_t blend_mode           = 0;      // 0x10C: *EBLENDMODE — 1=add,2=screen,3=mul,4=sub,6=alpha
    float    time_delta_mult      = 1.0f;   // 0x110: *TDELTAMULT — delta time multiplier
    uint32_t num_point_forces     = 0;      // 0x114: *NUMPOINTFORCES
    uint32_t file_total_size      = 0;      // 0x118: matches actual file size
    uint32_t emitter_params_size  = 0;      // 0x11C: always 412 in game-ready files
    uint32_t data_block_size      = 0;      // 0x120: always mirrors data_size (420)
    uint32_t num_assets           = 0;      // 0x124: *NUMASSETS — texture/asset count (1..32)
    uint32_t runtime_ptr_a        = 0;      // 0x128: heap pointer from Designer; overwritten
    uint32_t texture_data_offset  = 0;      // 0x12C: file offset to texture array

    uint32_t num_emission_assets  = 0;      // 0x130: *NUMEMISSIONASSETS
    uint32_t extra_size_134       = 0;      // 0x134: usually equals file_total_size
    uint32_t anim_data_offset     = 0;      // 0x138: file offset to animation data; 0=static
    uint32_t texture_base_offset  = 0;      // 0x13C: always 832 in all client files

    // ── DESIGNER STATE BLOCK (0x140–0x183) ──────────────────────────────────
    float    emitter_offset_x  = 0.0f;  // 0x140: *OFFSET X — emitter spatial offset
    float    emitter_offset_y  = 0.0f;  // 0x144: *OFFSET Y
    float    emitter_offset_z  = 0.0f;  // 0x148: *OFFSET Z
    uint32_t pad_14c           = 0;     // 0x14C: always 0
    uint8_t  designer_state[16] = {};   // 0x150-0x15F: Designer runtime state
    uint8_t  pad_160[16]        = {};   // 0x160-0x16F: always 0
    // Copied to emitter by FUN_010cdbf0 (always 0 in client files):
    float    runtime_1ac = 0.0f;        // 0x170 → emitter+0x1AC
    float    runtime_1b0 = 0.0f;        // 0x174 → emitter+0x1B0
    float    runtime_1b4 = 0.0f;        // 0x178 → emitter+0x1B4
    float    runtime_1b8 = 0.0f;        // 0x17C → emitter+0x1B8
    uint32_t pad_180     = 0;           // 0x180: always 0

    uint32_t file_total_size_dup = 0;   // 0x184: always equals file_total_size

    // Path emitter properties — controls particles distributed along a path
    float    path_dist_min = 0.0f;  // 0x188: minimum particle distance from path
    float    path_dist_var = 0.0f;  // 0x18C: path particle distance variance
    float    path_speed    = 0.0f;  // 0x190: emitter speed on path

    // Texture paths extracted from the texture array at texture_data_offset.
    // Paths are relative to the client res/ directory (e.g. "forkp/textures/dds/foo.dds").
    std::vector<std::string> textures;

    // Per-texture UV rectangles for sprite atlas lookup.
    struct UVRect { float u_min=0, v_min=0, u_max=1, v_max=1; };
    std::vector<UVRect> texture_uv_rects;

    // ── Animation Timeline (from struct at texture_base_offset, when anim_data_offset > 0) ──
    // ForkParticle animation timeline controller. 33% of client files have animation.
    struct AnimKeyframe {
        PsbColor initial_color;
        PsbColor trans_color_1;
        PsbColor trans_color_2;
        PsbColor final_color;
        float color_ratio_1 = 0, color_ratio_2 = 0, life_min = 0, life_var = 0;
        float vel_min = 0, vel_var = 0;
        uint32_t flags = 0;
        float initial_scale = 0, trans_scale = 0, final_scale = 0, scale_ratio = 0;
        float rot_min = 0, rot_var = 0, drag = 0;
        float scale_x = 0, scale_y = 0, scale_z = 0, scale_w = 0;
        float rotation_x = 0;
        float rotation_pad[3] = {};
        PsbColor tint;
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
