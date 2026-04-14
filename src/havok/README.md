# HKX (Havok Physics) Format

**Extension:** `.hkx`
**Used by:** The LEGO Universe client for physics collision data, rigid bodies, and scene geometry

## Overview

HKX files contain Havok physics data in three possible formats: binary packfile, tagged binary, and XML. Each starts with a distinct magic signature. The LEGO Universe client ships HKX files in multiple Havok versions. HKX files store rigid body definitions, collision shapes (boxes, capsules, convex hulls, compressed meshes), physics systems, and optional scene graph data.

## Binary Layout

### Binary Packfile Format

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       magic_0 — 0x57E0E057
0x04    4     u32       magic_1 — 0x10C0C010
0x08    ...             Havok version string, pointer size, sections...
```

### Tagged Binary Format

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       magic_0 — 0xCAB00D1E
0x04    4     u32       magic_1 — 0xD011FACE
0x08    ...             Tagged data sections...
```

### Key Data Structures

**hkpRigidBody**: position, rotation, mass, friction, restitution, collision shape reference, motion state, material info.

**Shape Types**:
- Box (hkpBoxShape): halfExtents vector
- Sphere (hkpSphereShape): radius
- Capsule (hkpCapsuleShape): vertexA, vertexB, radius
- Cylinder (hkpCylinderShape): vertexA, vertexB, cylRadius
- ConvexVertices (hkpConvexVerticesShape): AABB, rotated FourTransposedPoints, plane equations
- CompressedMesh (hkpCompressedMeshShape): quantized chunks, big triangles, convex pieces
- List (hkpListShape): child shapes with per-child collision filter info
- Mopp (hkpMoppBvTreeShape): MOPP code for BVH acceleration
- ConvexTransform/ConvexTranslate: shape with additional transform

**hkpCompressedMeshShape** (Havok 5.5):
- Chunks (80 bytes each): quantized vertices + triangle indices + offset for dequantization
- BigTriangles (16 bytes each): full-precision triangle indices into bigVertices
- ConvexPieces (64 bytes each): quantized convex hulls with face topology
- Dequantization: `world_pos = chunk.offset + float3(quantized_u16) * error`

## Versions

The LEGO Universe client ships HKX files in multiple Havok versions:

| Havok Version | Packfile Version | Contents Version String |
|--------------|-----------------|------------------------|
| **Havok 4.5** | 4 | `Havok-4.5.0-` |
| **Havok 5.1** | 5 | `Havok-5.1.0-` |
| **Havok 6.5** | 6 | `Havok-6.5.0-` |
| **Havok 7.0** | 7 | `Havok-7.0.0-` |
| **Havok 7.1** | 7 | `Havok-7.1.0-` |
| **Tagged binary** | — | (type metadata inline) |

The parser supports **packfile versions 4-11** and handles version-conditional field offsets:
- `hkpConvexVerticesShape` plane equations offset differs between v5 (0x60) and v7 (0x70)
- `hkpConvexVerticesShape` connectivity offset differs between v5 (0x70) and v7 (0x80)

The **tagged binary format** (magic 0xCAB00D1E) encodes type metadata inline — used by Havok 2010+.

The **XML format** (`<hkpackfile>`) is human-readable and version-independent.

The HKX writer outputs **Havok 7.1.0** format by default (packfile version 7).

## Key Details

- Little-endian byte order (for LU's x86 platform)
- Three format variants detected by magic bytes: binary packfile (0x57E0E057), tagged (0xCAB00D1E), and XML (`<?xml`/`<hkpackfile>`)
- 32-bit pointers in all LU client HKX files
- Sections contain class entries (name + signature), fixup tables, and serialized object data
- Virtual fixups map data offsets to class names for type identification

## References

- HKXDocs (github.com/SimonNitzsche/HKXDocs) — HKX format documentation
- Ghidra RE of legouniverse.exe — Havok physics integration
