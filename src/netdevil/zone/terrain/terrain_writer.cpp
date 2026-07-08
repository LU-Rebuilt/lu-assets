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

        // Light map: u32 size + raw data
        w.write_u32(static_cast<uint32_t>(chunk.light_map.size()));
        w.write_bytes(chunk.light_map.data(), chunk.light_map.size());

        if (terrain.version >= 32) {
            w.write_u32(chunk.tex_map_res);
            w.write_bytes(chunk.texture_map.data(), chunk.texture_map.size());
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

        // Scene map (version >= 32, present when the color map has a resolution)
        if (terrain.version >= 32 && chunk.color_map_res > 0) {
            w.write_bytes(chunk.scene_map.data(), chunk.scene_map.size());
        }

        // Mesh LOD section: u32 vert count, then usage/sizes/tris when nonzero
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

    return std::move(w.data());
}

} // namespace lu::assets
