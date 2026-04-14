# NIF (Gamebryo/NetImmerse) Format

**Extension:** `.nif` (meshes/scenes), `.kf` (animation sequences)
**Used by:** The LEGO Universe client for all 3D models, scene hierarchies, materials, and skeletal animations

## Overview

NIF is Gamebryo's binary scene graph format. LU uses version 20.3.0.9 with user_version 12 and user_version_2 34. Files contain a block-based structure where each block is a typed object (node, mesh, material, controller, etc.) referenced by index.

## Binary Layout

### File Header

```
Offset  Size    Type        Field
------  ----    ----        -----
0x00    var     ASCII       text_header — ";Gamebryo File Format, Version X.X.X.X\n"
+0x00   4       u32         version — binary version (0x14030009 for LU)
+0x04   1       u8          endian_flag — 1 = little-endian
+0x05   4       u32         user_version (12 for LU)
+0x09   4       u32         num_blocks — total block count
+0x0D   4       u32         user_version_2 (34 for LU, if user_version >= 10)
+var    3*var   u8+str[3]   export_info — 3 u8-length-prefixed strings (if user_version >= 3)
+var    2       u16         num_block_types
+var    var     u32+str[]   block_type_names — u32-length-prefixed strings
+var    2*N     u16[N]      block_type_indices — per-block type index (N = num_blocks)
+var    4*N     u32[N]      block_sizes — per-block byte sizes (version >= 20.2.0.7)
+var    4       u32         num_strings — string table count
+var    4       u32         max_string_length
+var    var     u32+str[]   string_table — u32-length-prefixed entries
+var    4       u32         num_groups
+var    4*G     u32[G]      group_sizes
```

### NiAVObject (base for scene nodes)

```
--- NiObjectNET ---
i32   name_idx — string table index (-1 = none)
u32   num_extra_data
i32[] extra_data_refs — block indices
i32   controller_ref — block index (-1 = none)

--- NiAVObject ---
u16   flags (ALWAYS u16, not u32)
f32[3] translation
f32[9] rotation — 3x3 matrix, row-major
f32   scale
u32   num_properties
i32[] property_refs
i32   collision_ref
```

### NiTriShapeData (geometry)

```
u32   group_id (version >= 20.3.0.9)
u16   num_vertices
u16   keep_flags
bool  has_vertices
f32[3]*N  vertices (if has_vertices)
u16   vector_flags — low 6 bits = UV set count, bit 12 = tangent space
bool  has_normals
f32[3]*N  normals (if has_normals)
f32[3]*N  tangents (if has_normals && tangent_space)
f32[3]*N  bitangents (if has_normals && tangent_space)
f32[3] bound_center
f32   bound_radius
bool  has_vertex_colors
f32[4]*N  vertex_colors RGBA (if has_vertex_colors)
f32[2]*S*N  UV coords per set per vertex
u16   consistency_flags
i32   additional_data_ref
u16   num_triangles
u32   num_triangle_points
bool  has_triangles
u16[3]*T  triangles — index triples (if has_triangles)
```

### Block Type Frequency (across 10,051 client meshes)

```
165,173  NiNode                  63,104  NiTriShape
 63,104  NiTriShapeData          29,072  NiMaterialProperty
 25,676  NiVertexColorProperty   22,173  NiTriStrips/Data
 15,879  NiTransformController   15,547  NiCamera
 12,430  NiTransformData         12,286  NiLODNode
```

## Key Details

- Little-endian byte order (endian_flag = 1)
- LU target version: 20.3.0.9 (0x14030009), user_version 12, user_version_2 34
- NiAVObject flags field is ALWAYS u16, not u32
- All block references are by index (-1 = none/null)
- .kf files use the same NIF format but contain NiControllerSequence animation blocks
- String table provides shared name storage; blocks reference strings by index

## References

- nif.xml (github.com/niftools/nifxml) — NIF block type and field definitions
- NifSkope (github.com/niftools/nifskope) — format validation and visual inspection
- Ghidra RE of legouniverse.exe
