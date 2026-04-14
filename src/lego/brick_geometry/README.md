# Brick Geometry (.g) Format

**Extension:** `.g`, `.g1`, `.g2`
**Used by:** LEGO Universe client for brick primitive meshes at multiple LOD levels

## Overview

Each `.g` file contains the geometry for a single LEGO brick primitive — vertex positions, normals, optional texture coordinates, and triangle indices. The client stores geometry files across 3 LOD levels in `res/brickprimitives/lod0/`, `lod1/`, and `lod2/`.

File naming: `<designID>.g` (LOD 0), `<designID>.g1` (LOD 1), `<designID>.g2` (LOD 2).

## Binary Layout

```
Offset  Size            Type        Field
------  ----            ----        -----
0x00    4               u32         magic — 0x42420D31
0x04    4               u32         vertex_count
0x08    4               u32         index_count (triangles = index_count / 3)
0x0C    4               u32         options — bitflags:
                                        (options & 3) == 3: has texture coordinates
                                        (options & 48) == 48: has bone weights
0x10    vertex_count*12 f32[3]      vertex positions (x, y, z)
+       vertex_count*12 f32[3]      vertex normals (nx, ny, nz)
+       vertex_count*8  f32[2]      texture coordinates (u, v) — only if has_uvs
+       index_count*4   u32         triangle indices (groups of 3)
+       variable        —           bone weight data — only if has_bones
```

## Version

No versioning — single format. Identified by magic `0x42420D31`.

## Key Details

- Little-endian byte order
- Magic: `0x42420D31` (1111961649)
- Options bitfield controls presence of UV and bone sections
- Positions are in local brick space — assembly applies LXFML transforms
- LOD 0 = highest detail, LOD 2 = simplified

## References

- [lu-toolbox](https://github.com/Squareville/lu-toolbox) — Blender .g import reference
- [lcdr/lu_formats](https://github.com/lcdr/lu_formats) g.ksy — Kaitai Struct specification
