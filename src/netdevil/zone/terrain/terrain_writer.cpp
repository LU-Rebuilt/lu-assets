#include "netdevil/zone/terrain/terrain_writer.h"
#include "common/binary_writer/binary_writer.h"

namespace lu::assets {

std::vector<uint8_t> terrain_write(const TerrainFile& terrain) {
    BinaryWriter w;

    w.write_u16(terrain.version);
    w.write_u8(terrain.dev_flag);
    w.write_u32(static_cast<uint32_t>(terrain.chunks.size()));
    w.write_u32(terrain.chunks_width);
    w.write_u32(terrain.chunks_height);

    for (const TerrainChunk& chunk : terrain.chunks) {
        w.write_u32(chunk.chunk_id);
        w.write_u32(chunk.width);
        w.write_u32(chunk.height);
        w.write_f32(chunk.offset_x);
        w.write_f32(chunk.offset_z);

        if (terrain.version < 32) {
            w.write_u32(chunk.shader_id);
        }

        for (int i = 0; i < 4; ++i) {
            w.write_u32(chunk.texture_ids[i]);
        }

        w.write_f32(chunk.scale);

        for (float h : chunk.heightmap) {
            w.write_f32(h);
        }

        // Color map — version < 32 stores width*width BGRA texels with no resolution
        // field (the client derives width-1); version >= 32 stores an explicit
        // resolution + RGBA texels.
        if (terrain.version < 32) {
            w.write_bytes(chunk.color_map_full.data(), chunk.color_map_full.size());
        } else {
            w.write_u32(chunk.color_map_res);
            w.write_bytes(chunk.color_map.data(), chunk.color_map.size());
        }

        // Light map: version >= 32 only (RAWReadColorandLightMaps gates size+data on
        // `31 < version`; v<32 has no light map field on disk at all).
        if (terrain.version >= 32) {
            w.write_u32(static_cast<uint32_t>(chunk.light_map.size()));
            w.write_bytes(chunk.light_map.data(), chunk.light_map.size());
        }

        // Texture map: present for every version. v<32 re-swizzles RGBA back to the
        // on-disk BGRA order (inverse of the reader's swap); v>=32 writes RGBA directly.
        w.write_u32(chunk.tex_map_res);
        if (terrain.version < 32) {
            for (size_t i = 0; i + 3 < chunk.texture_map.size(); i += 4) {
                w.write_u8(chunk.texture_map[i + 2]); // B
                w.write_u8(chunk.texture_map[i + 1]); // G
                w.write_u8(chunk.texture_map[i + 0]); // R
                w.write_u8(chunk.texture_map[i + 3]); // A
            }
        } else {
            w.write_bytes(chunk.texture_map.data(), chunk.texture_map.size());
        }

        // Texture settings byte + blend map: version >= 32 only.
        if (terrain.version >= 32) {
            w.write_u8(chunk.texture_settings);
            w.write_u32(static_cast<uint32_t>(chunk.blend_map.size()));
            w.write_bytes(chunk.blend_map.data(), chunk.blend_map.size());
        }

        w.write_u32(static_cast<uint32_t>(chunk.flairs.size()));
        for (const TerrainFlair& flair : chunk.flairs) {
            w.write_u32(flair.id);
            w.write_f32(flair.scale);
            w.write_f32(flair.position.x);
            w.write_f32(flair.position.y);
            w.write_f32(flair.position.z);
            w.write_f32(flair.rotation.x);
            w.write_f32(flair.rotation.y);
            w.write_f32(flair.rotation.z);
            w.write_u8(flair.r);
            w.write_u8(flair.g);
            w.write_u8(flair.b);
            w.write_u8(flair.a);
        }

        // Scene map (RAWReadSceneMap) — mirrors the reader's three version bands.
        if (terrain.version < 31) {
            w.write_u8(chunk.scene_map_skip_byte);
        } else if (terrain.version == 31) {
            w.write_bytes(chunk.scene_map_full.data(), chunk.scene_map_full.size());
        } else if (chunk.color_map_res > 0) {
            w.write_bytes(chunk.scene_map.data(), chunk.scene_map.size());
        }

        // Mesh LOD section: version >= 32 only.
        if (terrain.version >= 32) {
            w.write_u32(chunk.mesh_vert_count);
            if (chunk.mesh_vert_count > 0) {
                for (uint16_t v : chunk.mesh_vert_usage) {
                    w.write_u16(v);
                }
                for (int i = 0; i < 16; ++i) {
                    w.write_u16(chunk.mesh_vert_size[i]);
                }
                for (int i = 0; i < 16; ++i) {
                    w.write_u16(static_cast<uint16_t>(chunk.mesh_tris[i].size()));
                    for (uint16_t t : chunk.mesh_tris[i]) {
                        w.write_u16(t);
                    }
                }
            }
        }
    }

    return std::move(w.data());
}

} // namespace lu::assets
