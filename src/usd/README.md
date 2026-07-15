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
| 4 | Object composition — gather a whole object (model + all animations + collision) into one stage, driven by a KFM manifest or a CDClient LOT | **done** |

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

## `object_to_usd`

Composes a *whole object* — its render model, its complete animation set and its
collision — into one USD stage (`object_to_usd.h`). This is the payoff of the
per-file converters above: rather than juggling separate files, an author opens
the object as a unit. Two entry points produce the same composed layout:

- **`object_from_kfm(kfm_path, vfs, …)`** — driven by a KFM animation manifest,
  which names the model NIF, the scene root and every animation sequence/clip.
- **`object_from_lot(lot, cd, vfs, …)`** — driven by a CDClient LOT: its render
  component and its physics component(s).

Composed layout:

```
/object                 (SkelRoot)
  Model/                model geometry + materials        (author_nif_model)
  Skeleton              one flat skeleton over the joints
  anim_<clip> …         one UsdSkelAnimation per KF clip  (author_kf_animations)
  Collision/
    <PhysicsType>/      collision meshes per physics component (author_hkx_collision)
```

Key behaviours:

- **A KFM render asset pulls in both the NIF and the KFs.** A LOT's render
  component often points at a `.kfm` (animated actors like the minifig), not a
  `.nif`; when it does, the composer parses it as the manifest and loads the
  model it names *plus* its whole animation set. A `.nif` render asset is used
  directly, with a same-stem `.kfm` beside it consulted for animations.
- **Collision follows the object's physics component(s).** The physics-family
  component types (ControllablePhysics, SimplePhysics, ProjectilePhysics,
  RigidBodyPhantomPhysics, PhantomPhysics, VehiclePhysics, PhysicsSystem —
  RE'd `ComponentType` values) each resolve their `physics_asset` HKX; more than
  one can coexist (e.g. a phantom trigger volume plus a solid body), so each
  gets its own labelled sub-scope. The `physics_asset` is read via the generic
  query interface, not `get_physics_component()`, whose typed column list isn't
  present in every CDClient schema version.
- **Animation event keys and transitions are preserved.** A sibling `.etk`
  (`<name>.etk`) beside each KF supplies `NiExtraTextKeyExtraData` event markers,
  attached to the clip as `lu:textKeyTimes` / `lu:textKeyEvents`. The KFM
  transition graph (dest/type/duration per edge) is attached as
  `lu:transition*` metadata — USD has no native animation-transition schema.
- **File resolution** goes through a `Vfs` (packed or unpacked client). Asset
  references are normalized (backslashes → slashes, collapsed runs, `.`/`..`
  resolved relative to the referring KFM's directory); `.hkx` physics paths are
  additionally tried under `res/physics/`, where CDClient roots them.

The intermediate `author_*` helpers (`usd_compose_internal.h`) let each per-file
exporter author into a shared stage under a caller-supplied path, instead of
each creating its own stage — that is what makes composition possible.

Skinning: when a model composes against the animation skeleton, its skinned
meshes are bound via `UsdSkelBindingAPI` (joint indices/weights from the NIF
skin influences, remapped onto the shared skeleton by joint name). The skeleton
carries the model's real bone hierarchy (joint paths encode parenting) with a
bind pose from `NiSkinData`, so the composed model renders and stands upright in
Blender.

**Known limitation (animation playback):** the exported animation samples still
produce quaternion/matrix values that Blender's USD importer reports as
non-decomposable ("Error decomposing matrix"), so animated playback of the
composed rig is not yet correct — the static posed model is fine. This is a
rotation-sampling/convention issue in `author_clip` (present even for a
standalone `kf2usd` export with an identity bind pose), isolated from the
geometry/skeleton/skinning structure, and is the next thing to fix.

Up axis: LU/Gamebryo geometry is **Y-up** (a minifig stands along +Y; ground
props lie flat in XZ). All exporters declare stages `upAxis = Y` so models
import upright; `nif2usd --z-up` bakes a rotation for Z-up consumers.

## OpenUSD resolution

`find_package(pxr)` uses a system OpenUSD when present (fast); otherwise a
minimal monolithic OpenUSD is built from source via FetchContent (slow — the
system path is strongly preferred).

Some system OpenUSD packages leak optional-plugin dependencies (Alembic/HDF5,
OpenSubdiv/CUDA) into the propagated link interface — plugins loaded at runtime,
not needed to link a program that only *writes* USD. The CMake scrubs
unresolvable link-interface entries from the imported targets so authoring works
without those optional back-ends installed.
