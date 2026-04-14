# RAW Terrain Heightmap Format

**Extension:** `.raw`
**Used by:** The LEGO Universe client for terrain geometry, texturing, and decoration placement in zones

## Overview

RAW terrain files contain chunked heightmap data with per-chunk texture layers, color maps, light maps, blend maps, and decoration placements (flairs). The terrain is divided into a grid of chunks, each with its own heightmap and texture data.

## Binary Layout

### File Header

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    2     u16       version
0x02    1     u8        dev_flag — development/debug flag
0x03    4     u32       chunk_count
0x07    4     u32       chunks_width — grid width in chunks
0x0B    4     u32       chunks_height — grid height in chunks
```

### Per Chunk

```
Offset  Size      Type        Field
------  ----      ----        -----
+0x00   4         u32         chunk_id
+0x04   4         u32         width — heightmap width in samples
+0x08   4         u32         height — heightmap height in samples
+0x0C   4         f32         offset_x — chunk world X offset
+0x10   4         f32         offset_z — chunk world Z offset
(version < 32):
+0x14   4         u32         shader_id
+var    16        u32[4]      texture_ids — terrain layer texture indices
+var    4         f32         scale
+var    W*H*4     f32[]       heightmap — width * height float values
(version >= 32):
+var    4         u32         color_map_res
(version < 32): color_map_res = width
+var    R*R*4     u8[]        color_map — RGBA per texel (R = color_map_res)
+var    4         u32         light_map_size
+var    N         u8[N]       light_map — baked lighting data
(version >= 32):
+var    4         u32         tex_map_res
+var    T*T*4     u8[]        texture_map — RGBA per texel (T = tex_map_res)
+var    1         u8          texture_settings
+var    4         u32         blend_map_size
+var    B         u8[B]       blend_map — DDS texture splatting data
+var    4         u32         flair_count
+var    36*F      struct[]    flairs — decoration objects (F = flair_count)
(version >= 32):
+var    R*R       u8[]        scene_map — per-texel scene ID
```

### TerrainFlair (36 bytes)

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       id
0x04    4     f32       scale
0x08    12    f32[3]    position (x, y, z)
0x14    12    f32[3]    rotation — Euler angles (radians)
0x20    4     u8[4]     color — RGBA
```

## Version

Terrain files have a u16 version field at offset 0x00. Version 32 is a major threshold that changes the per-chunk layout:

| Version | Layout differences |
|---------|-------------------|
| < 32 | `shader_id` and `texture_ids[4]` present; `color_map_res` = chunk width |
| >= 32 | Separate `color_map_res`, `tex_map_res`, `texture_settings`, `blend_map`, and `scene_map` per chunk |

All shipped LU client terrain files use version >= 32 with the extended chunk format.

## Key Details

- Little-endian byte order
- No magic number; version field is the first 2 bytes
- Heightmap values are raw float elevations
- Color maps are RGBA at configurable resolution
- Blend maps contain embedded DDS data for texture splatting
- Flairs are terrain decorations (rocks, plants, etc.) with transform and tint
- Version 32+ adds separate texture map, blend map, and scene map per chunk

## References

- DarkflameServer PR #1910 terrain math (github.com/DarkflameUniverse/DarkflameServer)
- Ghidra RE of legouniverse.exe
