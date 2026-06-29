#include "netdevil/zone/lvl/lvl_writer.h"
#include "netdevil/common/ldf/ldf_writer.h"
#include "common/binary_writer/binary_writer.h"

namespace lu::assets {

static void write_vec3(BinaryWriter& w, const Vec3& v) {
    w.write_f32(v.x);
    w.write_f32(v.y);
    w.write_f32(v.z);
}

static void write_quat_wxyz(BinaryWriter& w, const Quat& q) {
    w.write_f32(q.w);
    w.write_f32(q.x);
    w.write_f32(q.y);
    w.write_f32(q.z);
}

static void write_object_config(BinaryWriter& w, const std::vector<LdfEntry>& config) {
    if (config.empty()) {
        w.write_u32(0);
        return;
    }
    std::string text = ldf_write_text(config);
    if (!text.empty() && text.back() == '\n') text.pop_back();
    w.write_u32(static_cast<uint32_t>(text.size()));
    for (char c : text) w.write_u16(static_cast<uint16_t>(static_cast<uint8_t>(c)));
}

static void write_render_technique(BinaryWriter& w, const LvlRenderTechnique& rt) {
    w.write_u32(static_cast<uint32_t>(rt.attrs.size()));
    if (rt.attrs.empty()) return;

    w.write_fixed_str(rt.name, 64);
    for (auto& attr : rt.attrs) {
        w.write_fixed_str(attr.name, 64);
        w.write_u32(attr.num_floats);
        w.write_u8(attr.is_color ? 1 : 0);
        for (float f : attr.values) w.write_f32(f);
    }
}

static size_t write_chunk_header(BinaryWriter& w, uint32_t id,
                                  uint16_t header_version, uint16_t data_version) {
    w.write_u32(LVL_CHUNK_MAGIC);
    w.write_u32(id);
    w.write_u16(header_version);
    w.write_u16(data_version);
    size_t size_pos = w.pos();
    w.write_u32(0);  // total_size (patched later)
    w.write_u32(0);  // data_offset (patched later)
    // 12 bytes of 0xCD padding — matches the original 32-byte CHNK header
    // produced by the NetDevil/Gamebryo level editor
    for (int i = 0; i < 12; ++i) w.write_u8(0xCD);
    return size_pos;
}

static void finish_chunk(BinaryWriter& w, size_t size_pos, size_t data_start) {
    size_t chunk_pos = size_pos - 12;
    // Pad with 0xCD: at least 1 byte, aligned to 16-byte boundary
    size_t unpadded = w.pos() - chunk_pos;
    size_t padded = (unpadded + 16) & ~size_t(15);
    for (size_t i = unpadded; i < padded; ++i) w.write_u8(0xCD);
    uint32_t total = static_cast<uint32_t>(w.pos() - chunk_pos);
    w.patch_u32(size_pos, total);
    w.patch_u32(size_pos + 4, static_cast<uint32_t>(data_start));
}

static void write_lighting(BinaryWriter& w, const LvlLightingInfo& li, uint32_t version) {
    if (version >= 45) w.write_f32(li.blend_time);

    for (float f : li.ambient)    w.write_f32(f);
    for (float f : li.specular)   w.write_f32(f);
    for (float f : li.upper_hemi) w.write_f32(f);
    write_vec3(w, li.position);

    if (version >= 39) {
        auto write_dd = [&](const LvlDrawDistances& dd) {
            w.write_f32(dd.fog_near);
            w.write_f32(dd.fog_far);
            w.write_f32(dd.post_fog_solid);
            w.write_f32(dd.post_fog_fade);
            w.write_f32(dd.static_obj_distance);
            w.write_f32(dd.dynamic_obj_distance);
        };
        write_dd(li.min_draw);
        write_dd(li.max_draw);
    }

    if (version >= 40) {
        w.write_u32(static_cast<uint32_t>(li.cull_vals.size()));
        for (auto& cv : li.cull_vals) {
            w.write_u32(cv.group_id);
            w.write_f32(cv.min);
            w.write_f32(cv.max);
        }
    }

    if (version >= 31 && version < 39) {
        w.write_f32(li.fog_near);
        w.write_f32(li.fog_far);
    }

    if (version >= 31) {
        for (float f : li.fog_color) w.write_f32(f);
    }

    if (version >= 36) {
        for (float f : li.dir_light) w.write_f32(f);
    }

    if (version < 42) {
        write_vec3(w, li.start_position);
        if (version >= 33) write_quat_wxyz(w, li.start_rotation);
    }
}

static void write_skydome(BinaryWriter& w, const LvlSkydomeInfo& si, uint32_t version) {
    w.write_u4_str(si.filename);
    if (version >= 34) {
        w.write_u4_str(si.sky_layer_filename);
        for (auto& rl : si.ring_layer) w.write_u4_str(rl);
    }
}

static void write_editor(BinaryWriter& w, const LvlEditorSettings& es) {
    size_t block_size_pos = w.pos();
    w.write_u32(0);
    size_t block_start = w.pos();
    w.write_u32(static_cast<uint32_t>(es.saved_colors.size()));
    for (auto& c : es.saved_colors) {
        w.write_f32(c.r);
        w.write_f32(c.g);
        w.write_f32(c.b);
    }
    uint32_t block_size = static_cast<uint32_t>(w.pos() - block_start);
    w.patch_u32(block_size_pos, block_size);
}

std::vector<uint8_t> lvl_write(const LvlFile& lvl) {
    BinaryWriter w;
    uint32_t version = lvl.version;

    BinaryWriter obj_payload;
    obj_payload.write_u32(static_cast<uint32_t>(lvl.objects.size()));
    for (auto& obj : lvl.objects) {
        obj_payload.write_u64(obj.object_id);
        obj_payload.write_u32(obj.lot);
        if (version >= 38) obj_payload.write_u32(static_cast<uint32_t>(obj.node_type));
        if (version >= 32) obj_payload.write_u32(obj.glom_id);
        write_vec3(obj_payload, obj.position);
        write_quat_wxyz(obj_payload, obj.rotation);
        obj_payload.write_f32(obj.scale);
        write_object_config(obj_payload, obj.config);
        if (version >= 7) write_render_technique(obj_payload, obj.render_technique);
    }

    BinaryWriter particle_payload;
    particle_payload.write_u32(static_cast<uint32_t>(lvl.particles.size()));
    for (auto& p : lvl.particles) {
        if (version >= 43) particle_payload.write_u16(p.priority);
        write_vec3(particle_payload, p.position);
        write_quat_wxyz(particle_payload, p.rotation);

        particle_payload.write_u32(static_cast<uint32_t>(p.effect_names.size()));
        for (char c : p.effect_names)
            particle_payload.write_u16(static_cast<uint16_t>(static_cast<uint8_t>(c)));

        if (version < 46) {
            particle_payload.write_u8(0);
            particle_payload.write_u8(0);
        } else {
            std::string cfg_str = ldf_write_text(p.config);
            if (!cfg_str.empty() && cfg_str.back() == '\n') cfg_str.pop_back();
            particle_payload.write_u32(static_cast<uint32_t>(cfg_str.size()));
            for (char c : cfg_str)
                particle_payload.write_u16(static_cast<uint16_t>(static_cast<uint8_t>(c)));
        }
    }

    // === Chunk 1000 (fib) ===
    size_t fib_size_pos = write_chunk_header(w, 1000, 1, 1);
    size_t fib_data_start = w.pos();
    w.write_u32(version);
    w.write_u32(lvl.revision);
    size_t ofs_env_pos = w.pos();      w.write_u32(0);
    size_t ofs_obj_pos = w.pos();      w.write_u32(0);
    size_t ofs_particle_pos = w.pos(); w.write_u32(0);
    finish_chunk(w, fib_size_pos, fib_data_start);

    // === Chunk 2000 (environment) ===
    if (lvl.has_environment) {
        w.patch_u32(ofs_env_pos, static_cast<uint32_t>(w.pos()));

        size_t env_size_pos = write_chunk_header(w, 2000, 1, lvl.env_data_version);
        size_t env_data_start = w.pos();

        size_t lighting_ofs_pos = w.pos(); w.write_u32(0);
        size_t skydome_ofs_pos  = w.pos(); w.write_u32(0);
        size_t editor_ofs_pos   = w.pos(); w.write_u32(0);

        w.patch_u32(lighting_ofs_pos, static_cast<uint32_t>(w.pos()));
        write_lighting(w, lvl.environment.lighting, version);

        w.patch_u32(skydome_ofs_pos, static_cast<uint32_t>(w.pos()));
        write_skydome(w, lvl.environment.skydome, version);

        if (version >= 37 && lvl.environment.has_editor) {
            w.patch_u32(editor_ofs_pos, static_cast<uint32_t>(w.pos()));
            write_editor(w, lvl.environment.editor);
        }

        finish_chunk(w, env_size_pos, env_data_start);
    }

    // === Chunk 2001 (objects) ===
    if (lvl.has_objects || !lvl.objects.empty()) {
        w.patch_u32(ofs_obj_pos, static_cast<uint32_t>(w.pos()));

        size_t obj_size_pos = write_chunk_header(w, 2001, 1, 1);
        size_t obj_data_start = w.pos();
        w.write_bytes(obj_payload.data().data(), obj_payload.data().size());
        finish_chunk(w, obj_size_pos, obj_data_start);
    }

    // === Chunk 2002 (particles) ===
    if (lvl.has_particles || !lvl.particles.empty()) {
        w.patch_u32(ofs_particle_pos, static_cast<uint32_t>(w.pos()));

        size_t particle_size_pos = write_chunk_header(w, 2002, 1, 1);
        size_t particle_data_start = w.pos();
        w.write_bytes(particle_payload.data().data(), particle_payload.data().size());
        finish_chunk(w, particle_size_pos, particle_data_start);
    }

    return w.data();
}

} // namespace lu::assets
