# Settings (Gamebryo Compiled Animation-Sequencing) Format

**Extension:** `.settings`
**Used by:** The LEGO Universe client (737 files per client version; 3754 total across every available client version/leak) to define animation sequences, sequence groups, and inter-sequence transitions for actors

## Overview

Settings files are a binary encoding of the same data model as the `.kfm` text format (`;Gamebryo KFM File Version x.x.x`). They define which animation sequences an actor supports, how they are grouped into categories, and transition rules between animations. The tool that wrote them has not been identified — `SequenceEditor.exe`, found alongside client installs, was checked as the presumed authoring tool but turned out to be an unrelated OpenAL audio utility with no Gamebryo code at all.

The `"Sequences"`/`"Sequence Groups"` section markers are NOT string constants the retail engine compares against — those bytes don't appear anywhere in `legouniverse.exe` (confirmed via Ghidra byte-pattern search across the full binary). The real engine reader must dispatch purely on the numeric `section_type` tags (1 and 4); the string markers only matter for our own reader's byte-exact round-trip.

## Binary Layout

```
--- Header ---
Offset  Size  Type      Field
------  ----  ----      -----
+0x00   1     u8        version_len
+0x01   N     char[N]   version — "2.3.0" in nearly every file; 22/3754 use "2.2.2"
                         with an identical header layout to 2.3.0
+var    4     u32       header_flags — always 1
+var    4     u32       model_filename_len — always 0 (empty NIF filename)
+var    4     u32       unk_c — stale authoring-tool memory, not semantic data (see below)
+var    1     u8        unk_d — correlates with unk_c, same origin (see below)
+var    4     u32       unk_e — mostly two dominant "mode" values (see below)
+var    1     u8        unk_f — always 0

--- Sequences Section ---
+var    4     u32       section_type — always 1
+var    1     u8        marker_len (= 9)
+var    9     char[9]   "Sequences"
+var    4     u32       declared_count (UNRELIABLE — may undercount)
  Per entry (until "Sequence Groups" marker detected):
  +0x00  4    u32       entry_type — 2 = group name, 3 = animation sequence
  +0x04  1    u8        name_len
  +0x05  N    char[N]   name
  +var   4    u32       entry_id — type 3: event code; type 2: animation count

--- Sequence Groups Section ---
+var    4     u32       section_type — always 4
+var    1     u8        marker_len (= 15)
+var    15    char[15]  "Sequence Groups"
+var    4     u32       group_count — 0 in nearly every file (see below)
  Per group entry (group_count times; rare — see below):
  +0x00  4    u32       entry_count — number of animations in this group
  +0x04  1    u8        name_len
  +0x05  N    char[N]   name
  +var   4    u32       unk_trailing — purpose unknown; 0 in the one known real sample

--- Animation Table ---
+var    4     u32       animation_count
  Per animation (15 bytes each):
  +0x00  4    u32       event_code — sequence ID
  +0x04  4    u32       num_transitions — always 0 in every real file
  +0x08  4    u32       unk_field — always 0 in every real file
  +0x0C  1    u8        unk_byte_1 — always 0 in every real file
  +0x0D  1    u8        unk_byte_2 — always 0 in every real file
  +0x0E  1    u8        unk_byte_3 — always 0 in every real file

--- Footer ---
  Variable-length remaining data stored as raw bytes.
```

## Version

All but 22 of the 3754 real client files use version **"2.3.0"**; the 22 exceptions use **"2.2.2"** with an identical header layout, so there is no version-conditional parsing needed. The version is related to Gamebryo's KFM serialization version but is not the same as the `.kfm` file version.

## The unk_c / unk_d / unk_e fields — corpus survey findings

Investigated via a full-corpus statistical survey (all 3754 real `.settings` files across every available client version/leak) plus static Ghidra RE of `legouniverse.exe`. Conclusion: **not semantically meaningful game data** — preserved verbatim for byte-perfect round-trip only.

- **unk_c** is 0 in 3317/3754 files (88%). The remaining 437 have one of only ~60 distinct high-entropy 32-bit values, each shared verbatim across small groups of files that are clearly from the same authoring batch (e.g. three `amb_frog-*.settings` variants all share the same value; two `mf_kipper_*.settings` variants share another). Identical values across otherwise-unrelated file content, with no correlation to sequence/animation count or file size, matches the classic signature of a stale/uninitialized in-memory pointer or heap address serialized directly by the authoring tool's save routine.
- **unk_d** is 0 whenever unk_c is 0, and one of {63, 64, 191, 192} whenever unk_c is non-zero — plausible as bytes adjacent to the same leaked pointer/struct in memory. Same non-semantic origin as unk_c.
- **unk_e** is 0xFFFFFFFF in 3534/3754 files (94%) and 0xF5 in 136/3754 (all large/complex files). The 0xFFFFFFFF value loosely tracks "this file defines no type-2 sequence-group entries" but isn't an exact predicate (234 counterexamples). No exact formula found against sequence count, group-entry count, or animation count.

Further resolution would require either live debugging of the actual authoring tool (never identified) or the tool's source.

## The rare Sequence Groups case

`group_count` is 0 in every real file except one pair (`mf_main_low_hardware.settings` / `mf_main_low_software.settings`, identical content repeated across every client version — genuinely a single unique sample). That file has `group_count=1`, a group named `"Lookat_01"` listing 6 animations. The group entry layout (`entry_count` + name + a trailing unknown `u32`, no per-member payload) was confirmed against that one sample; the animation table starts immediately after via `animation_count` resync.

## Key Details

- Little-endian byte order
- The `declared_sequence_count` in the Sequences section header is NOT reliable (one real file declares 6 but has 465 entries)
- Parser reads Sequences entries until detecting the "Sequence Groups" section marker
- Entry type 2 = sequence group/category name; entry type 3 = animation sequence (confirmed via full-corpus tally: 5422 type-2 and 100085 type-3 entries)
- Entry IDs for type 3 match event codes in the animation table
- `num_transitions` and every animation-table unk field are always 0 across all 100085 real animation-table entries — inter-sequence transitions appear to be unused in practice
- 15 bytes per animation table entry

## References

- kfm.xml (github.com/niftools/kfmxml) — KFM format field definitions (the text-format sibling this binary format shares a data model with)
- Full-corpus statistical survey (3754 real files) and Ghidra byte-pattern search of `legouniverse.exe`
