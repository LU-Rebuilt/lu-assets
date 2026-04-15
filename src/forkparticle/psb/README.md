# PSB (Particle System Binary) Format

**Extension:** `.psb`
**Used by:** LEGO Universe client (11,366 files under `forkp/effects/`)
**Origin:** ForkParticle SDK (no public documentation exists)

## Overview

PSB is ForkParticle's proprietary binary emitter format. Each file defines a single particle emitter with all parameters needed for simulation and rendering. The format was fully reverse-engineered from legouniverse.exe and the ForkParticle MediaFX editor.

## RE Sources

- **mediafx.exe CEmitterStatsDlg** (FUN_0046f890) — Maps every PSB offset to its official ForkParticle UI label
- **mediafx.exe PSB text serializer** (FUN_004179b0) — Official `*TAG` names for every field
- **legouniverse.exe FUN_01092450** — PSB-to-emitter-struct field mapping (verified offset-by-offset)
- **legouniverse.exe FUN_010cdbf0** — Emitter initializer, flag decode, blend mode mapping
- **legouniverse.exe FUN_0109bb30** — Per-particle update (color/size interpolation, texture animation)
- **legouniverse.exe FUN_01093fc0** — Physics (drag, gravity, point forces, position)
- **legouniverse.exe FUN_01097c60** — Particle spawn (lifetime, velocity, spin, size randomization)
- **legouniverse.exe FUN_010d2f90** — Emitter update (emission rate, accumulator, max_particles cap)
- **Statistical analysis** of all 11,366 unpacked client PSB files

## Binary Layout

All values are little-endian.

### Header (0x00-0x0F, 16 bytes)

| Offset | Size | Type | ForkParticle Tag | Field | Notes |
|--------|------|------|-----------------|-------|-------|
| 0x00 | 4 | u32 | — | header_size | Always 80 |
| 0x04 | 4 | u32 | — | data_size | Always 420 |
| 0x08 | 4 | u32 | — | particle_id | Emitter index within effect |
| 0x0C | 4 | u32 | — | section_offset | Always 112 |

### Color Block (0x10-0x4F, 64 bytes)

| Offset | Size | Type | Tag | Field | MediaFX UI Label |
|--------|------|------|-----|-------|-----------------|
| 0x10 | 16 | f32x4 | *PICOLOR | initial_color | "Initial color" |
| 0x20 | 16 | f32x4 | *PTCOLOR1 | trans_color_1 | "Transitional color 1" |
| 0x30 | 16 | f32x4 | *PTCOLOR2 | trans_color_2 | "Transitional color 2" |
| 0x40 | 16 | f32x4 | *PFCOLOR | final_color | "Final color" |

### Particle Properties (0x50-0x6B, 28 bytes)

| Offset | Size | Type | Tag | Field | MediaFX UI Label |
|--------|------|------|-----|-------|-----------------|
| 0x50 | 4 | f32 | *PCOLORRATIO | color_ratio_1 | "Color life percentage1" |
| 0x54 | 4 | f32 | *PCOLORRATIO2 | color_ratio_2 | "Color life percentage2" |
| 0x58 | 4 | f32 | *PLIFEMIN | life_min | "Minimum particle life" |
| 0x5C | 4 | f32 | *PLIFEVAR | life_var | "Life variance" |
| 0x60 | 4 | f32 | *PVELMIN | vel_min | "Minimum initial velocity" |
| 0x64 | 4 | f32 | *PVELVAR | vel_var | "Velocity variance" |
| 0x68 | 4 | u32 | *PFLAGS | flags | "Bit property flags" |

### Scale Properties (0x6C-0x78, 16 bytes)

| Offset | Size | Type | Tag | Field | MediaFX UI Label |
|--------|------|------|-----|-------|-----------------|
| 0x6C | 4 | f32 | *PISCALE | initial_scale | "Initial scale" |
| 0x70 | 4 | f32 | *PTSCALE | trans_scale | "Transitional scale" |
| 0x74 | 4 | f32 | *PFSCALE | final_scale | "Final scale" |
| 0x78 | 4 | f32 | *PSCALERATIO | scale_ratio | "Scale life percentage" |

### Rotation + Drag (0x7C-0x84, 12 bytes)

| Offset | Size | Type | Tag | Field | MediaFX UI Label |
|--------|------|------|-----|-------|-----------------|
| 0x7C | 4 | f32 | *PROTMIN | rot_min | "Minimum rotation" |
| 0x80 | 4 | f32 | *PROTVAR | rot_var | "Rotation speed variance" |
| 0x84 | 4 | f32 | *PDRAG | drag | "Drag coefficient variance" |

