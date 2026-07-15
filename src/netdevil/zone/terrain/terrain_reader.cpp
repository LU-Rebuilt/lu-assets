#include "netdevil/zone/terrain/terrain_reader.h"
#include "common/binary_reader/binary_reader.h"

#include <algorithm>

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
        // Ghidra RE (RAWReadColorandLightMaps @ 0102e3e0):
        //   version < 32: colorMapResolution = width - 1
        //   version >= 32: colorMapResolution = read_u32()
        if (terrain.version >= 32) {
            chunk.color_map_res = r.read_u32();
        } else {
            chunk.color_map_res = (chunk.width > 0) ? chunk.width - 1 : 0;
        }

        // Color map: 4 bytes per texel
        // version < 32: stored as BGRA in file, client swizzles to RGBA
        //   (Ghidra: R=byte[2], G=byte[1], B=byte[0], A=byte[3])
        //   Also reads width*width entries but only stores (width-1)^2
        // version >= 32: stored as RGBA, direct read of colorMapRes^2 entries
        if (terrain.version < 32) {
            // Read width*width BGRA entries. Keep the on-disk bytes verbatim in
            // color_map_full (for terrain_write round-trips), and build the client's
            // in-memory view in color_map: swizzled to RGBA, cropped to (width-1)^2.
            size_t read_count = static_cast<size_t>(chunk.width) * chunk.width;
            size_t store_res = chunk.color_map_res; // width - 1
            if (r.remaining() >= read_count * 4) {
                auto raw = r.read_bytes(read_count * 4);
                chunk.color_map_full.assign(raw.begin(), raw.end());
                if (store_res > 0 && store_res <= 4096) {
                    chunk.color_map.resize(store_res * store_res * 4);
                    size_t store_idx = 0;
                    for (uint32_t y = 0; y < store_res; ++y) {
                        for (uint32_t x = 0; x < store_res; ++x) {
                            size_t src = (static_cast<size_t>(y) * chunk.width + x) * 4;
                            chunk.color_map[store_idx + 0] = chunk.color_map_full[src + 2]; // R
                            chunk.color_map[store_idx + 1] = chunk.color_map_full[src + 1]; // G
                            chunk.color_map[store_idx + 2] = chunk.color_map_full[src + 0]; // B
                            chunk.color_map[store_idx + 3] = chunk.color_map_full[src + 3]; // A
                            store_idx += 4;
                        }
                    }
                }
            }
        } else if (chunk.color_map_res > 0 && chunk.color_map_res <= 4096) {
            size_t cm_bytes = static_cast<size_t>(chunk.color_map_res) * chunk.color_map_res * 4;
            if (r.remaining() >= cm_bytes) {
                auto cm = r.read_bytes(cm_bytes);
                chunk.color_map.assign(cm.begin(), cm.end());
            }
        }

        // Light map: u32 size + raw data (baked lighting)
        // Light map: only present for version >= 32 (RAWReadColorandLightMaps gates the
        // whole size+data read on `31 < version`; Ghidra RE of legouniverse.exe 1.10.64).
        if (terrain.version >= 32 && r.remaining() >= 4) {
            uint32_t lm_size = r.read_u32();
            if (lm_size > 0 && lm_size <= 64 * 1024 * 1024 && r.remaining() >= lm_size) {
                auto lm = r.read_bytes(lm_size);
                chunk.light_map.assign(lm.begin(), lm.end());
            }
        }

        // Texture map: present for every version (RAWReadChunk's next call, sub_0103aaf0 in
        // Ghidra). v<32 uses the same width*width-read/(width-1)^2-store BGRA swizzle as the
        // color map, with no on-disk resolution field of its own — texturemapsize IS the
        // read/store width directly (unlike color_map_res, there's no -1 crop here).
        if (r.remaining() >= 4) {
            chunk.tex_map_res = r.read_u32();
            if (chunk.tex_map_res > 0 && chunk.tex_map_res <= 4096) {
                size_t tex_bytes = static_cast<size_t>(chunk.tex_map_res) * chunk.tex_map_res * 4;
                if (r.remaining() >= tex_bytes) {
                    if (terrain.version < 32) {
                        auto raw = r.read_bytes(tex_bytes);
                        chunk.texture_map.resize(tex_bytes);
                        for (size_t i = 0; i + 3 < tex_bytes; i += 4) {
                            chunk.texture_map[i + 0] = raw[i + 2]; // R
                            chunk.texture_map[i + 1] = raw[i + 1]; // G
                            chunk.texture_map[i + 2] = raw[i + 0]; // B
                            chunk.texture_map[i + 3] = raw[i + 3]; // A
                        }
                    } else {
                        auto tm = r.read_bytes(tex_bytes);
                        chunk.texture_map.assign(tm.begin(), tm.end());
                    }
                }
            }

            // Texture settings byte + blend map: version >= 32 only (sub_0103aaf0's `else`
            // branch on `version < 0x20`; v<32 has neither field on disk at all).
            if (terrain.version >= 32) {
                if (r.remaining() >= 1) {
                    chunk.texture_settings = r.read_u8();
                }
                if (r.remaining() >= 4) {
                    uint32_t blend_size = r.read_u32();
                    if (blend_size > 0 && r.remaining() >= blend_size) {
                        auto bm = r.read_bytes(blend_size);
                        chunk.blend_map.assign(bm.begin(), bm.end());
                    }
                }
            }
        }

        // Flairs: terrain decoration objects (RAWReadFlairData, present in all versions).
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

        // Scene map: per-texel scene ID (RAWReadSceneMap). Every version has a real byte
        // (or grid of bytes) on disk here — nothing is ever skipped/junk on the file side,
        // only the client's in-memory scene_map differs in size/content from what's
        // stored. Three version bands (all preserved verbatim, see TerrainChunk fields):
        //   version < 31:  ONE real on-disk byte per chunk (pre-per-texel-map era); the
        //                   client reads it, discards it, and builds its own zeroed
        //                   (colorMapRes+1)^2 in-memory map instead of using it.
        //   version == 31: (colorMapRes+1)^2 real bytes on disk; the client's in-memory
        //                   view keeps only the inner colorMapRes^2 (border row/col
        //                   cropped, same pattern as the color map).
        //   version >= 32: colorMapRes^2 bytes on disk, matching the in-memory view
        //                   exactly (no crop).
        if (terrain.version < 31) {
            if (r.remaining() >= 1) chunk.scene_map_skip_byte = r.read_u8();
        } else if (terrain.version == 31) {
            uint32_t res = chunk.color_map_res;
            uint32_t full = res + 1;
            size_t full_bytes = static_cast<size_t>(full) * full;
            if (r.remaining() >= full_bytes) {
                auto raw = r.read_bytes(full_bytes);
                chunk.scene_map_full.assign(raw.begin(), raw.end());
                chunk.scene_map.reserve(static_cast<size_t>(res) * res);
                for (uint32_t y = 0; y < full; ++y) {
                    for (uint32_t x = 0; x < full; ++x) {
                        if (y < res && x < res) {
                            chunk.scene_map.push_back(chunk.scene_map_full[
                                static_cast<size_t>(y) * full + x]);
                        }
                    }
                }
            }
        } else if (chunk.color_map_res > 0) {
            size_t sm_bytes = static_cast<size_t>(chunk.color_map_res) * chunk.color_map_res;
            if (r.remaining() >= sm_bytes) {
                auto sm = r.read_bytes(sm_bytes);
                chunk.scene_map.assign(sm.begin(), sm.end());
            }
        }

        // Mesh vertex/triangle data: version >= 32 only (RAWReadMeshData gates the entire
        // section on `31 < version`; v<32 files have no mesh section on disk whatsoever).
        if (terrain.version >= 32 && r.remaining() >= 4) {
            chunk.mesh_vert_count = r.read_u32();
            if (chunk.mesh_vert_count > 0 && chunk.mesh_vert_count < 1000000) {
                // meshVertUsage[vertSize] (uint16 array)
                chunk.mesh_vert_usage.reserve(chunk.mesh_vert_count);
                for (uint32_t v = 0; v < chunk.mesh_vert_count && r.remaining() >= 2; ++v) {
                    chunk.mesh_vert_usage.push_back(r.read_u16());
                }

                // meshVertSize[16] (always 16 uint16s)
                for (int mv = 0; mv < 16 && r.remaining() >= 2; ++mv) {
                    chunk.mesh_vert_size[mv] = r.read_u16();
                }

                // meshTri[16] — each: triCount(u16) + triIndices[triCount](u16)
                for (int mt = 0; mt < 16 && r.remaining() >= 2; mt++) {
                    uint16_t triCount = r.read_u16();
                    chunk.mesh_tris[mt].reserve(triCount);
                    for (uint16_t t = 0; t < triCount && r.remaining() >= 2; ++t) {
                        chunk.mesh_tris[mt].push_back(r.read_u16());
                    }
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

        // Ghidra RE of FUN_010699b0: width = X columns, height = Z rows
        // heightmap index = z_row * width + x_col
        uint32_t w = chunk.width;  // X dimension
        uint32_t h = chunk.height; // Z dimension
        float s = chunk.scale;
        if (s <= 0) s = 1.0f;

        uint32_t baseIdx = static_cast<uint32_t>(out.vertices.size() / 3);

        // Outer = Z rows, inner = X columns (matching client iteration order)
        for (uint32_t zi = 0; zi < h; ++zi) {
            for (uint32_t xi = 0; xi < w; ++xi) {
                uint32_t idx = zi * w + xi;
                float y = (idx < chunk.heightmap.size()) ? chunk.heightmap[idx] : 0.0f;
                float x = (static_cast<float>(xi) + (chunk.offset_x / s)) * s;
                float worldY = (y / s) * s;
                float z = (static_cast<float>(zi) + (chunk.offset_z / s)) * s;
                out.vertices.push_back(x);
                out.vertices.push_back(worldY);
                out.vertices.push_back(z);
            }
        }

        // Triangle generation: stride = w (X columns per Z row)
        for (uint32_t zi = 1; zi < h; ++zi) {
            for (uint32_t xi = 1; xi < w; ++xi) {
                uint32_t tl = baseIdx + (zi - 1) * w + (xi - 1);
                uint32_t tr = baseIdx + (zi - 1) * w + xi;
                uint32_t bl = baseIdx + zi * w + (xi - 1);
                uint32_t br = baseIdx + zi * w + xi;

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
        // Ghidra RE: width = X columns, height = Z rows
        // heightmap index = z_row * width + x_col
        uint32_t w = chunk.width, h = chunk.height;

        for (uint32_t zi = 0; zi < h; ++zi) {
            for (uint32_t xi = 0; xi < w; ++xi) {
                uint32_t idx = zi * w + xi;
                float y = (idx < chunk.heightmap.size()) ? chunk.heightmap[idx] : 0.0f;
                float rgb[3] = {0.6f, 0.6f, 0.6f};

                switch (mode) {
                case TerrainColorMode::HeightGradient:
                    heightGradient((y - hMin) / hRange, rgb);
                    break;
                case TerrainColorMode::ColorMap:
                    if (!chunk.color_map.empty() && chunk.color_map_res > 0 && w > 1 && h > 1) {
                        float ci = (static_cast<float>(xi) / (w-1)) * (chunk.color_map_res-1);
                        float cj = (static_cast<float>(zi) / (h-1)) * (chunk.color_map_res-1);
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
                        float ci = (static_cast<float>(xi) / (w-1)) * (chunk.color_map_res-1);
                        float cj = (static_cast<float>(zi) / (h-1)) * (chunk.color_map_res-1);
                        uint32_t cu = std::min(static_cast<uint32_t>(ci), chunk.color_map_res-1);
                        uint32_t cv = std::min(static_cast<uint32_t>(cj), chunk.color_map_res-1);
                        uint32_t si = cu * chunk.color_map_res + cv;
                        if (si < chunk.scene_map.size()) sceneIdToColor(chunk.scene_map[si], rgb);
                    }
                    break;
                case TerrainColorMode::TextureWeights:
                    if (!chunk.texture_map.empty() && chunk.tex_map_res > 0 && w > 1 && h > 1) {
                        float ci = (static_cast<float>(xi) / (w-1)) * (chunk.tex_map_res-1);
                        float cj = (static_cast<float>(zi) / (h-1)) * (chunk.tex_map_res-1);
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
