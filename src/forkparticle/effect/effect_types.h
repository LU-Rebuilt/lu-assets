#pragma once
// effect_types.h — ForkParticle effect definition data structures.
// Each effect has one or more emitters, each referencing a .psb file.
//
// REVERSE ENGINEERING SOURCE:
//   legouniverse.exe FUN_010c8610 — effect .txt parser. Maps each tag to the
//   emitter struct. All boolean flags (ROT, TRAIL, DS, SE, MT, LOOP) are packed
//   into a single flags byte at struct+0x80.
//
// EMITTER STRUCT LAYOUT (from FUN_010c8610):
//   +0x28: float[16] transform    — 4x4 matrix (from TRANSFORM tag)
//   +0x68: uint8_t   priority     — render priority (from PRIO tag, supports per-client)
//   +0x6C: float     dist_sq      — max render distance SQUARED (from DIST tag)
//   +0x70: float     dmin_sq      — min render distance SQUARED (from DMIN tag)
//   +0x74: float     time         — start delay in seconds (from TIME tag)
//   +0x78: float     unknown_78   — parsed from unknown 4-char tag
//   +0x7C: int32_t   facing       — facing mode enum (from FACING tag)
//   +0x80: uint8_t   flags        — packed boolean flags:
//            bit 0 (0x01): ROT    — particles rotate with emitter
//            bit 1 (0x02): TRAIL  — draw trail geometry between positions
//            bit 2 (0x04): DS     — distance sort (back-to-front by camera distance)
//            bit 3 (0x08): SE     — scale by emitter transform
//            bit 4 (0x10): MT     — motion transform (local-space particle movement)
//            bit 5 (0x20): LOOP   — loop emitter playback
//            bit 6 (0x40): ??     — unknown flag from 3-char tag

#include <string>
#include <vector>

namespace lu::assets {

struct EffectEmitter {
    std::string name;           // Maps to {name}.psb in the same directory
    float transform[16] = {    // 4x4 row-major transform matrix
        1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
    };

    // Facing mode enum — controls particle billboard orientation.
    // 0 = camera-facing (billboard), 1 = world X, 2 = world Y,
    // 3 = world Z, 4 = emitter-relative, 5 = radial (outward from origin)
    int facing = 0;

    // Start delay before this emitter begins (seconds)
    float time = 0.0f;

    // Render distance thresholds.
    // IMPORTANT: LU client stores these as SQUARED values for efficient
    // distance comparison (no sqrt needed). Our parser stores raw values;
    // squaring is done at runtime.
    float dist = 0.0f;          // max render distance
    float dmin = 0.0f;          // min render distance

    // Render priority — controls draw order. Supports per-client overrides
    // in format "clientId:value,clientId:value" but typically just an int.
    int prio = 0;

    // Packed boolean flags — each maps to a bit in the client's flags byte.
    int rot = 0;                // bit 0: particles rotate with emitter
    int trail = 0;              // bit 1: draw trail geometry between particle positions
    int ds = 0;                 // bit 2: distance sort (back-to-front rendering)
    int se = 0;                 // bit 3: scale particles by emitter transform
    int mt = 0;                 // bit 4: motion transform (local-space movement)
    int loop = 0;               // bit 5: loop emitter playback
};

struct EffectFile {
    std::vector<EffectEmitter> emitters;
};

} // namespace lu::assets
