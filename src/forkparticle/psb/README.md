# PSB (Particle System Binary) Format

**Extension:** `.psb`
**Used by:** The LEGO Universe client (11,366 files under forkp/effects/) for ForkParticle emitter definitions

## Overview

PSB is ForkParticle's proprietary binary emitter format. Each file defines a single particle emitter with colors, timing, velocity, size, rotation, spin, bounds, textures, and optional animation keyframes. No public ForkParticle SDK documentation exists; the format was fully reverse-engineered from legouniverse.exe.

## Binary Layout

### Header (0x00-0x0F, 16 bytes)

```
Offset  Size  Type  Field
------  ----  ----  -----
0x00    4     u32   header_size — always 80 (0x50)
0x04    4     u32   data_size — always 420 (0x1A4)
0x08    4     u32   particle_id — emitter index within the effect
0x0C    4     u32   section_offset — always 112 (0x70)
```

### Color Block (0x10-0x4F, 64 bytes)

```
0x10    16    f32x4  start_color (RGBA)
0x20    16    f32x4  middle_color (RGBA)
0x30    16    f32x4  end_color (RGBA)
0x40    16    f32x4  birth_color (RGBA)
```

### Timing Block (0x50-0x6B, 28 bytes)

```
0x50    4     f32   color_midpoint_1 — start-to-middle threshold (0-1)
0x54    4     f32   color_midpoint_2 — middle-to-end threshold (0-1)
0x58    4     f32   particle_lifetime (seconds)
0x5C    4     f32   birth_rate (particles/sec)
0x60    4     f32   death_delay (seconds)
0x64    4     f32   emit_period (0=burst, positive=repeating cycle)
0x68    4     u32   flags — blend mode bits + behavior flags
```

### Velocity Block (0x6C-0x87, 28 bytes)

```
0x6C    4     f32   emit_speed
0x70    4     f32   speed_x
0x74    4     f32   speed_y
0x78    4     f32   size_transition — size phase threshold (0-1)
0x7C    4     f32   gravity (units/s^2)
0x80    4     f32   spread_angle (degrees)
0x84    4     f32   rotation_speed (degrees/s)
```

### Size Block (0x88-0x97, 16 bytes)

```
0x88    4     f32   size_start
0x8C    4     f32   size_end
0x90    4     f32   size_mult (NOT read by runtime)
0x94    4     f32   size_alpha (NOT read by runtime)
```

### Rotation Block (0x98-0xA7, 16 bytes)

```
0x98    4     f32   initial_rotation (degrees, converted to radians by engine)
0x9C    12    f32x3 padding — always 0
```

### Color2 Block (0xA8-0xB7, 16 bytes)

```
0xA8    16    f32x4  color2 (RGBA) — secondary color
```

### Acceleration Block (0xB8-0xCF, 24 bytes)

```
0xB8    4     f32   accel_x
0xBC    4     f32   accel_y
0xC0    4     f32   accel_z
0xC4    4     f32   padding — almost always 0
0xC8    4     f32   format_const — always 100.0 (format version marker)
0xCC    4     f32   max_draw_dist (game units, 0=no cull)
```

### Spin Block (0xD0-0xEB, 28 bytes)

```
0xD0    4     f32   spin_start (degrees)
0xD4    4     f32   spin_min (degrees/s)
0xD8    4     f32   spin_max (degrees/s)
0xDC    4     f32   spin_var (degrees/s)
0xE0    4     f32   spin_damp
0xE4    4     f32   spin_speed (degrees/s)
0xE8    4     u32   spin_flags
```

### Bounds Block (0xEC-0x107, 28 bytes)

```
0xEC    12    f32x3  bounds_min (x, y, z)
0xF8    12    f32x3  bounds_max (x, y, z)
0x104   4     f32    padding — always 0
```

### Metadata Block (0x108-0x19F)

```
0x108   4     f32   texture_cycle_rate
0x10C   4     u32   texture_blend_mode (0=none,1=add,2=screen,3=mul,4=sub,6=alpha)
0x110   4     f32   playback_scale (never zero)
0x114   4     u32   loop_count (0=once, >=1=repeat)
0x118   4     u32   file_total_size
0x11C   4     u32   emitter_params_size — always 412
0x120   4     u32   data_block_size — always 420
0x124   4     u32   num_textures (1-32)
0x128   4     u32   runtime_ptr_a (overwritten at load)
0x12C   4     u32   texture_data_offset
0x130   4     u32   flag_extra (0 or 1)
0x134   4     u32   extra_size
0x138   4     u32   anim_data_offset (0=static)
0x13C   4     u32   texture_base_offset — always 832 (0x340)
0x140-0x183   var   designer state / runtime state
0x184   4     u32   file_total_size_dup
0x188   4     f32   scale_188
0x18C   4     f32   scale_18c
0x190   4     f32   scale_190
0x194   4     u32   emitter_name_present (0=no name)
0x198   4     u32   emitter_name_offset
```

### Texture Array (at texture_data_offset)

```
Per texture (64 bytes each, num_textures entries):
+0x00   4     u32     type — 1 = texture path
+0x04   60    char[60] path — null-terminated texture path
```

## Version

PSB files have no explicit version field. The `format_const` field at offset 0xC8 is always exactly `100.0` in all 11,366 client files and likely serves as a format version marker. The fixed structure sizes (`header_size` = 80, `data_size` = 420, `emitter_params_size` = 412) are invariant across all client files, indicating a single format version throughout LU's lifetime.

## Key Details

- Little-endian byte order
- No magic number; identified by extension and fixed header values
- header_size always 80, data_size always 420, format_const always 100.0
- 33% of client files have animation keyframe data (anim_data_offset > 0)
- Blend mode flags in the flags field at 0x68 map to blend modes 1-9
- Rotation values stored in degrees; engine converts to radians
- RE source: legouniverse.exe FUN_01092450 (field mapping) and FUN_010cdbf0 (initializer)

## References

- No public ForkParticle documentation exists
- Ghidra RE of legouniverse.exe — complete field-by-field mapping
- Statistical analysis of all 11,366 client PSB files
