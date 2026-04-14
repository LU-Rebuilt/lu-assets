#include "netdevil/zone/terrain/terrain_reader.h"
#include "common/binary_reader/binary_reader.h"

namespace lu::assets {

TerrainFile terrain_parse(std::span<const uint8_t> data) {
    BinaryReader r(data);
    TerrainFile terrain;

    terrain.version = r.read_u16();
    terrain.dev_flag = r.read_u8();
    terrain.chunk_count = r.read_u32();
    terrain.chunks_width = r.read_u32();
    terrain.chunks_height = r.read_u32();

    terrain.chunks.reserve(terrain.chunk_count);

    for (uint32_t c = 0; c < terrain.chunk_count; ++c) {
        if (r.remaining() < 20) break;

        TerrainChunk chunk;
        chunk.chunk_id = r.read_u32();
        chunk.width = r.read_u32();
        chunk.height = r.read_u32();
        chunk.offset_x = r.read_f32();
        chunk.offset_z = r.read_f32();

        // Shader ID (version < 32 only)
        if (terrain.version < 32) {
            chunk.shader_id = r.read_u32();
        }

        // Texture layer IDs (4 terrain texture indices)
        for (int i = 0; i < 4; ++i) {
            chunk.texture_ids[i] = r.read_u32();
        }

        chunk.scale = r.read_f32();

        // Heightmap: width * height float values
        size_t hm_count = static_cast<size_t>(chunk.width) * chunk.height;
        if (r.remaining() < hm_count * 4) break;
        chunk.heightmap.resize(hm_count);
        for (size_t i = 0; i < hm_count; ++i) {
            chunk.heightmap[i] = r.read_f32();
        }

        // Color map resolution
        if (terrain.version >= 32) {
            chunk.color_map_res = r.read_u32();
        } else {
            chunk.color_map_res = chunk.width;
        }

        // Color map: RGBA per texel
        if (chunk.color_map_res > 0 && chunk.color_map_res <= 4096) {
            size_t cm_bytes = static_cast<size_t>(chunk.color_map_res) * chunk.color_map_res * 4;
            if (r.remaining() >= cm_bytes) {
                auto cm = r.read_bytes(cm_bytes);
                chunk.color_map.assign(cm.begin(), cm.end());
            }
        }

        // Light map: u32 size + raw data (baked lighting)
        if (r.remaining() >= 4) {
            uint32_t lm_size = r.read_u32();
            if (lm_size > 0 && lm_size <= 64 * 1024 * 1024 && r.remaining() >= lm_size) {
                auto lm = r.read_bytes(lm_size);
                chunk.light_map.assign(lm.begin(), lm.end());
            }
        }

        // Version >= 32: additional texture data
        if (terrain.version >= 32 && r.remaining() >= 4) {
            // Texture map: per-texel texture selection
            chunk.tex_map_res = r.read_u32();
            if (chunk.tex_map_res > 0 && chunk.tex_map_res <= 4096) {
                size_t tex_bytes = static_cast<size_t>(chunk.tex_map_res) * chunk.tex_map_res * 4;
                if (r.remaining() >= tex_bytes) {
                    auto tm = r.read_bytes(tex_bytes);
                    chunk.texture_map.assign(tm.begin(), tm.end());
                }
            }

            // Texture settings flags
            if (r.remaining() >= 1) {
                chunk.texture_settings = r.read_u8();
            }

            // Blend map: DDS-format texture for terrain layer blending
            if (r.remaining() >= 4) {
                uint32_t blend_size = r.read_u32();
                if (blend_size > 0 && r.remaining() >= blend_size) {
                    auto bm = r.read_bytes(blend_size);
                    chunk.blend_map.assign(bm.begin(), bm.end());
                }
            }
        }

        // Flairs: terrain decoration objects
        if (r.remaining() >= 4) {
            uint32_t flair_count = r.read_u32();
            if (flair_count <= 100000) {
                chunk.flairs.reserve(flair_count);
                for (uint32_t f = 0; f < flair_count; ++f) {
                    if (r.remaining() < 36) break;
                    TerrainFlair flair;
                    flair.id = r.read_u32();
                    flair.scale = r.read_f32();
                    flair.position.x = r.read_f32();
                    flair.position.y = r.read_f32();
                    flair.position.z = r.read_f32();
                    flair.rotation.x = r.read_f32();
                    flair.rotation.y = r.read_f32();
                    flair.rotation.z = r.read_f32();
                    flair.r = r.read_u8();
                    flair.g = r.read_u8();
                    flair.b = r.read_u8();
                    flair.a = r.read_u8();
                    chunk.flairs.push_back(flair);
                }
            }
        }

        // Scene map: per-texel scene ID assignment (version >= 32)
        if (terrain.version >= 32 && chunk.color_map_res > 0) {
            size_t sm_bytes = static_cast<size_t>(chunk.color_map_res) * chunk.color_map_res;
            if (r.remaining() >= sm_bytes) {
                auto sm = r.read_bytes(sm_bytes);
                chunk.scene_map.assign(sm.begin(), sm.end());
            }
        }

        // Mesh vertex/triangle data (present in all versions)
        // vertSize (u32) — if 0, no further mesh data
        if (r.remaining() >= 4) {
            uint32_t vertSize = r.read_u32();
            if (vertSize > 0 && vertSize < 1000000) {
                // meshVertUsage[vertSize] (uint16 array)
                size_t vBytes = static_cast<size_t>(vertSize) * 2;
                if (r.remaining() >= vBytes) r.skip(vBytes);

                // meshVertSize[16] (always 16 uint16s)
                if (r.remaining() >= 32) r.skip(32);

                // meshTri[16] — each: triCount(u16) + triIndices[triCount](u16)
                for (int mt = 0; mt < 16 && r.remaining() >= 2; mt++) {
                    uint16_t triCount = r.read_u16();
                    size_t triBytes = static_cast<size_t>(triCount) * 2;
                    if (r.remaining() >= triBytes) r.skip(triBytes);
                }
            }
        }

        terrain.chunks.push_back(std::move(chunk));
    }

    return terrain;
}

TerrainMesh terrain_generate_mesh(const TerrainFile& terrain) {
    TerrainMesh out;

    for (const auto& chunk : terrain.chunks) {
        if (chunk.heightmap.empty() || chunk.width <= 1 || chunk.height <= 1) continue;

        uint32_t w = chunk.width;
        uint32_t h = chunk.height;
        float s = chunk.scale;
        if (s <= 0) s = 1.0f;

        uint32_t baseIdx = static_cast<uint32_t>(out.vertices.size() / 3);

        // Vertex generation matching DarkflameServer:
        //   outer loop: col (i), inner loop: row (j)
        //   heightmap index: width * col + row
        for (uint32_t i = 0; i < w; ++i) {
            for (uint32_t j = 0; j < h; ++j) {
                uint32_t idx = w * i + j;
                float y = (idx < chunk.heightmap.size()) ? chunk.heightmap[idx] : 0.0f;
                float x = (static_cast<float>(i) + (chunk.offset_x / s)) * s;
                float worldY = (y / s) * s;
                float z = (static_cast<float>(j) + (chunk.offset_z / s)) * s;
                out.vertices.push_back(x);
                out.vertices.push_back(worldY);
                out.vertices.push_back(z);
            }
        }

        // Triangle generation: two per quad, stride = chunk.height
        for (uint32_t i = 1; i < w; ++i) {
            for (uint32_t j = 1; j < h; ++j) {
                uint32_t tl = baseIdx + (i - 1) * h + (j - 1);
                uint32_t tr = baseIdx + (i)     * h + (j - 1);
                uint32_t bl = baseIdx + (i - 1) * h + (j);
                uint32_t br = baseIdx + (i)     * h + (j);

                out.indices.push_back(tl); out.indices.push_back(tr); out.indices.push_back(bl);
                out.indices.push_back(tr); out.indices.push_back(br); out.indices.push_back(bl);
                out.triangle_count += 2;
            }
        }
    }

    return out;
}

static void heightGradient(float t, float* rgb) {
    t = std::max(0.0f, std::min(1.0f, t));
    if (t < 0.5f) {
        float s = t * 2.0f;
        rgb[0] = 0.1f*(1-s)+0.1f*s; rgb[1] = 0.2f*(1-s)+0.8f*s; rgb[2] = 0.8f*(1-s)+0.2f*s;
    } else {
        float s = (t-0.5f)*2.0f;
        rgb[0] = 0.1f*(1-s)+0.9f*s; rgb[1] = 0.8f*(1-s)+0.3f*s; rgb[2] = 0.2f*(1-s)+0.1f*s;
    }
}

// Official LU scene template colors (146 entries) — from the game client.
// Scene ID 0 = grey (default), IDs 1-145 = distinct colors for scene separation.
static void sceneIdToColor(uint8_t id, float* rgb) {
    static const float pal[][3] = {
        {0.502f,0.502f,0.502f},{1.0f,0.0f,0.0f},{0.0f,1.0f,0.0f},{0.0f,0.0f,1.0f},
        {1.0f,1.0f,0.0f},{1.0f,0.0f,1.0f},{0.0f,1.0f,1.0f},{0.502f,0.0f,1.0f},
        {1.0f,0.502f,0.0f},{1.0f,0.502f,0.502f},{0.502f,0.251f,0.0f},{0.502f,0.0f,0.251f},
        {0.0f,0.502f,0.251f},{0.251f,0.0f,0.502f},{0.875f,0.0f,0.251f},{0.251f,0.875f,0.502f},
        {1.0f,0.749f,0.0f},{1.0f,0.251f,0.063f},{0.251f,0.0f,0.875f},{0.749f,0.063f,0.063f},
        {0.063f,0.749f,0.063f},{1.0f,0.502f,1.0f},{0.937f,0.871f,0.804f},{0.804f,0.584f,0.459f},
        {0.992f,0.851f,0.710f},{0.471f,0.859f,0.886f},{0.529f,0.663f,0.420f},{1.0f,0.643f,0.455f},
        {0.980f,0.906f,0.710f},{0.624f,0.506f,0.439f},{0.992f,0.486f,0.431f},{0.0f,0.0f,0.0f},
        {0.675f,0.898f,0.933f},{0.122f,0.459f,0.996f},{0.635f,0.635f,0.816f},{0.4f,0.6f,0.8f},
        {0.051f,0.596f,0.729f},{0.451f,0.4f,0.741f},{0.871f,0.365f,0.514f},{0.796f,0.255f,0.329f},
        {0.706f,0.404f,0.302f},{1.0f,0.498f,0.286f},{0.918f,0.494f,0.365f},{0.690f,0.718f,0.776f},
        {1.0f,1.0f,0.6f},{0.110f,0.827f,0.635f},{1.0f,0.667f,0.8f},{0.867f,0.267f,0.573f},
        {0.114f,0.675f,0.839f},{0.737f,0.365f,0.345f},{0.867f,0.580f,0.459f},{0.604f,0.808f,0.922f},
        {1.0f,0.737f,0.851f},{0.992f,0.859f,0.427f},{0.169f,0.424f,0.769f},{0.937f,0.804f,0.722f},
        {0.431f,0.318f,0.376f},{0.808f,1.0f,0.114f},{0.427f,0.682f,0.506f},{0.765f,0.392f,0.773f},
        {0.8f,0.4f,0.4f},{0.906f,0.776f,0.592f},{0.988f,0.851f,0.459f},{0.659f,0.894f,0.627f},
        {0.584f,0.569f,0.549f},{0.110f,0.675f,0.471f},{0.067f,0.392f,0.706f},{0.941f,0.910f,0.569f},
        {1.0f,0.114f,0.808f},{0.698f,0.925f,0.365f},{0.365f,0.463f,0.796f},{0.792f,0.216f,0.404f},
        {0.231f,0.690f,0.561f},{0.988f,0.706f,0.835f},{1.0f,0.957f,0.310f},{1.0f,0.741f,0.533f},
        {0.965f,0.392f,0.686f},{0.667f,0.941f,0.820f},{0.804f,0.290f,0.298f},{0.929f,0.820f,0.612f},
        {0.592f,0.604f,0.667f},{0.784f,0.220f,0.353f},{0.937f,0.596f,0.667f},{0.992f,0.737f,0.706f},
        {0.102f,0.282f,0.463f},{0.188f,0.729f,0.561f},{0.773f,0.294f,0.549f},{0.098f,0.455f,0.824f},
        {0.729f,0.722f,0.424f},{1.0f,0.459f,0.220f},{1.0f,0.169f,0.169f},{0.973f,0.835f,0.408f},
        {0.902f,0.659f,0.843f},{0.255f,0.290f,0.298f},{1.0f,0.431f,0.290f},{0.110f,0.663f,0.788f},
        {1.0f,0.812f,0.671f},{0.773f,0.816f,0.902f},{0.992f,0.867f,0.902f},{0.082f,0.502f,0.471f},
        {0.988f,0.455f,0.992f},{0.969f,0.561f,0.655f},{0.557f,0.271f,0.522f},{0.455f,0.259f,0.784f},
        {0.616f,0.506f,0.729f},{1.0f,0.286f,0.424f},{0.839f,0.541f,0.349f},{0.443f,0.294f,0.137f},
        {1.0f,0.282f,0.816f},{0.933f,0.125f,0.302f},{1.0f,0.325f,0.286f},{0.753f,0.267f,0.561f},
        {0.122f,0.808f,0.796f},{0.471f,0.318f,0.663f},{1.0f,0.608f,0.667f},{0.988f,0.157f,0.278f},
        {0.463f,1.0f,0.478f},{0.624f,0.886f,0.749f},{0.647f,0.412f,0.310f},{0.541f,0.475f,0.365f},
        {0.271f,0.808f,0.635f},{0.804f,0.773f,0.761f},{0.502f,0.855f,0.922f},{0.925f,0.918f,0.745f},
        {1.0f,0.812f,0.282f},{0.992f,0.369f,0.325f},{0.980f,0.655f,0.424f},{0.094f,0.655f,0.710f},
        {0.922f,0.780f,0.875f},{0.988f,0.537f,0.675f},{0.859f,0.843f,0.824f},{0.871f,0.667f,0.533f},
        {0.467f,0.867f,0.906f},{1.0f,1.0f,0.4f},{0.573f,0.431f,0.682f},{0.196f,0.290f,0.698f},
        {0.969f,0.325f,0.580f},{1.0f,0.627f,0.537f},{0.561f,0.314f,0.616f},{1.0f,1.0f,1.0f},
        {0.635f,0.678f,0.816f},{0.988f,0.424f,0.522f},{0.804f,0.643f,0.871f},{0.988f,0.910f,0.514f},
        {0.773f,0.890f,0.518f},{1.0f,0.682f,0.259f},
    };
    const float* c = (id < 146) ? pal[id] : pal[0];
    rgb[0]=c[0]; rgb[1]=c[1]; rgb[2]=c[2];
}

TerrainVertexColors terrain_generate_colors(const TerrainFile& terrain, TerrainColorMode mode) {
    TerrainVertexColors out;

    // Compute global height range
    float hMin = 1e30f, hMax = -1e30f;
    for (const auto& chunk : terrain.chunks)
        for (float h : chunk.heightmap) { hMin = std::min(hMin, h); hMax = std::max(hMax, h); }
    float hRange = hMax - hMin;
    if (hRange < 0.01f) hRange = 1.0f;

    for (const auto& chunk : terrain.chunks) {
        if (chunk.heightmap.empty() || chunk.width <= 1 || chunk.height <= 1) continue;
        uint32_t w = chunk.width, h = chunk.height;

        for (uint32_t i = 0; i < w; ++i) {
            for (uint32_t j = 0; j < h; ++j) {
                uint32_t idx = w * i + j;
                float y = (idx < chunk.heightmap.size()) ? chunk.heightmap[idx] : 0.0f;
                float rgb[3] = {0.6f, 0.6f, 0.6f};

                switch (mode) {
                case TerrainColorMode::HeightGradient:
                    heightGradient((y - hMin) / hRange, rgb);
                    break;
                case TerrainColorMode::ColorMap:
                    if (!chunk.color_map.empty() && chunk.color_map_res > 0 && w > 1 && h > 1) {
                        float ci = (static_cast<float>(i) / (w-1)) * (chunk.color_map_res-1);
                        float cj = (static_cast<float>(j) / (h-1)) * (chunk.color_map_res-1);
                        uint32_t cu = std::min(static_cast<uint32_t>(ci), chunk.color_map_res-1);
                        uint32_t cv = std::min(static_cast<uint32_t>(cj), chunk.color_map_res-1);
                        uint32_t ti = (cu * chunk.color_map_res + cv) * 4;
                        if (ti + 3 < chunk.color_map.size()) {
                            rgb[0] = chunk.color_map[ti]/255.0f;
                            rgb[1] = chunk.color_map[ti+1]/255.0f;
                            rgb[2] = chunk.color_map[ti+2]/255.0f;
                        }
                    } else {
                        heightGradient((y - hMin) / hRange, rgb);
                    }
                    break;
                case TerrainColorMode::SceneMap:
                    if (!chunk.scene_map.empty() && chunk.color_map_res > 0 && w > 1 && h > 1) {
                        float ci = (static_cast<float>(i) / (w-1)) * (chunk.color_map_res-1);
                        float cj = (static_cast<float>(j) / (h-1)) * (chunk.color_map_res-1);
                        uint32_t cu = std::min(static_cast<uint32_t>(ci), chunk.color_map_res-1);
                        uint32_t cv = std::min(static_cast<uint32_t>(cj), chunk.color_map_res-1);
                        uint32_t si = cu * chunk.color_map_res + cv;
                        if (si < chunk.scene_map.size()) sceneIdToColor(chunk.scene_map[si], rgb);
                    }
                    break;
                case TerrainColorMode::TextureWeights:
                    // Texture map stores RGBA per texel — each channel is the weight for one
                    // of the 4 texture layers. Visualize as: R=layer0, G=layer1, B=layer2.
                    // Layer 3 (alpha) contributes to brightness.
                    if (!chunk.texture_map.empty() && chunk.tex_map_res > 0 && w > 1 && h > 1) {
                        float ci = (static_cast<float>(i) / (w-1)) * (chunk.tex_map_res-1);
                        float cj = (static_cast<float>(j) / (h-1)) * (chunk.tex_map_res-1);
                        uint32_t cu = std::min(static_cast<uint32_t>(ci), chunk.tex_map_res-1);
                        uint32_t cv = std::min(static_cast<uint32_t>(cj), chunk.tex_map_res-1);
                        uint32_t ti = (cu * chunk.tex_map_res + cv) * 4;
                        if (ti + 3 < chunk.texture_map.size()) {
                            rgb[0] = chunk.texture_map[ti]/255.0f;
                            rgb[1] = chunk.texture_map[ti+1]/255.0f;
                            rgb[2] = chunk.texture_map[ti+2]/255.0f;
                        }
                    } else {
                        heightGradient((y - hMin) / hRange, rgb);
                    }
                    break;
                }
                out.colors.push_back(rgb[0]); out.colors.push_back(rgb[1]); out.colors.push_back(rgb[2]);
            }
        }
    }

    return out;
}

} // namespace lu::assets
