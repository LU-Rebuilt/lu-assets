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
//   Per chunk (verified against legouniverse.exe 1.10.64's RAWReadChunk/RAWReadColorand-
//   LightMaps/sub_0103aaf0(texture+blend map)/RAWReadFlairData/RAWReadSceneMap/
//   RAWReadMeshData via Ghidra RE — all version gates below are as decompiled, not
//   inferred):
//     [u32 chunk_id] [u32 width] [u32 height] [f32 offset_x] [f32 offset_z]
//     if version < 32: [u32 shader_id]
//     [u32 texture_ids[4]] — terrain layer texture indices
//     [f32 scale]
//     [f32 heightmap[width*height]]
//     if version >= 32: [u32 color_map_res] [u8 color_map[color_map_res^2*4]] (RGBA)
//     else: [u8 color_map_full[width^2*4]] (BGRA; client stores only inner
//           (width-1)^2, byte-swizzled — see color_map vs color_map_full)
//     if version >= 32: [u32 light_map_size] [u8 light_map[light_map_size]]
//     [u32 tex_map_res] [u8 texture_map[tex_map_res^2*4]] — BGRA-swizzled for v<32,
//       RGBA direct for v>=32; texturemapsize is the read/store size directly, no crop
//     if version >= 32: [u8 texture_settings] [u32 blend_map_size] [u8 blend_map[...]]
//     [u32 flair_count] [TerrainFlair flairs[flair_count]]
//     Scene map (RAWReadSceneMap) — one real on-disk byte per texel in every version;
//     the client's in-memory scene_map array is just smaller than the disk data for
//     version <= 31, not the disk data itself being absent or zero:
//       version < 31:  [u8 scene_map_disk_byte] — ONE on-disk byte per chunk (not a
//                      per-texel map yet at this version); the client discards it and
//                      allocates its own zeroed (color_map_res+1)^2 scene_map in memory.
//                      Preserved verbatim in scene_map_skip_byte.
//       version == 31: [u8 scene_map_full[(color_map_res+1)^2]] — full on-disk grid, all
//                      real texel bytes; client's in-memory view keeps only the inner
//                      color_map_res^2 (border row/col cropped, like the color map).
//                      Preserved in full in scene_map_full; scene_map is the cropped view.
//       version >= 32: [u8 scene_map[color_map_res^2]] — on-disk size matches the
//                      in-memory view exactly, no crop.
//     if version >= 32: mesh LOD section (vert usage/sizes/tris)

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

    // Axis mapping (from Ghidra RE of FUN_010699b0, terrain normal computation):
    //   Outer loop iterates height (rows = Z), inner loop iterates width (cols = X)
    //   Heightmap index = z_row * width + x_col
    //   Normal X/Z are swapped at the end, confirming width=X, height=Z
    uint32_t width = 0;     // X dimension (columns)
    uint32_t height = 0;    // Z dimension (rows)
    float offset_x = 0;     // world-space X origin
    float offset_z = 0;     // world-space Z origin
    uint32_t shader_id = 0;        // Version < 32 only
    uint32_t texture_ids[4] = {};   // Terrain layer texture indices
    float scale = 1.0f;

    std::vector<float> heightmap;       // height * width floats (Z_rows * X_cols)
    uint32_t color_map_res = 0;         // version < 32: width-1, version >= 32: read from file
    std::vector<uint8_t> color_map;     // color_map_res^2 * 4 bytes (RGBA)
    std::vector<uint8_t> light_map;     // Variable size baked lighting data
    uint32_t tex_map_res = 0;           // Version >= 32
    std::vector<uint8_t> texture_map;   // tex_map_res^2 * 4 bytes (RGBA)
    uint8_t texture_settings = 0;       // Version >= 32
    std::vector<uint8_t> blend_map;     // DDS blend map for texture splatting
    std::vector<TerrainFlair> flairs;   // Decorations
    std::vector<uint8_t> scene_map;     // Per-texel scene ID; cropped to color_map_res^2
                                         // for version <= 31 same as color_map is
    // version < 31 only: the one real on-disk byte per chunk that predates the per-texel
    // scene map (RAWReadSceneMap reads it, then discards it and builds its own zeroed
    // in-memory scene_map instead of using it). Preserved verbatim — not junk, just data
    // this particular client version doesn't consume — so terrain_write reproduces it.
    uint8_t scene_map_skip_byte = 0;
    // version == 31 only: the full on-disk (color_map_res+1)^2 scene map — every byte
    // real, including the border row/column the client's in-memory view crops away (same
    // pattern as color_map vs color_map_full). Preserved verbatim for byte-identical
    // round-trips; scene_map (above) is the client's cropped in-memory view.
    std::vector<uint8_t> scene_map_full;

    // Version < 32 only: the color map exactly as stored on disk — width*width texels of
    // BGRA. `color_map` above is the client's in-memory view (swizzled to RGBA and cropped
    // to (width-1)^2, per RAWReadColorandLightMaps); this keeps the texels that crop
    // discards so terrain_write can round-trip byte-identically.
    std::vector<uint8_t> color_map_full;

    // Trailing per-chunk mesh LOD section (present in all versions when vert_count > 0).
    // Field names from the client's RAW reader RE: meshVertUsage, meshVertSize, meshTri.
    uint32_t mesh_vert_count = 0;              // "vertSize" in the client
    std::vector<uint16_t> mesh_vert_usage;     // [mesh_vert_count]
    uint16_t mesh_vert_size[16] = {};          // per-LOD vertex counts
    std::vector<uint16_t> mesh_tris[16];       // per-LOD triangle index lists
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
