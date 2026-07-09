# Brick Geometry (.g) Format

**Extension:** `.g`, `.g1`, `.g2`
**Used by:** LEGO Universe client for brick primitive meshes at multiple LOD levels

## Overview

Each `.g` file contains the geometry for a single LEGO brick primitive — vertex positions, optional normals/UVs, triangle indices, and a handful of optional trailing blocks (skin weights, extra vertex data, an opaque tag). The client stores geometry files across 3 LOD levels in `res/brickprimitives/lod0/`, `lod1/`, and `lod2/`.

File naming: `<designID>.g` (LOD 0), `<designID>.g1` (LOD 1), `<designID>.g2` (LOD 2).

## Binary Layout

```
Offset  Size            Type        Field
------  ----            ----        -----
0x00    4               u32         magic — 0x42473031 ("BG01")
0x04    4               u32         vertex_count
0x08    4               u32         index_count (triangles = index_count / 3)
0x0C    4               u32         options — independent bitflags (each gates its own
                                        block; NOT combined like (options & 3) == 3):
                                        0x01 has UVs, 0x02 has normals, 0x04 has an extra
                                        vertex-data block, 0x08 has a trailing opaque tag
                                        block, 0x10 has a skin-weight-index block, 0x20
                                        has a skin-weight (Vec3) block
0x10    vertex_count*12 f32[3]      vertex positions (x, y, z) — always present
+       vertex_count*12 f32[3]      vertex normals (nx, ny, nz) — only if 0x02
+       vertex_count*8  f32[2]      texture coordinates (u, v) — only if 0x01
+       index_count*4   u32         triangle indices (groups of 3) — always present
+       variable        —           skin-weight-index block — only if 0x10 (see below)
+       variable        —           skin-weight block — only if 0x20 (see below)
+       variable        —           extra vertex-data block — only if 0x04 (see below)
+       variable        —           opaque tag block — only if 0x08 (see below)
```

**Skin-weight-index block (0x10):**
```
u32       count
count*u32 weight_group_indices
index_count*u32 index_mapping   -- keyed by triangle-index position, not vertex index
```

**Skin-weight block (0x20):**
```
u32       count
count*f32[3] weights (Vec3-valued, not simple indices)
index_count*u32 index_mapping
```

**Extra vertex-data block (0x04):**
```
u32       size
size bytes extra_data (opaque)
vertex_count*u32 remap
```

**Opaque tag block (0x08):**
```
u32       tag
u32       size
size bytes payload (opaque)
```

## Version

No versioning — single format, identified by magic `0x42473031`.

## Key Details

- Little-endian byte order
- Magic: `0x42473031` = "BG01" (1111961649) — NOT `0x42420D31`, a value that appears in
  some older community documentation but does not match any real file
- Option bits are independent, not combined — `0x02` (normals) is set in effectively
  every real file; `0x01` (UVs) is set in only a small minority, contrary to older
  documentation which conflated the two under a single `(options & 3) == 3` check
- Verified via Ghidra RE of `legouniverse.exe` (`LEGO::BrickGeometry::vfunc3` at
  `0x00982cf0`, and its symmetric writer/allocator siblings) plus byte-exact
  verification against all 5934 real `.g`/`.g1`/`.g2` files under `res/brickprimitives/`
- The 0x10/0x20 skin-weight blocks' `index_mapping` arrays are sized to `index_count`
  (one entry per triangle index), not `vertex_count` — there is no direct per-vertex
  bone-index array in the on-disk format. The exact runtime consumption semantics
  (how `weight_group_indices`/`weights` map to bone transforms) could not be confirmed
  against a Ghidra-reachable consumer function; `brick_assembly.cpp` implements a
  best-supported reading of the 0x10 block only (single bone per vertex via the
  triangle-index-keyed mapping) and does not apply the 0x20 block's Vec3 weights
- Positions are in local brick space — assembly applies LXFML transforms
- LOD 0 = highest detail, LOD 2 = simplified
- `brick_geometry_write()` round-trips byte-identically against every real file checked
  (11784/11784 across two client trees)

## References

- [lu-toolbox](https://github.com/Squareville/lu-toolbox) — Blender .g import reference
- [lcdr/lu_formats](https://github.com/lcdr/lu_formats) g.ksy — Kaitai Struct specification (has the wrong magic/option-bit semantics; superseded by the Ghidra RE above)
