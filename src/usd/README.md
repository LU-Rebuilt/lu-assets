# usd — Universal Scene Description translation layer

Bridges LU's native asset formats to [OpenUSD](https://openusd.org) for
content authoring in DCC tools (Blender, Houdini, Maya, `usdview`). It sits
**on top of** the existing format readers/extractors in this library — it does
not re-parse raw blocks.

This is an **opt-in** component: it pulls in OpenUSD, a large dependency most
consumers of the core `lu_assets` library don't need. It builds as a separate
`lu_assets_usd` target, gated behind the `LU_ASSETS_BUILD_USD` CMake option
(OFF by default). The core `lu_assets` library never depends on OpenUSD.

## Status

Phased effort.

| Phase | Scope | State |
|-------|-------|-------|
| 1 | NIF → USD (mesh, materials, textures, vertex colors) | **done** |
| 2 | USD → NIF import (client-accepts fidelity bar) | **done** |
| 3a | KF animation → UsdSkel | **done** |
| 3b | HKX collision → USD | **done** |
| 4 | CDClient object composition (gather a LOT's NIF + textures + anims + physics into one stage, and back) | planned |

## `nif_to_usd`

`nif_to_usd(nif, out_path, source_name, options)` writes a `.usda` (text) or
`.usdc` (binary crate) stage from a parsed `NifFile`. Built on
`extractNifRenderGeometry` (see `gamebryo/nif/nif_geometry.h`), so meshes come
out with parent transforms, materials and skin metadata already resolved.

Exported per render mesh:

- **Geometry** — `UsdGeomMesh`, world space, triangulated. Points, per-vertex
  normals, first UV set as the `st` primvar (V flipped: NIF/D3D top-left origin
  → USD bottom-left), authored vertex colors as `displayColor`/`displayOpacity`.
  Declared `leftHanded` so NIF's clockwise winding front-faces correctly in
  USD's right-handed viewers.
- **Material** — a `UsdPreviewSurface` from the `NiMaterialProperty` colors
  (diffuse/emissive/opacity; specular approximated into roughness since
  UsdPreviewSurface is metal-rough). The diffuse texture is wired through a
  `UsdUVTexture` + `UsdPrimvarReader_float2`; the texture path is preserved
  verbatim as an asset reference (a relative client path — resolution is left
  to whatever opens the stage, matching the client).
- **LU-specific data** as `lu:*` custom attributes (source block indices, LOD
  near/far bands, skin bone names) — data USD has no native slot for, kept so a
  future USD→NIF importer can reconstruct it.

Stage metadata records Z-up (NIF/Gamebryo convention), meters, and the source
filename. `NifToUsdOptions::convert_to_y_up` instead bakes a -90° X rotation on
the root so the model reads correctly in Y-up-defaulting tools.

Output validates clean under `usdchecker` (the only diagnostic is an expected
unresolved-texture warning when the referenced `.dds` isn't co-located with the
stage).

## `usd_to_nif`

`usd_to_nif(usd_path, options)` reads a USD stage and returns a `NifFile` (write
it with `nif_write`). The inverse of `nif_to_usd`, with a **semantic** fidelity
bar — a valid NIF the client loads and draws the same, not byte-identity (USD
does not carry Gamebryo's exact block layout or optional fields).

It walks every `UsdGeomMesh`, resolving each prim's world transform, and hand-
builds a minimal valid NIF for version 20.3.0.9:

```
NiNode "Scene Root"
  └─ per mesh: NiTriShape → NiTriShapeData
               NiMaterialProperty
               [NiTexturingProperty → NiSourceTexture]   (when textured)
```

Per mesh it imports positions, per-vertex normals, the `st` UV set (undoing the
export V-flip), and vertex colors; the bound `UsdPreviewSurface`'s constant
inputs become the `NiMaterialProperty` colors, and a connected `UsdUVTexture`'s
file path becomes a `NiSourceTexture`. Per-mesh transforms are baked into
vertices, so the flat scene uses identity node transforms. A Y-up stage is
rotated back to NIF's Z-up.

The hand-emitted block byte layouts match how the reader (verified against
`nif.xml`) parses each block at 20.3.0.9 — notably: `NiProperty::Flags` is
**absent** on `NiMaterialProperty` at this version (present only ≤ 10.0.0.0),
and `NiTexturingProperty` ends with an unconditional `Num Shader Textures` u32
after the (absent-here) decal slots.

Not yet imported: skeletons/skinning, animation, collision, LOD nodes, and
multi-level hierarchy (a flat root-node scene is emitted). Round-trip verified
on the client corpus: geometry (vertex/triangle counts, positions, UVs),
material colours and texture paths survive NIF → USD → NIF.

## `kf_to_usd`

`kf_to_usd(kf, out_path, source_name, options)` exports a NIF/KF file's
animation clips to a UsdSkel stage. LU KF files parse through the same NIF
container (`nif_parse`); each holds one or more `NiControllerSequence` clips that
target scene nodes *by name* with per-node `NiTransformInterpolator` →
`NiTransformData` translation/rotation/scale keyframes.

Mapping:

```
SkelRoot "/anim"
  ├─ Skeleton        (flat joint set = union of targeted node names)
  ├─ SkelAnimation "anim_<clip0>"
  ├─ SkelAnimation "anim_<clip1>"
  └─ …
```

Each clip becomes a `UsdSkelAnimation` with per-joint translate/rotate/scale
time samples, sampled at the union of the clip's key times; timing maps to stage
start/end time codes at `timeCodesPerSecond` (default 30). Rotation quats are
reordered to USD's (w,x,y,z); the shortest-arc nlerp is used between keys. A KF
clip carries target names, not a bind hierarchy, so the skeleton is flat with
identity bind/rest transforms — enough for playback; a real bind pose comes from
the paired model (composed together in the CDClient object phase).

Multiple clips land in one stage (all `SkelAnimation`s under one skeleton), so a
model's whole animation set exports to a single container. Verified on the
client corpus: 120/120 sampled KF files export and validate under `usdchecker`.

## `hkx_to_usd`

`hkx_to_usd(hkx_path, out_path, source_name, options)` exports an HKX file's
collision shapes to `UsdGeomMesh` prims under a `/collision` scope, authored
with `purpose = "guide"` so DCC tools treat them as collision helpers rather
than render geometry. The original Havok shape class is preserved as a
`lu:hkxShapeType` attribute.

Geometry comes from the shared `Hkx::extractGeometry` pipeline (which resolves
rigid-body + scene-node transforms to world space). As part of this phase that
extractor gained convex-hull tessellation: `hkpConvexVerticesShape` stores its
vertices SIMD-transposed with no faces, so the hull is computed with an
incremental 3D convex hull — this is the dominant LU collision shape, so it
lifts coverage from ~94% (Mopp/scene-mesh only) to ~99% of corpus files.

Verified on the client corpus: 148/151 sampled HKX files export collision
geometry, all `usdchecker`-valid (the remainder have no tessellable shape).

## OpenUSD resolution

`find_package(pxr)` uses a system OpenUSD when present (fast); otherwise a
minimal monolithic OpenUSD is built from source via FetchContent (slow — the
system path is strongly preferred).

Some system OpenUSD packages leak optional-plugin dependencies (Alembic/HDF5,
OpenSubdiv/CUDA) into the propagated link interface — plugins loaded at runtime,
not needed to link a program that only *writes* USD. The CMake scrubs
unresolvable link-interface entries from the imported targets so authoring works
without those optional back-ends installed.
