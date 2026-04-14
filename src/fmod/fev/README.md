# FEV (FMOD Event) Format

**Extension:** `.fev`
**Used by:** The LEGO Universe client (42 files in audio/) to define FMOD sound events, categories, parameters, and music data

## Overview

FEV files are the FMOD Designer event project binary format. They contain a complete audio event hierarchy: banks, event categories, event groups, events (simple and complex), layers, sound instances, effect envelopes, parameters, sound definitions, waveform references, reverb definitions, and music data.

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
  +0x08  8    u32[2]    fsb_checksum — cross-checked against FSB header
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

## Key Details

- Little-endian byte order
- Magic: `0x31564546` ("FEV1"); version: `0x00004000`
- Also accepts RIFF-wrapped FEV (magic `0x46464952`)
- Events are either simple (single layer) or complex (multiple layers with parameters)
- Effect envelopes control DSP properties (volume, pitch, pan) over parameter ranges
- FSB bank checksums in the FEV must match the paired FSB file's header checksums
- Category volume is linear (1.0 = 0 dB); event volume is also linear
- 3D flags use FMOD_MODE bitfield encoding

## References

- lcdr/lu_formats fev.ksy (github.com/lcdr/lu_formats) — complete FEV binary spec
- FMOD Designer 4.44.64 documentation
- Ghidra RE of fmod_event.dll
