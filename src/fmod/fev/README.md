# FEV (FMOD Event) Format

**Extension:** `.fev`
**Used by:** The LEGO Universe client (42 files in audio/) to define FMOD sound events, categories, parameters, and music data

## Overview

FEV files are the FMOD Designer event project binary format. They contain a complete audio event hierarchy: banks, event categories, event groups, events (simple and complex), layers, sound instances, effect envelopes, parameters, sound definitions, waveform references, reverb definitions, and music data.

Two container formats exist, both round-tripped byte-for-byte by `fev_parse()`/`fev_write()`:

- **FEV1** (magic `"FEV1"`): flat binary, inline strings, version `0x00004000`. All 42 files in the shipped client's `audio/` tree are this variant.
- **RIFF-wrapped** (magic `"RIFF"`, form `"FEV "`): a RIFF chunk container, FMT version `0x00450000` (FMOD Designer 4.45). Used by a leaked pre-release client dump (31 real files). See "RIFF-wrapped FEV" below.

`fev_parse()` dispatches on the leading magic and sets `FevFile::is_riff`; `fev_write()` re-emits the same container the file came from. Both directions are byte-perfect for their own origin: 42/42 FEV1 + 31/31 RIFF verified against real files, plus the 11 FEV1 files that also ship in the pre-release dump.

Two music-data structures were corrected while building the writer (both had desynced the reader on `music_global.fev`, silently corrupting its music data):

- **`cond` is a container, not an empty leaf.** It wraps zero or more nested condition items (`cprm`/`cms`). An empty `cond` is just an 8-byte item (length + tag); a non-empty one holds a `[length][cprm|cms]` sub-item. Treating it as an empty leaf left the nested item's length prefix to be misread as a garbage chunk tag.
- **`smpm` (sample map) is a `u32` count followed by count × three `u32` entries**, not a single `u32`. It maps music segments/cues to sample indices (the first entry field matches the segment IDs used in `lnkd` chunks). Modeling it as one `u32` desynced the chunk loop on any file with a populated sample map.

## Binary Layout

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       magic — 0x31564546 ("FEV1")
0x04    4     u32       version — 0x00004000
0x08    8     u32[2]    checksums

--- Manifest ---
+0x00   4     u32       manifest_count
  Per entry (manifest_count times):
  +0x00  4    u32       type (FevManifestType enum, 0x00-0x20)
  +0x04  4    u32       value — object counts and name pool sizes

--- Project Name ---
  u32-prefixed string

