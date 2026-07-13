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

Phased effort. Current phase: **NIF → USD export**.

| Phase | Scope | State |
|-------|-------|-------|
| 1 | NIF → USD (mesh, materials, textures, vertex colors) | **done** |
| 2 | USD → NIF import (client-accepts fidelity bar) | planned |
| 3 | KF/ETK animation (UsdSkel) + HKX collision | planned |

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

## OpenUSD resolution

`find_package(pxr)` uses a system OpenUSD when present (fast); otherwise a
minimal monolithic OpenUSD is built from source via FetchContent (slow — the
system path is strongly preferred).

Some system OpenUSD packages leak optional-plugin dependencies (Alembic/HDF5,
OpenSubdiv/CUDA) into the propagated link interface — plugins loaded at runtime,
not needed to link a program that only *writes* USD. The CMake scrubs
unresolvable link-interface entries from the imported targets so authoring works
without those optional back-ends installed.
