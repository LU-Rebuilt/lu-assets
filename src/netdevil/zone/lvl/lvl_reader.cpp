#include "netdevil/zone/lvl/lvl_reader.h"
#include "netdevil/common/ldf/ldf_reader.h"
#include "common/binary_reader/binary_reader.h"

#include <cstring>

namespace lu::assets {

// ── Chunk header ──────────────────────────────────────────────────────────────

struct ChunkHeader {
    uint32_t id;
    uint16_t header_version;
    uint16_t data_version;
    uint32_t total_size;   // Full chunk size including the 20-byte CHNK header
    uint32_t data_offset;  // Absolute file offset of the chunk payload
    size_t   chunk_pos;    // File offset where the CHNK magic was found
};

// Scan forward for the next CHNK magic. Returns true on success.
// `chunk_pos` on the reader is set to the byte after the full header (i.e.,
// scan continues from there after this call), but CallerMust use chunk_pos
// to advance to the next chunk boundary.
static bool find_next_chunk(BinaryReader& r, ChunkHeader& out) {
    while (r.remaining() >= 20) {
        size_t scan_pos = r.pos();
        uint32_t magic = r.read_u32();
        if (magic == LVL_CHUNK_MAGIC) {
            out.chunk_pos     = scan_pos;
            out.id            = r.read_u32();
            out.header_version= r.read_u16();
            out.data_version  = r.read_u16();
            out.total_size    = r.read_u32();
            out.data_offset   = r.read_u32();
            return true;
        }
        r.seek(scan_pos + 1);
    }
    return false;
}

// ── Common helpers ────────────────────────────────────────────────────────────

static Vec3 read_vec3(BinaryReader& r) {
    Vec3 v;
    v.x = r.read_f32();
    v.y = r.read_f32();
    v.z = r.read_f32();
    return v;
}

// Quaternion stored W-first in the file; convert to XYZW for our struct.
static Quat read_quat_wxyz(BinaryReader& r) {
    Quat q;
    q.w = r.read_f32();
    q.x = r.read_f32();
    q.y = r.read_f32();
    q.z = r.read_f32();
    return q;
}

// u4_str: u32 byte-length + ASCII bytes (NOT char_count × 2).
// Used in skydome filenames. The ksy type is ASCII, so length = byte count.
static std::string read_u4_str(BinaryReader& r) {
    uint32_t len = r.read_u32();
    if (len == 0 || len > 4096) { r.skip(len); return {}; }
    std::string s(len, '\0');
    auto bytes = r.read_bytes(len);
    std::memcpy(s.data(), bytes.data(), len);
    return s;
}

// u4_wstr: u32 char_count + char_count × UTF-16LE chars → ASCII.
// Used in effect_names and particle config_data.
static std::string read_u4_wstr(BinaryReader& r) {
    uint32_t char_count = r.read_u32();
    if (char_count == 0) return {};
    if (char_count > 65536) { r.skip(char_count * 2); return {}; }
    std::string result;
    result.reserve(char_count);
    for (uint32_t i = 0; i < char_count; ++i) {
        uint16_t wc = r.read_u16();
        result += static_cast<char>(wc < 128 ? wc : '?');
    }
    return result;
}

// Read a null-terminated ASCII string from a fixed-size field.
static std::string read_fixed_str(BinaryReader& r, size_t field_size) {
    auto bytes = r.read_bytes(field_size);
    size_t len = 0;
    while (len < field_size && bytes[len] != 0) ++len;
    return std::string(reinterpret_cast<const char*>(bytes.data()), len);
}

// ── Chunk 2000: Environment ───────────────────────────────────────────────────

// lighting_info — version-gated per lu_formats/files/lvl.ksy.
// Offsets are absolute within the file, so we create a fresh BinaryReader for each sub-section.
static LvlLightingInfo parse_lighting(std::span<const uint8_t> file_data,
                                       uint32_t abs_offset,
                                       uint32_t version) {
    LvlLightingInfo li;
    if (abs_offset == 0 || abs_offset >= file_data.size()) return li;

    BinaryReader r(file_data.subspan(abs_offset));

    // blend_time (version >= 45)
    if (version >= 45) li.blend_time = r.read_f32();

    // ambient, specular, upper_hemi (3 floats each, always present)
    for (float& f : li.ambient)    f = r.read_f32();
    for (float& f : li.specular)   f = r.read_f32();
    for (float& f : li.upper_hemi) f = r.read_f32();

    li.position = read_vec3(r);

    // Draw distances (version >= 39)
    if (version >= 39) {
        li.has_draw_distances = true;
        auto& mn = li.min_draw;
        mn.fog_near = r.read_f32(); mn.fog_far = r.read_f32();
        mn.post_fog_solid = r.read_f32(); mn.post_fog_fade = r.read_f32();
        mn.static_obj_distance = r.read_f32(); mn.dynamic_obj_distance = r.read_f32();

        auto& mx = li.max_draw;
        mx.fog_near = r.read_f32(); mx.fog_far = r.read_f32();
        mx.post_fog_solid = r.read_f32(); mx.post_fog_fade = r.read_f32();
        mx.static_obj_distance = r.read_f32(); mx.dynamic_obj_distance = r.read_f32();
    }

    // Cull data (version >= 40)
    if (version >= 40) {
        uint32_t num = r.read_u32();
        li.cull_vals.reserve(num);
        for (uint32_t i = 0; i < num && r.remaining() >= 12; ++i) {
            LvlCullVal cv;
            cv.group_id = r.read_u32();
            cv.min      = r.read_f32();
            cv.max      = r.read_f32();
            li.cull_vals.push_back(cv);
        }
    }

    // Fog near/far (version 31–38 only)
    if (version >= 31 && version < 39) {
        li.fog_near = r.read_f32();
        li.fog_far  = r.read_f32();
    }

    // Fog color (version >= 31)
    if (version >= 31) {
        for (float& f : li.fog_color) f = r.read_f32();
    }

    // Directional light (version >= 36)
    if (version >= 36) {
        for (float& f : li.dir_light) f = r.read_f32();
    }

    // Spawn position (version < 42)
    if (version < 42) {
        li.has_spawn    = true;
        li.start_position = read_vec3(r);
        // Spawn rotation (version 33–41)
        if (version >= 33) li.start_rotation = read_quat_wxyz(r);
    }

    return li;
}

static LvlSkydomeInfo parse_skydome(std::span<const uint8_t> file_data,
                                     uint32_t abs_offset,
                                     uint32_t version) {
    LvlSkydomeInfo si;
    if (abs_offset == 0 || abs_offset >= file_data.size()) return si;

    BinaryReader r(file_data.subspan(abs_offset));
    si.filename = read_u4_str(r);

    // sky_layer + ring_layer[0-3] (version >= 34)
    if (version >= 34 && r.remaining() >= 4) {
        si.sky_layer_filename = read_u4_str(r);
        for (auto& rl : si.ring_layer) {
            if (r.remaining() >= 4) rl = read_u4_str(r);
        }
    }
    return si;
}

static LvlEditorSettings parse_editor(std::span<const uint8_t> file_data,
                                       uint32_t abs_offset) {
    LvlEditorSettings es;
    if (abs_offset == 0 || abs_offset >= file_data.size()) return es;

    BinaryReader r(file_data.subspan(abs_offset));
    r.skip(4);  // chunk_size (u32) — size of this sub-block; we read sequentially
    uint32_t num_colors = r.read_u32();
    if (num_colors > 4096) return es;
    es.saved_colors.reserve(num_colors);
    for (uint32_t i = 0; i < num_colors && r.remaining() >= 12; ++i) {
        LvlEditorColor c;
        c.r = r.read_f32();
        c.g = r.read_f32();
        c.b = r.read_f32();
        es.saved_colors.push_back(c);
    }
    return es;
}

// Chunk 2000 payload: 3 absolute offsets → lighting, skydome, editor.
static LvlEnvironmentData parse_env_chunk(std::span<const uint8_t> file_data,
                                           uint32_t data_offset,
                                           uint32_t version) {
    LvlEnvironmentData env;
    if (data_offset + 12 > file_data.size()) return env;

    BinaryReader r(file_data.subspan(data_offset, 12));
    uint32_t ofs_lighting = r.read_u32();
    uint32_t ofs_skydome  = r.read_u32();
    uint32_t ofs_editor   = r.read_u32();

    env.lighting = parse_lighting(file_data, ofs_lighting, version);
    env.skydome  = parse_skydome(file_data, ofs_skydome, version);

    if (version >= 37 && ofs_editor != 0) {
        env.editor     = parse_editor(file_data, ofs_editor);
        env.has_editor = true;
    }

    return env;
}

// ── Chunk 2001: Objects ───────────────────────────────────────────────────────

// Object config string: u32 char_count + char_count × UTF-16LE → ASCII → LDF parse.
static std::vector<LdfEntry> read_object_config(BinaryReader& r) {
    uint32_t char_count = r.read_u32();
    if (char_count == 0) return {};
    if (char_count > 1000000) { r.skip(char_count * 2); return {}; }

    std::string ascii;
    ascii.reserve(char_count);
    for (uint32_t i = 0; i < char_count; ++i) {
        uint16_t wc = r.read_u16();
        ascii += static_cast<char>(wc < 128 ? wc : '?');
    }
    return ldf_parse(ascii);
}

// render_technique — lu_formats/files/lvl.ksy render_technique + render_attr.
// num_render_attrs u32; if > 0: name (64-byte ASCII); then num_render_attrs × render_attr.
// render_attr: name (64-byte ASCII), num_floats u32, is_color u8, floats[4].
static LvlRenderTechnique read_render_technique(BinaryReader& r) {
    LvlRenderTechnique rt;
    uint32_t num_attrs = r.read_u32();
    if (num_attrs == 0) return rt;

    if (r.remaining() >= 64)
        rt.name = read_fixed_str(r, 64);

    rt.attrs.reserve(num_attrs);
    for (uint32_t i = 0; i < num_attrs; ++i) {
        if (r.remaining() < 64 + 4 + 1 + 16) break;
        LvlRenderAttr attr;
        attr.name       = read_fixed_str(r, 64);
        attr.num_floats = r.read_u32();
        attr.is_color   = r.read_u8() != 0;
        for (float& f : attr.values) f = r.read_f32();
        rt.attrs.push_back(std::move(attr));
    }
    return rt;
}

// Parse one scene object. headerVersion is from the fib chunk.
static LvlObject parse_object(BinaryReader& r, uint32_t version) {
    LvlObject obj;
    obj.object_id = r.read_u64();
    obj.lot       = r.read_u32();

    // obj_type: version >= 38 (ksy says >=38, not >37)
    if (version >= 38) {
        uint32_t nt = r.read_u32();
        obj.node_type = static_cast<LvlNodeType>(nt <= 15 ? nt : 1);
    }

    // glom_id: version >= 32
    if (version >= 32) obj.glom_id = r.read_u32();

    obj.position = read_vec3(r);
    obj.rotation = read_quat_wxyz(r);  // File: WXYZ; stored as XYZW (conversion in read_quat_wxyz)
    obj.scale    = r.read_f32();

    obj.config = read_object_config(r);

    // render_technique (version >= 7)
    if (version >= 7) {
        obj.render_technique = read_render_technique(r);
    }

    return obj;
}

// ── Chunk 2002: Particles ─────────────────────────────────────────────────────

static LvlParticle parse_particle(BinaryReader& r, uint32_t version) {
    LvlParticle p;

    // priority (version >= 43)
    if (version >= 43) p.priority = r.read_u16();

    p.position = read_vec3(r);
    p.rotation = read_quat_wxyz(r);

    // effect_names: u4_wstr (u32 char_count + UTF-16LE)
    p.effect_names = read_u4_wstr(r);

    // null_terminator: 2 bytes [0x00, 0x00] if version < 46
    if (version < 46) r.skip(2);

    // config_data: u4_wstr parsed as LDF
    std::string cfg_str = read_u4_wstr(r);
    if (!cfg_str.empty()) p.config = ldf_parse(cfg_str);

    return p;
}

// ── Main parser ───────────────────────────────────────────────────────────────

LvlFile lvl_parse(std::span<const uint8_t> data) {
    BinaryReader r(data);
    LvlFile lvl;

    ChunkHeader chunk;
    while (find_next_chunk(r, chunk)) {
        // Chunk boundary: chunk_pos + total_size (NOT start_pos + (size-20)).
        // total_size is the full chunk size including the 20-byte header.
        // The gap between header end (chunk_pos+20) and data_offset may contain
        // 0xCD padding bytes; advancing by total_size correctly skips to the next
        // chunk regardless of padding.
        size_t chunk_end = chunk.chunk_pos + chunk.total_size;
        if (chunk_end > data.size()) chunk_end = data.size();

        size_t data_available = (chunk.data_offset < data.size())
            ? (chunk_end - chunk.data_offset)
            : 0;

        if (chunk.id == 1000 && data_available >= 4) {
            // fib_data: version u32, revision u32, ofs_env u32, ofs_obj u32, ofs_particle u32.
            // Minimum: 4 bytes (version). Full spec: 20 bytes (5 u32s).
            BinaryReader cr(data.subspan(chunk.data_offset, data_available));
            lvl.version  = cr.read_u32();
            if (data_available >= 8)  lvl.revision = cr.read_u32();
            // Offset fields (ofs_environment, ofs_object, ofs_particle) are absolute
            // file offsets to the respective CHNK headers. We discover chunks via
            // find_next_chunk scanning so we don't need to seek to them explicitly.
        }
        else if (chunk.id == 2000 && data_available >= 12) {
            // Environment chunk
            lvl.environment     = parse_env_chunk(data, chunk.data_offset, lvl.version);
            lvl.has_environment = true;
        }
        else if (chunk.id == 2001 && data_available >= 4) {
            // SceneObjectData chunk
            BinaryReader cr(data.subspan(chunk.data_offset, data_available));
            uint32_t obj_count = cr.read_u32();
            lvl.objects.reserve(obj_count);
            for (uint32_t i = 0; i < obj_count; ++i) {
                if (cr.remaining() < 8) break;
                try {
                    lvl.objects.push_back(parse_object(cr, lvl.version));
                } catch (const std::out_of_range&) {
                    break;
                }
            }
        }
        else if (chunk.id == 2002 && data_available >= 4) {
            // Particle chunk
            BinaryReader cr(data.subspan(chunk.data_offset, data_available));
            uint32_t count = cr.read_u32();
            lvl.particles.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                if (cr.remaining() < 14) break;
                try {
                    lvl.particles.push_back(parse_particle(cr, lvl.version));
                } catch (const std::out_of_range&) {
                    break;
                }
            }
        }

        r.seek(chunk_end);
    }

    return lvl;
}

} // namespace lu::assets
