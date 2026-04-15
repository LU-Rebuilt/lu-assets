#include "forkparticle/psb/psb_reader.h"
#include "common/binary_reader/binary_reader.h"

#include <cstring>

namespace lu::assets {

// Texture entry in the texture array (at file offset = texture_data_offset).
// Each entry is 64 bytes: u32 type + char[60] path.
// Verified from FUN_010cdbf0 @ 010cdbf0 (legouniverse.exe):
//   loop iterates num_assets entries with stride 0x40;
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
    psb.initial_color = read_color(r);   // 0x10: *PICOLOR
    psb.trans_color_1 = read_color(r);   // 0x20: *PTCOLOR1
    psb.trans_color_2 = read_color(r);   // 0x30: *PTCOLOR2
    psb.final_color   = read_color(r);   // 0x40: *PFCOLOR

    // ── PARTICLE PROPERTIES (0x50–0x6B) ────────────────────────────────────
    psb.color_ratio_1 = r.read_f32();    // 0x50: *PCOLORRATIO
    psb.color_ratio_2 = r.read_f32();    // 0x54: *PCOLORRATIO2
    psb.life_min      = r.read_f32();    // 0x58: *PLIFEMIN
    psb.life_var      = r.read_f32();    // 0x5C: *PLIFEVAR
    psb.vel_min       = r.read_f32();    // 0x60: *PVELMIN
    psb.vel_var       = r.read_f32();    // 0x64: *PVELVAR
    psb.flags         = r.read_u32();    // 0x68: *PFLAGS

    // ── SCALE PROPERTIES (0x6C–0x78) ──────────────────────────────────────
    psb.initial_scale = r.read_f32();    // 0x6C: *PISCALE
    psb.trans_scale   = r.read_f32();    // 0x70: *PTSCALE
    psb.final_scale   = r.read_f32();    // 0x74: *PFSCALE
    psb.scale_ratio   = r.read_f32();    // 0x78: *PSCALERATIO

    // ── ROTATION + DRAG (0x7C–0x84) ───────────────────────────────────────
    psb.rot_min = r.read_f32();          // 0x7C: *PROTMIN
    psb.rot_var = r.read_f32();          // 0x80: *PROTVAR
    psb.drag    = r.read_f32();          // 0x84: *PDRAG

    // ── SCALE VECTOR (0x88–0x97) ──────────────────────────────────────────
    // *SCALE: 4 floats. First two multiplied by engine constant (FUN_01092450).
    psb.scale[0] = r.read_f32();         // 0x88
    psb.scale[1] = r.read_f32();         // 0x8C
    psb.scale[2] = r.read_f32();         // 0x90: NOT used by runtime
    psb.scale[3] = r.read_f32();         // 0x94: NOT used by runtime

    // ── ROTATION VECTOR (0x98–0xA7) ───────────────────────────────────────
    // *ROTATION: 4 floats. Only [0] used, converted deg→rad by engine.
    psb.rotation[0] = r.read_f32();      // 0x98
    psb.rotation[1] = r.read_f32();      // 0x9C: always 0 in client files
    psb.rotation[2] = r.read_f32();      // 0xA0: always 0
    psb.rotation[3] = r.read_f32();      // 0xA4: always 0

    // ── TINT COLOR (0xA8–0xB7) ────────────────────────────────────────────
    psb.tint = read_color(r);            // 0xA8: *TINT

    // ── SCALE VARIATION (0xB8–0xC3) ──────────────────────────────────────
    psb.iscale_var = r.read_f32();       // 0xB8: *ISCALEMIN
    psb.tscale_var = r.read_f32();       // 0xBC: *TSCALEMIN
    psb.fscale_var = r.read_f32();       // 0xC0: *FSCALEMIN

    // ── EMITTER PROPERTIES (0xC4–0xEB) ────────────────────────────────────
    psb.sim_life      = r.read_f32();    // 0xC4: *ESIMLIFE (designer-only, not read by LU)
    psb.emitter_life  = r.read_f32();    // 0xC8: *ELIFE (designer-only, not read by LU)
    psb.emit_rate     = r.read_f32();    // 0xCC: *ERATE
    psb.gravity       = r.read_f32();    // 0xD0: *EGRAVITY
    psb.plane_w       = r.read_f32();    // 0xD4: *EPLANEW
    psb.plane_h       = r.read_f32();    // 0xD8: *EPLANEH
    psb.plane_d       = r.read_f32();    // 0xDC: *EPLANED
    psb.cone_radius   = r.read_f32();    // 0xE0: *ECONERAD
    psb.max_particles = r.read_f32();    // 0xE4: *EMAXPARTICLE
    psb.volume_type   = r.read_u32();    // 0xE8: *EVOLUME

    // ── BOUNDS BLOCK (0xEC–0x107) ──────────────────────────────────────────
    psb.bounds_min[0] = r.read_f32();    // 0xEC
    psb.bounds_min[1] = r.read_f32();    // 0xF0
    psb.bounds_min[2] = r.read_f32();    // 0xF4
    psb.bounds_max[0] = r.read_f32();    // 0xF8
    psb.bounds_max[1] = r.read_f32();    // 0xFC
    psb.bounds_max[2] = r.read_f32();    // 0x100
    psb.num_burst     = r.read_f32();    // 0x104: *NBURST

