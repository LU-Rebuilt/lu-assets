# Settings (NiKFMTool Binary) Format

**Extension:** `.settings`
**Used by:** The LEGO Universe client (737 files in mesh/ directory) to define animation sequences, sequence groups, and inter-sequence transitions for actors

## Overview

Settings files are a binary encoding of Gamebryo's NiKFMTool animation data. They define which animation sequences an actor supports, how they are grouped into categories, and transition rules between animations. Written by NetDevil's SequenceEditor.exe build tool.

## Binary Layout

```
--- Header ---
Offset  Size  Type      Field
------  ----  ----      -----
+0x00   1     u8        version_len
+0x01   N     char[N]   version — always "2.3.0" in client files
+var    4     u32       header_flags — always 1
+var    4     u32       model_filename_len — always 0 (empty NIF filename)
+var    4     u32       unk_c — varies (0 in simple files)
+var    1     u8        unk_d — varies (0, 63, 64, or 192)
+var    4     u32       unk_e — varies (0, 0xF5, or 0xFFFFFFFF)
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
+var    4     u32       group_count — always 0 in observed files

--- Animation Table ---
+var    4     u32       animation_count
  Per animation (15 bytes each):
  +0x00  4    u32       event_code — NiKFMTool sequence ID
  +0x04  4    u32       num_transitions — transition count (often 0)
  +0x08  4    u32       unk_field — always 0
  +0x0C  1    u8        unk_byte_1 — always 0
  +0x0D  1    u8        unk_byte_2 — always 0
  +0x0E  1    u8        unk_byte_3 — always 0

--- Footer ---
  Variable-length remaining data stored as raw bytes.
```

## Key Details

- Little-endian byte order
- Version string is always "2.3.0" in all client files
- The declared_sequence_count in the Sequences section header is NOT reliable (e.g. mf_darkling.settings declares 6 but has 465 entries)
- Parser reads entries until detecting the "Sequence Groups" section marker
- Entry type 2 = sequence group/category name; entry type 3 = animation sequence
- Entry IDs for type 3 match event codes in the animation table
- 15 bytes per animation table entry

## References

- kfm.xml (github.com/niftools/kfmxml) — NiKFMTool format specification
- Ghidra RE of NiKFMTool::ReadBinaryKFM
