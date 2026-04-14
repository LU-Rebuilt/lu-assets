#include "forkparticle/psb/psb_reader.h"
#include "common/binary_reader/binary_reader.h"

#include <cstring>

namespace lu::assets {

// Texture entry in the texture array (at file offset = texture_data_offset).
// Each entry is 64 bytes: u32 type + char[60] path.
// Verified from FUN_010cdbf0 @ 010cdbf0 (legouniverse.exe):
//   loop iterates num_textures entries with stride 0x40;
//   entry[0] == 1 identifies a texture path entry;
//   entry[4..] is the null-terminated path string.
static constexpr size_t TEXTURE_ENTRY_STRIDE = 64;
static constexpr size_t TEXTURE_ENTRY_TYPE_OFFSET = 0;
static constexpr size_t TEXTURE_ENTRY_PATH_OFFSET = 4;
static constexpr size_t TEXTURE_ENTRY_PATH_MAX    = 60;

static constexpr uint32_t TEXTURE_ENTRY_TYPE_PATH = 1;

static PsbColor read_color(BinaryReader& r) {
    PsbColor c;
    c.r = r.read_f32();
    c.g = r.read_f32();
    c.b = r.read_f32();
    c.a = r.read_f32();
    return c;
}

PsbFile psb_parse(std::span<const uint8_t> data) {
    if (data.size() < 16) {
        throw PsbError("PSB: file too small for header");
    }

    BinaryReader r(data);
    PsbFile psb;

    // ── HEADER (0x00–0x0F) ─────────────────────────────────────────────────
    psb.header_size    = r.read_u32();
    psb.data_size      = r.read_u32();
    psb.particle_id    = r.read_u32();
    psb.section_offset = r.read_u32();

    if (data.size() < psb.data_size) {
        throw PsbError("PSB: file truncated (smaller than data_size)");
    }

    // ── COLOR BLOCK (0x10–0x4F) ────────────────────────────────────────────
    psb.start_color  = read_color(r);
    psb.middle_color = read_color(r);
    psb.end_color    = read_color(r);
    psb.birth_color  = read_color(r);

    // ── TIMING BLOCK (0x50–0x6B) ───────────────────────────────────────────
    psb.birth_delay  = r.read_f32();
    psb.life_min     = r.read_f32();
    psb.life_max     = r.read_f32();
    psb.birth_rate   = r.read_f32();
    psb.death_delay  = r.read_f32();
    psb.emit_period  = r.read_f32();  // 0x64 → emitter+0x13C
    psb.flags        = r.read_u32();

    // ── VELOCITY BLOCK (0x6C–0x87) ─────────────────────────────────────────
    psb.emit_speed    = r.read_f32();
    psb.speed_x       = r.read_f32();
    psb.speed_y       = r.read_f32();
    psb.speed_z       = r.read_f32();
    psb.gravity       = r.read_f32();
    psb.spread_angle  = r.read_f32();
    psb.rotation_speed = r.read_f32();  // 0x84 → emitter+0x174

    // ── SIZE BLOCK (0x88–0x97) ─────────────────────────────────────────────
    // Engine multiplies size_start/size_end by an internal constant before use
    // (FUN_01092450: *(float*)(emitter+0x150) = *(f*)(psb+0x88) * DOUBLE_015fcc48).
    psb.size_start = r.read_f32();
    psb.size_end   = r.read_f32();
    psb.size_mult  = r.read_f32();   // 0x90: written by exporter, NOT used at runtime
    psb.size_alpha = r.read_f32();   // 0x94: written by exporter, NOT used at runtime

    // ── ROTATION BLOCK (0x98–0xA7) ─────────────────────────────────────────
    // Engine converts initial_rotation from degrees to radians:
    //   emitter+0x170 = (π/180) × psb+0x98  (FUN_01092450)
    psb.initial_rotation = r.read_f32();
    psb.pad_rotation_1 = r.read_f32();  // 0x9C: always 0 in client files
    psb.pad_rotation_2 = r.read_f32();  // 0xA0: always 0
    psb.pad_rotation_3 = r.read_f32();  // 0xA4: always 0

    // ── COLOR2 BLOCK (0xA8–0xB7) ───────────────────────────────────────────
    psb.color2 = read_color(r);

    // ── ACCELERATION BLOCK (0xB8–0xCF) ─────────────────────────────────────
    psb.accel_x       = r.read_f32();
    psb.accel_y       = r.read_f32();
    psb.accel_z       = r.read_f32();
    psb.pad_accel     = r.read_f32();   // 0xC4: almost always 0
    psb.format_const  = r.read_f32();   // 0xC8: always 100.0 (format version marker)
    psb.max_draw_dist = r.read_f32();   // 0xCC: emitter+0xCC ← psb+0xCC

    // ── SPIN BLOCK (0xD0–0xEB) ─────────────────────────────────────────────
    psb.spin_start = r.read_f32();
    psb.spin_min   = r.read_f32();
    psb.spin_max   = r.read_f32();
    psb.spin_var   = r.read_f32();
    psb.spin_damp  = r.read_f32();   // 0xE0: emitter+0x164; exact use unclear
    psb.spin_speed = r.read_f32();
    psb.spin_flags = r.read_u32();   // 0xE8: low byte → emitter+0x180

    // ── BOUNDS BLOCK (0xEC–0x107) ──────────────────────────────────────────
    // Axis-aligned bounding box. Sentinel ≈ ±100000 disables per-axis culling.
    // NOT accessed by FUN_01092450 or FUN_010cdbf0 — likely read by culling code.
    psb.bounds_min[0] = r.read_f32();  // 0xEC: min_x
    psb.bounds_min[1] = r.read_f32();  // 0xF0: min_y
    psb.bounds_min[2] = r.read_f32();  // 0xF4: min_z
    psb.bounds_max[0] = r.read_f32();  // 0xF8: max_x
    psb.bounds_max[1] = r.read_f32();  // 0xFC: max_y
    psb.bounds_max[2] = r.read_f32();  // 0x100: max_z
    psb.pad_bounds = r.read_f32();  // 0x104: always 0 in client files

    // ── METADATA BLOCK (0x108–0x19F) ───────────────────────────────────────
    psb.emit_rate_final    = r.read_f32();   // 0x108 → emitter+0x17C
    psb.texture_blend_mode = r.read_u32();   // 0x10C → FUN_01092380 (blend enum 0..6)
    psb.playback_scale     = r.read_f32();   // 0x110 → emitter+0x194
    psb.loop_count         = r.read_u32();   // 0x114 → emitter+0x198
    psb.file_total_size    = r.read_u32();   // 0x118 → emitter+0x19C
    psb.emitter_params_size = r.read_u32();  // 0x11C: almost always 412
    psb.data_block_size = r.read_u32();       // 0x120: always mirrors data_size (420)
    psb.num_textures       = r.read_u32();   // 0x124 → emitter+0x1A0
    psb.runtime_ptr_a = r.read_u32();         // 0x128: heap pointer from Designer
    psb.texture_data_offset = r.read_u32();  // 0x12C → emitter+0x1A8

    psb.flag_extra_130      = r.read_u32();  // 0x130: 0 or 1; purpose unclear
    psb.extra_size_134      = r.read_u32();  // 0x134: usually = file_total_size
    psb.anim_data_offset    = r.read_u32();  // 0x138: file offset to anim data (0=static)
    psb.texture_base_offset = r.read_u32();  // 0x13C: always 832; runtime-patched ptr

    // 0x140-0x183: Designer state block
    psb.designer_offset_x = r.read_f32();   // 0x140
    psb.designer_offset_y = r.read_f32();   // 0x144
    psb.designer_offset_z = r.read_f32();   // 0x148
    psb.pad_14c = r.read_u32();             // 0x14C: always 0
    std::memcpy(psb.designer_state, data.data() + r.pos(), 16); r.skip(16); // 0x150-0x15F
    std::memcpy(psb.pad_160, data.data() + r.pos(), 16); r.skip(16);       // 0x160-0x16F
    psb.runtime_1ac = r.read_f32();         // 0x170 → emitter+0x1AC
    psb.runtime_1b0 = r.read_f32();         // 0x174 → emitter+0x1B0
    psb.runtime_1b4 = r.read_f32();         // 0x178 → emitter+0x1B4
    psb.runtime_1b8 = r.read_f32();         // 0x17C → emitter+0x1B8
    psb.pad_180 = r.read_u32();             // 0x180: always 0

    psb.file_total_size_dup = r.read_u32(); // 0x184: always equals file_total_size

    psb.scale_188 = r.read_f32();   // 0x188 → emitter+0x1C0
    psb.scale_18c = r.read_f32();   // 0x18C → emitter+0x1C4
    psb.scale_190 = r.read_f32();   // 0x190 → emitter+0x1BC

    // Extract texture paths from the texture array.
    // The game engine uses texture_data_offset (patched to a pointer at runtime)
    // with num_textures entries × 64 bytes (FUN_010cdbf0 @ 010cdbf0).
    if (psb.num_textures > 0 && psb.texture_data_offset < data.size()) {
        size_t entry_base = psb.texture_data_offset;
        for (uint32_t i = 0; i < psb.num_textures; ++i) {
            size_t entry = entry_base + i * TEXTURE_ENTRY_STRIDE;
            if (entry + TEXTURE_ENTRY_STRIDE > data.size()) break;

            uint32_t type;
            std::memcpy(&type, data.data() + entry + TEXTURE_ENTRY_TYPE_OFFSET, 4);
            if (type == TEXTURE_ENTRY_TYPE_PATH) {
                const char* path_ptr = reinterpret_cast<const char*>(
                    data.data() + entry + TEXTURE_ENTRY_PATH_OFFSET);
                size_t len = strnlen(path_ptr, TEXTURE_ENTRY_PATH_MAX);
                psb.textures.emplace_back(path_ptr, len);
            }
        }
    }

    // Extract animation timeline struct at texture_base_offset (when animation present).
    if (psb.anim_data_offset > 0 && psb.anim_data_offset < data.size() &&
        psb.texture_base_offset + 0x54 < data.size()) {
        auto& at = psb.anim_timeline;
        size_t base = psb.texture_base_offset;

        // +0x00: char[32] name
        const char* namePtr = reinterpret_cast<const char*>(data.data() + base);
        at.name = std::string(namePtr, strnlen(namePtr, 32));

        // +0x24: framerate, +0x28: frame_count, +0x2C: data_block_offset, +0x3C: entry_size
        std::memcpy(&at.framerate, data.data() + base + 0x24, 4);
        std::memcpy(&at.frame_count, data.data() + base + 0x28, 4);
        std::memcpy(&at.data_block_offset, data.data() + base + 0x2C, 4);
        std::memcpy(&at.entry_size, data.data() + base + 0x3C, 4);

        // +0x44: per-frame offsets (frame_count entries)
        for (uint32_t i = 0; i < at.frame_count && base + 0x44 + (i+1)*4 <= data.size(); ++i) {
            uint32_t off;
            std::memcpy(&off, data.data() + base + 0x44 + i * 4, 4);
            at.frame_offsets.push_back(off);
        }

        // Parse keyframe data. Each frame block has a 5-u32 header at the offset,
        // then the emitter parameters start at offset+20 (same layout as PSB+0x10).
        auto readColor = [&data](size_t off) -> PsbColor {
            PsbColor c;
            if (off + 16 <= data.size()) {
                std::memcpy(&c.r, data.data() + off, 4);
                std::memcpy(&c.g, data.data() + off + 4, 4);
                std::memcpy(&c.b, data.data() + off + 8, 4);
                std::memcpy(&c.a, data.data() + off + 12, 4);
            }
            return c;
        };
        auto readF32 = [&data](size_t off) -> float {
            float v = 0;
            if (off + 4 <= data.size()) std::memcpy(&v, data.data() + off, 4);
            return v;
        };
        auto readU32 = [&data](size_t off) -> uint32_t {
            uint32_t v = 0;
            if (off + 4 <= data.size()) std::memcpy(&v, data.data() + off, 4);
            return v;
        };

        for (uint32_t i = 0; i < at.frame_count; ++i) {
            if (i >= at.frame_offsets.size() || at.frame_offsets[i] == 0) {
                // Offset 0 = loop back to frame 0; skip
                if (!at.keyframes.empty())
                    at.keyframes.push_back(at.keyframes[0]);
                else
                    at.keyframes.push_back({});
                continue;
            }

            // Params start at frame_offset + 20 (skip 5-u32 header)
            size_t p = base + at.frame_offsets[i] + 20;
            PsbFile::AnimKeyframe kf;

            kf.start_color  = readColor(p);       p += 16; // +0x10
            kf.middle_color = readColor(p);        p += 16; // +0x20
            kf.end_color    = readColor(p);        p += 16; // +0x30
            kf.birth_color  = readColor(p);        p += 16; // +0x40
            kf.birth_delay  = readF32(p);          p += 4;  // +0x50
            kf.life_min     = readF32(p);          p += 4;
            kf.life_max     = readF32(p);          p += 4;
            kf.birth_rate   = readF32(p);          p += 4;
            kf.death_delay  = readF32(p);          p += 4;
            kf.emit_period  = readF32(p);          p += 4;
            kf.flags        = readU32(p);          p += 4;  // +0x68
            kf.emit_speed   = readF32(p);          p += 4;  // +0x6C
            kf.speed_x      = readF32(p);          p += 4;
            kf.speed_y      = readF32(p);          p += 4;
            kf.speed_z      = readF32(p);          p += 4;
            kf.gravity      = readF32(p);          p += 4;
            kf.spread_angle = readF32(p);          p += 4;
            kf.rotation_speed = readF32(p);        p += 4;  // +0x84
            kf.size_start   = readF32(p);          p += 4;  // +0x88
            kf.size_end     = readF32(p);          p += 4;
            kf.size_mult    = readF32(p);          p += 4;
            kf.size_alpha   = readF32(p);          p += 4;
            kf.initial_rotation = readF32(p);      p += 4;  // +0x98
            kf.pad_rotation[0] = readF32(p);       p += 4;
            kf.pad_rotation[1] = readF32(p);       p += 4;
            kf.pad_rotation[2] = readF32(p);       p += 4;  // +0xA4
            kf.color2       = readColor(p);                  // +0xA8

            at.keyframes.push_back(std::move(kf));
        }
    }

    // Extract per-texture UV rects from the texture array entries.
    // Each 64-byte texture entry has UV rect at +0x2C: f32 u_min, v_min, u_max, v_max.
    // This works for both static and animated files.
    if (psb.num_textures > 0 && psb.texture_data_offset > 0) {
        // Allow slight overshoot beyond 1.0 (texture bleeding/padding)
        auto is_valid_uv = [](float u0, float v0, float u1, float v1) {
            return u0 >= -0.01f && u0 < 1.01f && v0 >= -0.01f && v0 < 1.01f &&
                   u1 > 0 && u1 <= 1.01f && v1 > 0 && v1 <= 1.01f &&
                   u1 > u0 && v1 > v0 &&
                   (u1 - u0) > 0.01f && (v1 - v0) > 0.01f;
        };

        for (uint32_t i = 0; i < psb.num_textures; ++i) {
            size_t entry_off = psb.texture_data_offset + i * 64 + 0x2C;
            if (entry_off + 16 > data.size()) break;

            float v0, v1, v2, v3;
            std::memcpy(&v0, data.data() + entry_off, 4);
            std::memcpy(&v1, data.data() + entry_off + 4, 4);
            std::memcpy(&v2, data.data() + entry_off + 8, 4);
            std::memcpy(&v3, data.data() + entry_off + 12, 4);

            if (is_valid_uv(v0, v1, v2, v3)) {
                psb.texture_uv_rects.push_back({v0, v1, v2, v3});
            } else {
                psb.texture_uv_rects.push_back({0, 0, 1, 1});
            }
        }
    }

    // Extract emitter name from PSB+0x194/0x198.
    // 0x194 = emitter_name_present flag (non-zero = has name).
    // 0x198 = emitter_name_offset into file (patched to pointer at runtime).
    // Verified from FUN_010cdbf0 @ 010cdbf0: sets emitter+0x1EC when flag ≠ 0.
    if (data.size() > 0x198) {
        uint32_t name_flag, name_offset;
        std::memcpy(&name_flag,   data.data() + 0x194, 4);
        std::memcpy(&name_offset, data.data() + 0x198, 4);
        if (name_flag != 0 && name_offset > 0 && name_offset < data.size()) {
            const char* nptr = reinterpret_cast<const char*>(data.data() + name_offset);
            size_t max_len = data.size() - name_offset;
            psb.emitter_name = std::string(nptr, strnlen(nptr, max_len));
        }
    }

    return psb;
}

} // namespace lu::assets