    // ── METADATA BLOCK (0x108–0x13F) ───────────────────────────────────────
    psb.anim_speed         = r.read_f32();   // 0x108: *ANMSPEED
    psb.blend_mode         = r.read_u32();   // 0x10C: *EBLENDMODE
    psb.time_delta_mult    = r.read_f32();   // 0x110: *TDELTAMULT
    psb.num_point_forces   = r.read_u32();   // 0x114: *NUMPOINTFORCES
    psb.file_total_size    = r.read_u32();   // 0x118
    psb.emitter_params_size = r.read_u32();  // 0x11C
    psb.data_block_size    = r.read_u32();   // 0x120
    psb.num_assets         = r.read_u32();   // 0x124: *NUMASSETS
    psb.runtime_ptr_a      = r.read_u32();   // 0x128
    psb.texture_data_offset = r.read_u32();  // 0x12C

    psb.num_emission_assets = r.read_u32();  // 0x130: *NUMEMISSIONASSETS
    psb.extra_size_134      = r.read_u32();  // 0x134
    psb.anim_data_offset    = r.read_u32();  // 0x138
    psb.texture_base_offset = r.read_u32();  // 0x13C

    // 0x140-0x183: Designer state block
    psb.emitter_offset_x = r.read_f32();    // 0x140: *OFFSET X
    psb.emitter_offset_y = r.read_f32();    // 0x144: *OFFSET Y
    psb.emitter_offset_z = r.read_f32();    // 0x148: *OFFSET Z
    psb.pad_14c = r.read_u32();             // 0x14C: always 0
    std::memcpy(psb.designer_state, data.data() + r.pos(), 16); r.skip(16); // 0x150-0x15F
    std::memcpy(psb.pad_160, data.data() + r.pos(), 16); r.skip(16);       // 0x160-0x16F
    psb.runtime_1ac = r.read_f32();         // 0x170
    psb.runtime_1b0 = r.read_f32();         // 0x174
    psb.runtime_1b4 = r.read_f32();         // 0x178
    psb.runtime_1b8 = r.read_f32();         // 0x17C
    psb.pad_180 = r.read_u32();             // 0x180

    psb.file_total_size_dup = r.read_u32(); // 0x184

    psb.path_dist_min = r.read_f32();       // 0x188
    psb.path_dist_var = r.read_f32();       // 0x18C
    psb.path_speed    = r.read_f32();       // 0x190

    // Extract texture paths from the texture array.
    if (psb.num_assets > 0 && psb.texture_data_offset < data.size()) {
        size_t entry_base = psb.texture_data_offset;
        for (uint32_t i = 0; i < psb.num_assets; ++i) {
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

        // Parse keyframe data
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
                if (!at.keyframes.empty())
                    at.keyframes.push_back(at.keyframes[0]);
                else
                    at.keyframes.push_back({});
                continue;
            }

            // Params start at frame_offset + 20 (skip 5-u32 header)
            size_t p = base + at.frame_offsets[i] + 20;
            PsbFile::AnimKeyframe kf;

            kf.initial_color = readColor(p);       p += 16; // 0x10
            kf.trans_color_1 = readColor(p);       p += 16; // 0x20
            kf.trans_color_2 = readColor(p);       p += 16; // 0x30
            kf.final_color   = readColor(p);       p += 16; // 0x40
            kf.color_ratio_1 = readF32(p);         p += 4;  // 0x50
            kf.color_ratio_2 = readF32(p);         p += 4;  // 0x54
            kf.life_min      = readF32(p);         p += 4;  // 0x58
            kf.life_var      = readF32(p);         p += 4;  // 0x5C
            kf.vel_min       = readF32(p);         p += 4;  // 0x60
            kf.vel_var       = readF32(p);         p += 4;  // 0x64
            kf.flags         = readU32(p);         p += 4;  // 0x68
            kf.initial_scale = readF32(p);         p += 4;  // 0x6C
            kf.trans_scale   = readF32(p);         p += 4;  // 0x70
            kf.final_scale   = readF32(p);         p += 4;  // 0x74
            kf.scale_ratio   = readF32(p);         p += 4;  // 0x78
            kf.rot_min       = readF32(p);         p += 4;  // 0x7C
            kf.rot_var       = readF32(p);         p += 4;  // 0x80
            kf.drag          = readF32(p);         p += 4;  // 0x84
            kf.scale_x       = readF32(p);         p += 4;  // 0x88
            kf.scale_y       = readF32(p);         p += 4;  // 0x8C
            kf.scale_z       = readF32(p);         p += 4;  // 0x90
            kf.scale_w       = readF32(p);         p += 4;  // 0x94
            kf.rotation_x    = readF32(p);         p += 4;  // 0x98
            kf.rotation_pad[0] = readF32(p);       p += 4;
            kf.rotation_pad[1] = readF32(p);       p += 4;
            kf.rotation_pad[2] = readF32(p);       p += 4;  // 0xA4
            kf.tint          = readColor(p);                 // 0xA8

            at.keyframes.push_back(std::move(kf));
        }
    }

    // Extract per-texture UV rects from the texture array entries.
    if (psb.num_assets > 0 && psb.texture_data_offset > 0) {
        auto is_valid_uv = [](float u0, float v0, float u1, float v1) {
            return u0 >= -0.01f && u0 < 1.01f && v0 >= -0.01f && v0 < 1.01f &&
                   u1 > 0 && u1 <= 1.01f && v1 > 0 && v1 <= 1.01f &&
                   u1 > u0 && v1 > v0 &&
                   (u1 - u0) > 0.01f && (v1 - v0) > 0.01f;
        };

        for (uint32_t i = 0; i < psb.num_assets; ++i) {
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