--- Banks ---
  Per bank:
  +0x00  4    u32       load_mode (0x80=stream, 0x100=decompress, 0x200=load)
  +0x04  4    s32       max_streams
  +0x08  8    u32[2]    fsb_checksum — 8-byte truncated MD5 of the source filename;
                        matches the paired FSB4 header's bank_checksums field
                        (both independently derived, not runtime cross-checked —
                        see fmod/fsb/README.md's "Header checksums" section)
  +0x10  var  u32+str   name

--- Event Categories (recursive tree) ---
  Per category:
  +0x00  var  u32+str   name
  +var   4    f32       volume (linear, 1.0 = unity)
  +var   4    f32       pitch
  +var   4    s32       max_streams
  +var   4    u32       max_playback_behavior (0-4)
  +var   4    u32       subcategory_count
  ... subcategories (recursive)

--- Event Groups (recursive tree) ---
  Per group:
  +0x00  var  u32+str   name
  +var   var  u32+props user_properties
  +var   var            subgroups (recursive)
  +var   var            events

--- Events ---
  Per event:
  +0x00  4    u32       event_type (8=complex, 16=simple)
  +var   var  u32+str   name
  +var   16   u8[16]    guid
  +var   4    f32       volume (linear)
  +var   4    f32       pitch
  +var   4    f32       pitch_randomization
  +var   4    f32       volume_randomization
  +var   2    u16       priority
  +var   2    u16       max_instances
  +var   4    u32       max_playbacks
  +var   4    u32       steal_priority
  +var   4    u32       3d_flags (FMOD_MODE bitfield)
  +var   8    f32[2]    3d_min/max_distance
  +var   4    u32       event_flags
  +var   32   f32[8]    speaker_levels (L,R,C,LFE,LR,RR,LS,RS)
  +var   12   f32[3]    3d_cone (inside_angle, outside_angle, outside_volume)
  +var   4    u32       max_playbacks_behavior (1-5, 1-based)
  +var   ...            doppler, reverb, spread, fade, spawn, pan, layers, params...
```

### Manifest Types (selection)

```
Type  ID    Description
----  --    -----------
0x01        Bank count
0x02        Event category count
0x03        Event group count
0x0A        Simple event count
0x0B        Complex event count
0x11        Sound definition count
0x13        Project name size
```

## RIFF-wrapped FEV

The RIFF variant (FMT version `0x00450000`) stores the same audio hierarchy as FEV1 but split across RIFF chunks, with several fields moved or re-encoded. `fev_parse_riff()` decodes it into the same `FevFile` (best-effort model) while preserving the raw container so `fev_write()` reproduces every byte.

### Container structure

```
RIFF "FEV " {
  FMT   — 4-byte format/version word (0x00450000)
  LIST "PROJ" {
    OBCT — object-count manifest: count(u32) + (type,value) pairs (== FEV1 manifest)
    PROP — project name (u32-length-prefixed string)
    LGCY — the "legacy" body: the whole FEV1 structure minus header/manifest/
           project-name, re-encoded with the transforms below
    EPRP — flat envelope-point-coordinate array (see below)
    STRR — string reference table (names are u32 indices into this)
    LANG — language list: count(u32) + count u32-prefixed names, padded
  }
}
```

Chunks are word-aligned (odd-sized payloads get one trailing pad byte, not counted in the chunk size). Chunk ordering and padding are preserved verbatim.

### STRR string table

```
count(u32) + offsets[count](u32) + string pool (NUL-terminated strings)
```

Offsets are relative to the start of the pool (just past the offset table). In the RIFF LGCY body, **event-group names, event names, event-parameter names, and sound-definition names are u32 indices into STRR** rather than inline strings. Category names, bank names, and waveform filenames stay inline. The table (offsets + pool) is preserved verbatim and each element's original index is retained, so a rewrite emits the SAME index at each site (STRR may hold duplicate strings at distinct slots — indices are not re-derived from the string value).

### EPRP envelope points

```
count(u32) + count × (x:f32, y:f32, curve_shape:u32)
```

A single flat array of every effect-envelope point's curve coordinates, in envelope-traversal order. `x` is the normalized position (0..1) and `y` the value; the LGCY body stores only the integer point *positions*, so the curve shape and (x, y) live here. Preserved verbatim; the reader also surfaces the coordinates onto `FevEffectEnvelopePoint::eprp_x` / `value` / `curve_shape` where the counts line up.

### Per-language bank checksums

RIFF banks replace FEV1's single `(ck0, ck1)` pair with a per-language array:

```
bank_count(u32), lang_count(u32) [present only if bank_count > 0],
per bank: load_mode(u32), max_streams(s32),
          lang_count × (ck0:u32, ck1:u32, unk:u32),
          name (inline string)
```

`FevBank::lang_checksums` holds the full array; `fsb_checksum` mirrors language 0's `(ck0, ck1)` for the shared verify path.

### LGCY vs FEV1 field differences (fully reverse-engineered from the corpus)

The LGCY body follows the FEV1 layout with these additions/changes (all confirmed by full-consumption of the 31-file corpus; the reader applies them and the round-trip is verified byte-exact):

| Record | FEV1 | RIFF LGCY |
|--------|------|-----------|
| project name | header, once | inline in LGCY **and** in PROP (both emitted) |
| group / event / parameter / sound-def name | inline string | u32 STRR index |
| bank checksums | 1 × (ck0, ck1) | lang_count × (ck0, ck1, unk) |
| **event header** | — | +2 u32 between `event_flags` and speaker levels; +1 u32 after `threed_position_randomization`, before the layer list (all 0 in the corpus — version-gated 3D-rolloff words) |
| **effect envelope** | ctrl, name, dsp, flags, flags2, count, points(pos,val,curve), mapping[4], enabled | ctrl, dsp, extra0, flags, extra1, count, positions(u32 each), tail0, tail1 (tail1 = enabled); no name / flags2 / mapping; point val/curve in EPRP |
| **sound-def config** | 15 u32/f32 + 3 u16 | + 2 trailing u32 (0 in the corpus) |

The `extra`/`tail` words and the event-header additions are version-gated fields introduced by the newer FEV revision; they are 0 across the entire corpus but are read, modeled (`riff_extra*` / `riff_tail*` fields), and re-emitted so nothing is guessed.

### Round-trip design

`fev_parse_riff()` captures each chunk's raw bytes (LGCY, EPRP, STRR, LANG, plus FMT/OBCT/PROP and chunk ordering) into `FevFile::riff`, then runs the semantic LGCY decode as a **best-effort** pass to populate the model. `fev_write()` emits the preserved LGCY/EPRP bytes verbatim and reconstructs OBCT/PROP/STRR/LANG and the RIFF framing — so round-trip fidelity never depends on the semantic decode being complete for every file (matching the raw-block preservation pattern used for FSB sample data and PSB payloads). Hand-built `FevFile`s with no preserved bytes (unit tests) fall back to re-serializing LGCY/EPRP from the model, which the RIFF element writers support.

## Version

LU client FEV1 files use version `0x00004000` (stored at offset 0x04). The version field controls which fields are present during deserialization:

| Version threshold | Feature |
|-------------------|---------|
| >= 0x001A0000 | Envelope `enabled` field present |
| >= 0x00260000 | Envelope DSP target type bitfield present |
| >= 0x002E0000 | `sound_def_names_pool_size` field present (header bytes 8-11) |
| >= 0x00320000 | `waveform_names_pool_size` field present (header bytes 12-15) |
| >= 0x00390000 | Extended envelope flags present |

LU's version (0x00004000) exceeds all known thresholds, so all conditional fields are present.

## Key Details

- Little-endian byte order
- Magic: `0x31564546` ("FEV1"); version: `0x00004000`
- Also accepts RIFF-wrapped FEV (magic `0x46464952`)
- Events are either simple (single layer) or complex (multiple layers with parameters)
- Effect envelopes control DSP properties (volume, pitch, pan) over parameter ranges
- FSB bank checksums in the FEV match the paired FSB file's header checksums by construction (both are an MD5 of the same source filename, not a runtime cross-check)
- Category volume is linear (1.0 = 0 dB); event volume is also linear
- 3D flags use FMOD_MODE bitfield encoding

## References

- lcdr/lu_formats fev.ksy (github.com/lcdr/lu_formats) — complete FEV binary spec
- FMOD Designer 4.44.64 documentation
- Ghidra RE of fmod_event.dll
