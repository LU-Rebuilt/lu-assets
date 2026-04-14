# LVL (Scene/Level) Format

**Extension:** `.lvl`
**Used by:** The LEGO Universe client to define individual scenes within a zone, containing objects, environment settings, and particle placements

## Overview

LVL files use a chunked binary format where each section begins with a 20-byte "CHNK" header. A zone is composed of multiple LVL scenes referenced by the parent LUZ file. Each LVL contains environment lighting, placed objects with LDF configs, and particle system placements.

## Binary Layout

### CHNK Header (20 bytes, repeated per chunk)

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       magic — 0x4B4E4843 ("CHNK")
0x04    4     u32       chunk_id (1000, 2000, 2001, or 2002)
0x08    2     u16       header_version
0x0A    2     u16       data_version
0x0C    4     u32       total_size — full chunk size INCLUDING this 20-byte header
0x10    4     u32       data_offset — absolute file offset where chunk payload begins
```

Next chunk starts at: chunk_position + total_size.

### Chunk 1000 — File Info Block (FIB)

Contains file version and revision. Used to determine field presence in other chunks.

### Chunk 2000 — Environment

```
Lighting info:
  f32 blend_time (version >= 45)
  f32[3] ambient, f32[3] specular, f32[3] upper_hemisphere
  f32[3] light_position
  Draw distances (version >= 39): fog_near, fog_far, post_fog, static/dynamic obj dist
  Cull values (version >= 40): per-group min/max distances
  f32[3] fog_color (version >= 31)
  f32[3] directional_light (version >= 36)

Skydome info:
  u4_str filename — main skydome NIF path
  u4_str sky_layer_filename (version >= 34)
  u4_str ring_layer[4] — ring/cloud layer NIFs

Editor settings (version >= 37):
  Saved color palette entries (f32 r, g, b each)
```

### Chunk 2001 — Objects

```
Per object:
  u64     object_id
  u32     lot — LEGO Object Template ID
  u32     node_type (version >= 38, see LvlNodeType enum)
  u32     glom_id (version >= 32)
  f32[3]  position (x, y, z)
  f32[4]  rotation (w, x, y, z — stored WXYZ in file, converted to XYZW)
  f32     scale
  u32     config_char_count
  u16[N]  config_string — UTF-16LE, parsed as text LDF (key=type:value pairs)
  Render technique (version >= 7):
    char[64] technique_name
    Per attribute:
      char[64] name, u32 num_floats, bool is_color, f32[4] values
```

### Chunk 2002 — Particles

```
Per particle:
  u16     priority (version >= 43)
  f32[3]  position
  f32[4]  rotation (w, x, y, z — WXYZ in file)
  u4_wstr effect_names — semicolon-separated PSB paths
  u4_wstr config — optional LDF config string
```

## Key Details

- Little-endian byte order
- Magic: `0x4B4E4843` ("CHNK") at the start of every chunk
- Chunks can appear in any order; missing chunks are valid
- Object config strings are UTF-16LE encoded, decoded to ASCII LDF
- Rotation quaternions are stored as WXYZ in the file but typically used as XYZW
- Verified against legouniverse.exe: ReadLvlObjectData @ 0103ba20

## References

- lcdr/lu_formats lvl.ksy (github.com/lcdr/lu_formats)
- DarkflameServer Zone.cpp LoadLevel() (github.com/DarkflameUniverse/DarkflameServer)
