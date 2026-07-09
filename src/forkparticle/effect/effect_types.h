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
//   +0x78: float     cr           — "CR" tag, bare float (atof, no squaring/clamping);
//                                    no runtime consumer found in legouniverse.exe itself,
//                                    likely read by the ForkParticle SDK's own DLLs
//   +0x7C: int32_t   facing       — facing mode enum (from FACING tag)
//   +0x80: uint8_t   flags        — packed boolean flags:
//            bit 0 (0x01): ROT    — particles rotate with emitter
//            bit 1 (0x02): TRAIL  — draw trail geometry between positions
//            bit 2 (0x04): DS     — distance sort (back-to-front by camera distance)
//            bit 3 (0x08): SE     — scale by emitter transform
//            bit 4 (0x10): MT     — motion transform (local-space particle movement)
//            bit 5 (0x20): LOOP   — loop emitter playback
//            bit 6 (0x40): RSX    — vestigial PS3-port (Sony RSX GPU) flag, from the "RSX"
//                                   tag; no runtime consumer found in the PC client

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
    int rsx = 0;                // bit 6: RSX — vestigial PS3-port (Sony RSX GPU) flag,
                                 // no runtime consumer found in the PC client; must still
                                 // round-trip since some shipped files set it.

    // Creation rate — struct+0x78 in FUN_010c8610 (Ghidra: legouniverse.exe). Stored as a
    // bare float via atof with no squaring/clamping (unlike DIST/DMIN/PRIO). No runtime
    // consumer found in legouniverse.exe itself — likely read by the ForkParticle SDK's
    // own update/render DLLs rather than the game client. Present in ~23% of real files.
    float cr = 0.0f;
    bool has_cr = false;        // whether a CR: line was present (see raw_lines note below)

    // Verbatim source lines for this emitter, from its EMITTERNAME line up to (but not
    // including) the next EMITTERNAME line, each WITHOUT its line terminator. The real
    // client corpus has no dedicated writer tool (confirmed via Ghidra: legouniverse.exe
    // only ever reads this format) — field order, conditional presence, integer-vs-float
    // formatting, and even authoring typos (e.g. "lEMITTERNAME", "DSl") vary per file with
    // no discoverable rule, so by default effect_write() re-emits these raw lines unchanged
    // rather than reformatting from the typed fields above.
    //
    // If a consumer (e.g. psb-editor's property panel) edits one of the typed fields after
    // parsing, effect_write() detects the divergence from parse_snapshot (below) and
    // regenerates that emitter's text from the current field values instead — trading
    // exact source formatting for the edit actually taking effect. Freshly-constructed
    // emitters (raw_lines empty, e.g. psb-editor's "Add Emitter") are always generated
    // this way. See effect_writer.cpp.
    std::vector<std::string> raw_lines;

    // Snapshot of every typed field taken at parse time (or default-constructed for a new
    // emitter with no raw_lines) — effect_write()'s only use for this is an equality check
    // against the live fields to decide raw-replay vs. regenerate. Never touch by hand.
    struct Snapshot {
        float transform[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        int facing = 0, prio = 0, rot = 0, trail = 0, ds = 0, se = 0, mt = 0, loop = 0, rsx = 0;
        float time = 0.0f, dist = 0.0f, dmin = 0.0f, cr = 0.0f;
        bool has_cr = false;
    } parse_snapshot;

    bool matches_snapshot() const {
        for (int i = 0; i < 16; ++i) if (transform[i] != parse_snapshot.transform[i]) return false;
        return facing == parse_snapshot.facing && prio == parse_snapshot.prio &&
               rot == parse_snapshot.rot && trail == parse_snapshot.trail &&
               ds == parse_snapshot.ds && se == parse_snapshot.se && mt == parse_snapshot.mt &&
               loop == parse_snapshot.loop && rsx == parse_snapshot.rsx &&
               time == parse_snapshot.time && dist == parse_snapshot.dist &&
               dmin == parse_snapshot.dmin && cr == parse_snapshot.cr &&
               has_cr == parse_snapshot.has_cr;
    }
};

struct EffectFile {
    std::vector<EffectEmitter> emitters;

    // Exact original line terminator ("\r\n" or "\n"), detected from the source text, so
    // effect_write() can reproduce it. Real files are inconsistent client-tree-wide but
    // internally consistent per-file.
    std::string line_ending = "\n";

    // Raw lines appearing before the first EMITTERNAME line (e.g. a stray leading blank
    // line — present in a large minority of real files). Empty in the common case.
    std::vector<std::string> prefix_lines;

    // Whether the file's last line is followed by a final line terminator.
    bool trailing_newline = true;
};

} // namespace lu::assets