### Scale Vector (0x88-0x97, 16 bytes)

| Offset | Size | Type | Tag | Field | Notes |
|--------|------|------|-----|-------|-------|
| 0x88 | 16 | f32x4 | *SCALE | scale[4] | "Minimum scale" in MediaFX. First 2 components multiplied by engine constant (0.5). Components 3-4 not read by runtime. |

### Rotation Vector (0x98-0xA7, 16 bytes)

| Offset | Size | Type | Tag | Field | Notes |
|--------|------|------|-----|-------|-------|
| 0x98 | 16 | f32x4 | *ROTATION | rotation[4] | Only [0] used, converted deg-to-rad. Components 1-3 always 0. |

### Tint Color (0xA8-0xB7, 16 bytes)

| Offset | Size | Type | Tag | Field | MediaFX UI Label |
|--------|------|------|-----|-------|-----------------|
| 0xA8 | 16 | f32x4 | *TINT | tint | "Tint color" |

### Scale Variation (0xB8-0xC3, 12 bytes)

| Offset | Size | Type | Tag | Field | MediaFX UI Label |
|--------|------|------|-----|-------|-----------------|
| 0xB8 | 4 | f32 | *ISCALEMIN | iscale_var | "IScale variation" |
| 0xBC | 4 | f32 | *TSCALEMIN | tscale_var | "TScale variation" |
| 0xC0 | 4 | f32 | *FSCALEMIN | fscale_var | "FScale variation" |

### Emitter Properties (0xC4-0xEB, 40 bytes)

| Offset | Size | Type | Tag | Field | MediaFX UI Label | Notes |
|--------|------|------|-----|-------|-----------------|-------|
| 0xC4 | 4 | f32 | *ESIMLIFE | sim_life | "Emitter simulation life on create" | Designer-only, not read by LU |
| 0xC8 | 4 | f32 | *ELIFE | emitter_life | "Emitter Life" | Designer-only, not read by LU. Default 100.0 |
| 0xCC | 4 | f32 | *ERATE | emit_rate | "Emission rate" | Particles per second |
| 0xD0 | 4 | f32 | *EGRAVITY | gravity | "Gravity" | Negative = downward (Y-up) |
| 0xD4 | 4 | f32 | *EPLANEW | plane_w | "Plane-W" | Emission volume width |
| 0xD8 | 4 | f32 | *EPLANEH | plane_h | "Plane-H" | Emission volume height |
| 0xDC | 4 | f32 | *EPLANED | plane_d | "Plane-D" | Emission volume depth |
| 0xE0 | 4 | f32 | *ECONERAD | cone_radius | "Emission cone radius" | Degrees |
| 0xE4 | 4 | f32 | *EMAXPARTICLE | max_particles | "Maximum particles allowed" | |
| 0xE8 | 4 | u32 | *EVOLUME | volume_type | "Emitter volume type" | 0=point, 1=box, 2=arc, 3=sphere, 4=cone, 5=cylinder |

### Bounds (0xEC-0x107, 28 bytes)

| Offset | Size | Type | Tag | Field |
|--------|------|------|-----|-------|
| 0xEC | 12 | f32x3 | *BOUNDINGBOXMIN | bounds_min |
| 0xF8 | 12 | f32x3 | *BOUNDINGBOXMAX | bounds_max |
| 0x104 | 4 | f32 | *NBURST | num_burst — particles on create |

### Metadata (0x108-0x13F)

| Offset | Size | Type | Tag | Field | Notes |
|--------|------|------|-----|-------|-------|
| 0x108 | 4 | f32 | *ANMSPEED | anim_speed | Particle animation speed (flipbook) |
| 0x10C | 4 | u32 | *EBLENDMODE | blend_mode | 1=add, 2=screen, 3=mul, 4=sub, 6=alpha |
| 0x110 | 4 | f32 | *TDELTAMULT | time_delta_mult | Playback speed multiplier |
| 0x114 | 4 | u32 | *NUMPOINTFORCES | num_point_forces | |
| 0x118 | 4 | u32 | — | file_total_size | |
| 0x11C | 4 | u32 | — | emitter_params_size | Always 412 |
| 0x120 | 4 | u32 | — | data_block_size | Always 420 |
| 0x124 | 4 | u32 | *NUMASSETS | num_assets | Texture/asset count (1-32) |
| 0x128 | 4 | u32 | — | runtime_ptr_a | Heap pointer, overwritten at load |
| 0x12C | 4 | u32 | — | texture_data_offset | File offset to texture array |
| 0x130 | 4 | u32 | *NUMEMISSIONASSETS | num_emission_assets | |
| 0x134 | 4 | u32 | — | extra_size_134 | Usually = file_total_size |
| 0x138 | 4 | u32 | — | anim_data_offset | 0 = static texture |
| 0x13C | 4 | u32 | — | texture_base_offset | Always 832 |

