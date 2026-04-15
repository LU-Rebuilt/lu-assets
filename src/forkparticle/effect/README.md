# ForkParticle Effect Definition Format

**Extension:** `.txt`
**Used by:** LEGO Universe client to define particle effects composed of one or more emitters

## Overview

Effect definition files are plain-text configuration files. Each emitter references a PSB file by name and includes a 4x4 transform matrix, timing, and behavior flags.

## RE Source

**legouniverse.exe FUN_010c8610** — Effect text parser. Each tag is matched by string comparison and parsed with `strtok_s` using `" :,\t\n"` delimiters.

## Text Format

```
EMITTERNAME: fire_burst
TRANSFORM: 1.000000 0.000000 0.000000 0.000000 0.000000 1.000000 0.000000 0.000000 0.000000 0.000000 1.000000 0.000000 0.000000 2.500000 0.000000 1.000000
FACING: 0
ROT: 0
TRAIL: 0
TIME: 0.500000
DS: 0
SE: 0
DIST: 200.000000
DMIN: 0.000000
PRIO: 5
LOOP: 1
```

## Per-Emitter Fields

| Tag | Type | Emitter Struct | Description |
|-----|------|---------------|-------------|
| EMITTERNAME | string | name | PSB filename (without .psb extension) |
| TRANSFORM | f32x16 | +0x28 | 4x4 row-major world transform matrix |
| FACING | int | +0x7C | Billboard facing mode (see below) |
| ROT | int (flag) | +0x80 bit 0 | Particles rotate with emitter |
| TRAIL | int (flag) | +0x80 bit 1 | Draw trail geometry between positions |
| TIME | float | +0x74 | Start delay before emission (seconds) |
| DS | int (flag) | +0x80 bit 2 | Distance sort (back-to-front by camera) |
| SE | int (flag) | +0x80 bit 3 | Scale particles by emitter transform |
| MT | int (flag) | +0x80 bit 4 | Motion transform (local-space movement) |
| DIST | float | +0x6C | Max render distance (stored squared in client) |
| DMIN | float | +0x70 | Min render distance (stored squared in client) |
| PRIO | int | +0x68 | Render priority. Supports per-client format: `clientId:value,clientId:value` |
| LOOP | int (flag) | +0x80 bit 5 | Loop emitter playback |

## Facing Modes

| Value | Name | Description |
|-------|------|-------------|
| 0 | Camera Billboard | Particles always face the camera (default) |
| 1 | World X | Particles face the X axis (YZ plane) |
| 2 | World Y | Particles face the Y axis (XZ plane, flat on ground) |
| 3 | World Z | Particles face the Z axis (XY plane) |
| 4 | Emitter | Particles oriented by emitter transform |
| 5 | Radial | Particles face outward from emitter origin |

Facing mode names from MediaFX UI: "Radial Facing", "Emitter Facing", "World Z Facing", "World Y Facing", "World X Facing".

## Flag Byte Layout (+0x80)

All boolean tags (ROT, TRAIL, DS, SE, MT, LOOP) are packed into a single byte:

```
bit 0 (0x01): ROT   — particles rotate with emitter
bit 1 (0x02): TRAIL — draw trail geometry
bit 2 (0x04): DS    — distance sort
bit 3 (0x08): SE    — scale by emitter
bit 4 (0x10): MT    — motion transform
bit 5 (0x20): LOOP  — loop playback
bit 6 (0x40): (unknown, from 3-char tag)
```

## Key Details

- Plain text, one tag per line, `KEY: VALUE` format
- Tags are case-sensitive
- DIST and DMIN are stored as **squared** values in the client's emitter struct for efficient distance comparison (no sqrt needed at runtime)
- PRIO supports per-client priority overrides with comma-separated `clientId:value` pairs
- Multiple emitters per effect file
- Each emitter's PSB file is located by appending `.psb` to the EMITTERNAME value
