#pragma once

#include "netdevil/zone/luz/luz_types.h"

#include <cstdint>
#include <span>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// References:
//   - DarkflameServer PR #1910 (github.com/DarkflameUniverse/DarkflameServer) — terrain math

// RAW terrain heightmap parser.
// Format verified against DarkflameServer and Ghidra RE.
//
// Binary format:
//   [u16 version] [u8 dev_flag] [u32 chunk_count] [u32 chunks_width] [u32 chunks_height]
//   Per chunk:
//     [u32 chunk_id] [u32 width] [u32 height] [f32 offset_x] [f32 offset_z]
//     if version < 32: [u32 shader_id]
//     [u32 texture_ids[4]] — terrain layer texture indices
//     [f32 scale]
//     [f32 heightmap[width*height]]
//     if version >= 32: [u32 color_map_res] else: color_map_res = width
//     [u8 color_map[color_map_res^2 * 4]] — RGBA per texel
//     [u32 light_map_size] [u8 light_map[light_map_size]]
//     if version >= 32:
//       [u32 tex_map_res] [u8 texture_map[tex_map_res^2 * 4]]
//       [u8 texture_settings]
//       [u32 blend_map_size] [u8 blend_map[blend_map_size]] — DDS data
//     [u32 flair_count] [TerrainFlair flairs[flair_count]]
//     if version >= 32: [u8 scene_map[color_map_res^2]]

struct TerrainError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Terrain decoration object (36 bytes per flair)
struct TerrainFlair {
    uint32_t id = 0;
    float scale = 1.0f;
    Vec3 position;
    Vec3 rotation;          // Euler angles (radians)
    uint8_t r = 255, g = 255, b = 255, a = 255; // RGBA color
};

struct TerrainChunk {
    uint32_t chunk_id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    float offset_x = 0;
    float offset_z = 0;
    uint32_t shader_id = 0;        // Version < 32 only
    uint32_t texture_ids[4] = {};   // Terrain layer texture indices
    float scale = 1.0f;

    std::vector<float> heightmap;       // width * height floats
    uint32_t color_map_res = 0;
    std::vector<uint8_t> color_map;     // color_map_res^2 * 4 bytes (RGBA)
    std::vector<uint8_t> light_map;     // Variable size baked lighting data
    uint32_t tex_map_res = 0;           // Version >= 32
    std::vector<uint8_t> texture_map;   // tex_map_res^2 * 4 bytes (RGBA)
    uint8_t texture_settings = 0;       // Version >= 32
    std::vector<uint8_t> blend_map;     // DDS blend map for texture splatting
    std::vector<TerrainFlair> flairs;   // Decorations
    std::vector<uint8_t> scene_map;     // Per-texel scene ID (version >= 32)
};

struct TerrainFile {
    uint16_t version = 0;
    uint8_t dev_flag = 0;           // Development/debug flag byte
    uint32_t chunk_count = 0;
    uint32_t chunks_width = 0;
    uint32_t chunks_height = 0;
    std::vector<TerrainChunk> chunks;
};
} // namespace lu::assets
