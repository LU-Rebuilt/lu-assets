# ForkParticle Effect Definition Format

**Extension:** `.txt` (text-based effect definitions)
**Used by:** The LEGO Universe client to define particle effects composed of one or more emitters, each referencing a `.psb` file

## Overview

Effect definition files are plain-text configuration files that define a particle effect as a collection of emitter instances. Each emitter references a PSB file by name and includes a 4x4 transform matrix along with rendering and behavior parameters.

## Text Layout

```
emitter_name
1.0 0.0 0.0 0.0  0.0 1.0 0.0 0.0  0.0 0.0 1.0 0.0  0.0 0.0 0.0 1.0
facing=0 rot=0 trail=0 time=0.0 ds=0 se=0 mt=0 dist=0.0 dmin=0.0 prio=0 loop=0
```

### Per Emitter Fields

| Field    | Type  | Description                                      |
|----------|-------|--------------------------------------------------|
| name     | str   | Emitter name (maps to `{name}.psb` in same dir)  |
| transform| f32x16| 4x4 row-major transformation matrix              |
| facing   | int   | Billboard facing mode                            |
| rot      | int   | Rotation mode                                    |
| trail    | int   | Trail rendering enable                           |
| time     | f32   | Time offset for staggered emission               |
| ds       | int   | Distance sort flag                               |
| se       | int   | Unknown flag                                     |
| mt       | int   | Unknown flag                                     |
| dist     | f32   | Maximum render distance                          |
| dmin     | f32   | Minimum render distance                          |
| prio     | int   | Rendering priority                               |
| loop     | int   | Loop override                                    |

## Version

No versioning -- plain text format with no header or version field. A single format is used by all LU client effect definition files.

## Key Details

- Plain text format parsed line-by-line
- Each emitter's PSB file is located by appending `.psb` to the emitter name
- The 4x4 transform matrix allows positioning and rotating emitters relative to the effect origin
- Multiple emitters per effect file are supported
- No magic number or header; identified by context (file path under forkp/effects/)

## References

- No public ForkParticle documentation exists
- Ghidra RE of legouniverse.exe
