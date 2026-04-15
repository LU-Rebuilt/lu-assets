#include "forkparticle/psb/psb_writer.h"

#include <cstring>

namespace lu::assets {

namespace {

void write_u32(std::vector<uint8_t>& out, uint32_t v) {
    uint8_t buf[4];
    std::memcpy(buf, &v, 4);
    out.insert(out.end(), buf, buf + 4);
}

void write_f32(std::vector<uint8_t>& out, float v) {
    uint8_t buf[4];
    std::memcpy(buf, &v, 4);
    out.insert(out.end(), buf, buf + 4);
}

void write_color(std::vector<uint8_t>& out, const PsbColor& c) {
    write_f32(out, c.r);
    write_f32(out, c.g);
    write_f32(out, c.b);
    write_f32(out, c.a);
}

} // anonymous namespace

std::vector<uint8_t> psb_write(const PsbFile& psb,
                                std::span<const uint8_t> original_data) {
    std::vector<uint8_t> out;
    out.reserve(original_data.empty() ? 420 : original_data.size());

    // ── HEADER (0x00–0x0F) ─────────────────────────────────────────────────
    write_u32(out, psb.header_size);        // 0x00
    write_u32(out, psb.data_size);          // 0x04
    write_u32(out, psb.particle_id);        // 0x08
    write_u32(out, psb.section_offset);     // 0x0C

    // ── COLOR BLOCK (0x10–0x4F) ────────────────────────────────────────────
    write_color(out, psb.initial_color);    // 0x10: *PICOLOR
    write_color(out, psb.trans_color_1);    // 0x20: *PTCOLOR1
    write_color(out, psb.trans_color_2);    // 0x30: *PTCOLOR2
    write_color(out, psb.final_color);      // 0x40: *PFCOLOR

    // ── PARTICLE PROPERTIES (0x50–0x6B) ────────────────────────────────────
    write_f32(out, psb.color_ratio_1);      // 0x50: *PCOLORRATIO
    write_f32(out, psb.color_ratio_2);      // 0x54: *PCOLORRATIO2
    write_f32(out, psb.life_min);           // 0x58: *PLIFEMIN
    write_f32(out, psb.life_var);           // 0x5C: *PLIFEVAR
    write_f32(out, psb.vel_min);            // 0x60: *PVELMIN
    write_f32(out, psb.vel_var);            // 0x64: *PVELVAR
    write_u32(out, psb.flags);              // 0x68: *PFLAGS

    // ── SCALE PROPERTIES (0x6C–0x78) ──────────────────────────────────────
    write_f32(out, psb.initial_scale);      // 0x6C: *PISCALE
    write_f32(out, psb.trans_scale);        // 0x70: *PTSCALE
    write_f32(out, psb.final_scale);        // 0x74: *PFSCALE
    write_f32(out, psb.scale_ratio);        // 0x78: *PSCALERATIO

    // ── ROTATION + DRAG (0x7C–0x84) ───────────────────────────────────────
    write_f32(out, psb.rot_min);            // 0x7C: *PROTMIN
    write_f32(out, psb.rot_var);            // 0x80: *PROTVAR
    write_f32(out, psb.drag);               // 0x84: *PDRAG

    // ── SCALE VECTOR (0x88–0x97) ──────────────────────────────────────────
    write_f32(out, psb.scale[0]);           // 0x88: *SCALE
    write_f32(out, psb.scale[1]);           // 0x8C
    write_f32(out, psb.scale[2]);           // 0x90
    write_f32(out, psb.scale[3]);           // 0x94

    // ── ROTATION VECTOR (0x98–0xA7) ───────────────────────────────────────
    write_f32(out, psb.rotation[0]);        // 0x98: *ROTATION
    write_f32(out, psb.rotation[1]);        // 0x9C
    write_f32(out, psb.rotation[2]);        // 0xA0
    write_f32(out, psb.rotation[3]);        // 0xA4

    // ── TINT COLOR (0xA8–0xB7) ────────────────────────────────────────────
    write_color(out, psb.tint);             // 0xA8: *TINT

    // ── SCALE VARIATION (0xB8–0xC3) ──────────────────────────────────────
    write_f32(out, psb.iscale_var);         // 0xB8: *ISCALEMIN
    write_f32(out, psb.tscale_var);         // 0xBC: *TSCALEMIN
    write_f32(out, psb.fscale_var);         // 0xC0: *FSCALEMIN

    // ── EMITTER PROPERTIES (0xC4–0xEB) ────────────────────────────────────
    write_f32(out, psb.sim_life);           // 0xC4: *ESIMLIFE
    write_f32(out, psb.emitter_life);       // 0xC8: *ELIFE
    write_f32(out, psb.emit_rate);          // 0xCC: *ERATE
    write_f32(out, psb.gravity);            // 0xD0: *EGRAVITY
    write_f32(out, psb.plane_w);            // 0xD4: *EPLANEW
    write_f32(out, psb.plane_h);            // 0xD8: *EPLANEH
    write_f32(out, psb.plane_d);            // 0xDC: *EPLANED
    write_f32(out, psb.cone_radius);        // 0xE0: *ECONERAD
    write_f32(out, psb.max_particles);      // 0xE4: *EMAXPARTICLE
    write_u32(out, psb.volume_type);        // 0xE8: *EVOLUME

    // ── BOUNDS BLOCK (0xEC–0x107) ──────────────────────────────────────────
    write_f32(out, psb.bounds_min[0]);      // 0xEC
    write_f32(out, psb.bounds_min[1]);      // 0xF0
    write_f32(out, psb.bounds_min[2]);      // 0xF4
    write_f32(out, psb.bounds_max[0]);      // 0xF8
    write_f32(out, psb.bounds_max[1]);      // 0xFC
    write_f32(out, psb.bounds_max[2]);      // 0x100
    write_f32(out, psb.num_burst);          // 0x104: *NBURST

    // ── METADATA BLOCK (0x108–0x13F) ───────────────────────────────────────
    write_f32(out, psb.anim_speed);         // 0x108: *ANMSPEED
    write_u32(out, psb.blend_mode);         // 0x10C: *EBLENDMODE
    write_f32(out, psb.time_delta_mult);    // 0x110: *TDELTAMULT
    write_u32(out, psb.num_point_forces);   // 0x114: *NUMPOINTFORCES
    write_u32(out, psb.file_total_size);    // 0x118
    write_u32(out, psb.emitter_params_size);// 0x11C
    write_u32(out, psb.data_block_size);    // 0x120
    write_u32(out, psb.num_assets);         // 0x124: *NUMASSETS
    write_u32(out, psb.runtime_ptr_a);      // 0x128
    write_u32(out, psb.texture_data_offset);// 0x12C
    write_u32(out, psb.num_emission_assets);// 0x130: *NUMEMISSIONASSETS
    write_u32(out, psb.extra_size_134);     // 0x134
    write_u32(out, psb.anim_data_offset);   // 0x138
    write_u32(out, psb.texture_base_offset);// 0x13C

    // ── DESIGNER STATE BLOCK (0x140–0x183) ─────────────────────────────────
    write_f32(out, psb.emitter_offset_x);   // 0x140: *OFFSET X
    write_f32(out, psb.emitter_offset_y);   // 0x144: *OFFSET Y
    write_f32(out, psb.emitter_offset_z);   // 0x148: *OFFSET Z
    write_u32(out, psb.pad_14c);            // 0x14C
    out.insert(out.end(), psb.designer_state, psb.designer_state + 16); // 0x150
    out.insert(out.end(), psb.pad_160, psb.pad_160 + 16);              // 0x160
    write_f32(out, psb.runtime_1ac);        // 0x170
    write_f32(out, psb.runtime_1b0);        // 0x174
    write_f32(out, psb.runtime_1b4);        // 0x178
    write_f32(out, psb.runtime_1b8);        // 0x17C
    write_u32(out, psb.pad_180);            // 0x180

    write_u32(out, psb.file_total_size_dup);// 0x184

    // ── PATH PROPERTIES (0x188–0x190) ──────────────────────────────────────
    write_f32(out, psb.path_dist_min);      // 0x188
    write_f32(out, psb.path_dist_var);      // 0x18C
    write_f32(out, psb.path_speed);         // 0x190

    // 0x194: emitter_name_present, 0x198: emitter_name_offset, 0x19C: reserved
    if (original_data.size() >= 0x1A0) {
        out.insert(out.end(), original_data.begin() + 0x194,
                              original_data.begin() + 0x1A4);
    } else {
        for (int i = 0; i < 16; ++i) out.push_back(0);
    }

    // Everything after 0x1A4 (texture array, animation data, name string)
    if (original_data.size() > 0x1A4) {
        out.insert(out.end(), original_data.begin() + 0x1A4,
                              original_data.end());
    }

    return out;
}

} // namespace lu::assets