### Designer State + Path (0x140-0x190)

| Offset | Size | Type | Tag | Field | MediaFX UI Label |
|--------|------|------|-----|-------|-----------------|
| 0x140 | 4 | f32 | *OFFSET | emitter_offset_x | — |
| 0x144 | 4 | f32 | *OFFSET | emitter_offset_y | — |
| 0x148 | 4 | f32 | *OFFSET | emitter_offset_z | — |
| 0x188 | 4 | f32 | — | path_dist_min | "Minimum particle distance from path" |
| 0x18C | 4 | f32 | — | path_dist_var | "Path particle distance variance" |
| 0x190 | 4 | f32 | — | path_speed | "Emitter speed on path" |

### Emitter Name (0x194-0x198)

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0x194 | 4 | u32 | emitter_name_present (0 = no name) |
| 0x198 | 4 | u32 | emitter_name_offset (file offset to string) |

### Texture Array (at texture_data_offset)

num_assets entries, each 64 bytes:

| Offset | Size | Type | Field |
|--------|------|------|-------|
| +0x00 | 4 | u32 | type — 1 = texture path |
| +0x04 | 60 | char[60] | path — null-terminated |
| +0x2C | 16 | f32x4 | UV rect (u_min, v_min, u_max, v_max) |

## Flags Field (PSB+0x68)

The flags field encodes two independent systems:

### Particle Rendering Mode (decoded by FUN_010cdbf0)

| Flag Bit | Mode | Name | Description |
|----------|------|------|-------------|
| (none) | 0 | Billboard | Camera-facing quad (default) |
| 0x8 | 1 | Billboard (neg rot) | Camera-facing, negated initial rotation |
| 0x10 | 2 | Velocity Streak | Quad stretches along velocity (sparks) |
| 0x20 | 3 | Billboard Alt | Billboard variant |
| 0x40 | 4 | Billboard (neg rot 2) | Camera-facing, negated rotation |
| 0x80000 | 5 | Velocity Streak (no drag) | Velocity-aligned, often paired with 0x100 |
| 0x4000000 | 6 | 3D Model | DirectX .PAX mesh (GeomObjectDLL) |
| 0x800000 | 7 | 3D Model (2) | .PAX mesh variant |
| 0x1000000 | 8 | 3D Model (neg rot) | .PAX mesh, negated rotation |
| 0x2000000 | 9 | 3D Model (4) | .PAX mesh variant |

### Behavior Flags (independent of mode)

| Flag Bit | Name | Description |
|----------|------|-------------|
| 0x1 | Random texture | Random start texture index at birth |
| 0x2 | Animated texture | Flipbook animation using anim_speed |
| 0x4 | Random flip | 50% chance to mirror particle |
| 0x100 | Skip drag/forces | Particle ignores drag and point forces |
| 0x100000 | Immortal | Particle resets age at maxLife instead of dying |
| 0x200000 | Always positive spin | Spin rate never negated |
| 0x400000 | Always negative spin | Spin rate always negated |

## Simulation Details

### Lifetime
`maxLife = lerp(life_min, life_var, random)` — life_var is the MAXIMUM, not additive variance.

### Velocity
`speed = lerp(vel_min, vel_var, random)` — distributed within emission volume/cone.

### Size (per-particle randomized, 2-phase interpolation)
- Birth: `lerp(iscale_var, initial_scale, random)` (variation = minimum, scale = maximum)
- Mid: `lerp(tscale_var, trans_scale, random)`
- Death: `lerp(fscale_var, final_scale, random)`
- Phase 1 (0 to scale_ratio): birth -> mid
- Phase 2 (scale_ratio to 1): mid -> death
- Engine multiplies by SIZE_SCALE (0.5)

### Color (3-phase interpolation)
- Phase 1 (0 to color_ratio_1): initial -> transitional 1
- Phase 2 (color_ratio_1 to color_ratio_2): transitional 1 -> transitional 2
- Phase 3 (color_ratio_2 to 1): transitional 2 -> final
- Result modulated by tint RGBA

### Physics
- `velocity += velocity * drag * dt` (velocity-proportional, negative = deceleration)
- `velocity.y += gravity * dt` (negative = downward in Y-up)
- `position += velocity * dt`
- `rotation += spinRate * dt`
- Skipped when flags & 0x100

### Emission
- `numToSpawn = emit_rate * dt` with fractional accumulator
- Capped at max_particles
