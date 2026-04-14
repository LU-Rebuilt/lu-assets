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
    write_color(out, psb.start_color);      // 0x10
    write_color(out, psb.middle_color);     // 0x20
    write_color(out, psb.end_color);        // 0x30
    write_color(out, psb.birth_color);      // 0x40

    // ── TIMING BLOCK (0x50–0x6B) ───────────────────────────────────────────
    write_f32(out, psb.birth_delay);        // 0x50
    write_f32(out, psb.life_min);           // 0x54
    write_f32(out, psb.life_max);           // 0x58
    write_f32(out, psb.birth_rate);         // 0x5C
    write_f32(out, psb.death_delay);        // 0x60
    write_f32(out, psb.emit_period);        // 0x64
    write_u32(out, psb.flags);              // 0x68

    // ── VELOCITY BLOCK (0x6C–0x87) ─────────────────────────────────────────
    write_f32(out, psb.emit_speed);         // 0x6C
    write_f32(out, psb.speed_x);            // 0x70
    write_f32(out, psb.speed_y);            // 0x74
    write_f32(out, psb.speed_z);            // 0x78
    write_f32(out, psb.gravity);            // 0x7C
    write_f32(out, psb.spread_angle);       // 0x80
    write_f32(out, psb.rotation_speed);     // 0x84

    // ── SIZE BLOCK (0x88–0x97) ─────────────────────────────────────────────
    write_f32(out, psb.size_start);         // 0x88
    write_f32(out, psb.size_end);           // 0x8C
    write_f32(out, psb.size_mult);          // 0x90
    write_f32(out, psb.size_alpha);         // 0x94

    // ── ROTATION BLOCK (0x98–0xA7) ─────────────────────────────────────────
    write_f32(out, psb.initial_rotation);   // 0x98
    write_f32(out, psb.pad_rotation_1);     // 0x9C
    write_f32(out, psb.pad_rotation_2);     // 0xA0
    write_f32(out, psb.pad_rotation_3);     // 0xA4

    // ── COLOR2 BLOCK (0xA8–0xB7) ───────────────────────────────────────────
    write_color(out, psb.color2);           // 0xA8

    // ── ACCELERATION BLOCK (0xB8–0xCF) ─────────────────────────────────────
    write_f32(out, psb.accel_x);            // 0xB8
    write_f32(out, psb.accel_y);            // 0xBC
    write_f32(out, psb.accel_z);            // 0xC0
    write_f32(out, psb.pad_accel);          // 0xC4
    write_f32(out, psb.format_const);       // 0xC8
    write_f32(out, psb.max_draw_dist);      // 0xCC

    // ── SPIN BLOCK (0xD0–0xEB) ─────────────────────────────────────────────
    write_f32(out, psb.spin_start);         // 0xD0
    write_f32(out, psb.spin_min);           // 0xD4
    write_f32(out, psb.spin_max);           // 0xD8
    write_f32(out, psb.spin_var);           // 0xDC
    write_f32(out, psb.spin_damp);          // 0xE0
    write_f32(out, psb.spin_speed);         // 0xE4
    write_u32(out, psb.spin_flags);         // 0xE8

    // ── BOUNDS BLOCK (0xEC–0x107) ──────────────────────────────────────────
    write_f32(out, psb.bounds_min[0]);      // 0xEC
    write_f32(out, psb.bounds_min[1]);      // 0xF0
    write_f32(out, psb.bounds_min[2]);      // 0xF4
    write_f32(out, psb.bounds_max[0]);      // 0xF8
    write_f32(out, psb.bounds_max[1]);      // 0xFC
    write_f32(out, psb.bounds_max[2]);      // 0x100
    write_f32(out, psb.pad_bounds);         // 0x104

    // ── METADATA BLOCK (0x108–0x13F) ───────────────────────────────────────
    write_f32(out, psb.emit_rate_final);    // 0x108
    write_u32(out, psb.texture_blend_mode); // 0x10C
    write_f32(out, psb.playback_scale);     // 0x110
    write_u32(out, psb.loop_count);         // 0x114
    write_u32(out, psb.file_total_size);    // 0x118
    write_u32(out, psb.emitter_params_size);// 0x11C
    write_u32(out, psb.data_block_size);    // 0x120
    write_u32(out, psb.num_textures);       // 0x124
    write_u32(out, psb.runtime_ptr_a);      // 0x128
    write_u32(out, psb.texture_data_offset);// 0x12C
    write_u32(out, psb.flag_extra_130);     // 0x130
    write_u32(out, psb.extra_size_134);     // 0x134
    write_u32(out, psb.anim_data_offset);   // 0x138
    write_u32(out, psb.texture_base_offset);// 0x13C

    // ── DESIGNER STATE BLOCK (0x140–0x183) ─────────────────────────────────
    write_f32(out, psb.designer_offset_x);  // 0x140
    write_f32(out, psb.designer_offset_y);  // 0x144
    write_f32(out, psb.designer_offset_z);  // 0x148
    write_u32(out, psb.pad_14c);            // 0x14C
    out.insert(out.end(), psb.designer_state, psb.designer_state + 16); // 0x150
    out.insert(out.end(), psb.pad_160, psb.pad_160 + 16);              // 0x160
    write_f32(out, psb.runtime_1ac);        // 0x170
    write_f32(out, psb.runtime_1b0);        // 0x174
    write_f32(out, psb.runtime_1b4);        // 0x178
    write_f32(out, psb.runtime_1b8);        // 0x17C
    write_u32(out, psb.pad_180);            // 0x180

    write_u32(out, psb.file_total_size_dup);// 0x184

    // ── TAIL FIELDS (0x188–0x19F) ──────────────────────────────────────────
    write_f32(out, psb.scale_188);          // 0x188
    write_f32(out, psb.scale_18c);          // 0x18C
    write_f32(out, psb.scale_190);          // 0x190

    // 0x194: emitter_name_present, 0x198: emitter_name_offset, 0x19C: reserved
    // These are in the original data after the main block — copy from original
    // to preserve name offset and reserved field.
    if (original_data.size() >= 0x1A0) {
        out.insert(out.end(), original_data.begin() + 0x194,
                              original_data.begin() + 0x1A4);
    } else {
        // No original data — write zeros
        for (int i = 0; i < 16; ++i) out.push_back(0);
    }

    // Everything after 0x1A4 (texture array, animation data, name string)
    // is copied verbatim from the original file.
    if (original_data.size() > 0x1A4) {
        out.insert(out.end(), original_data.begin() + 0x1A4,
                              original_data.end());
    }

    return out;
}

} // namespace lu::assets
