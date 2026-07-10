# HKX Binary Packfile Container

**Extension:** `.hkx` (binary packfile variant only)
**Used by:** The LEGO Universe client for Havok physics collision data (rigid bodies, shapes, physics systems)

## Overview

This module round-trips the Havok "binary packfile" container format byte-for-byte, using
the same raw-block-preservation design as `gamebryo/nif`: the container-level structure
(header, section header table, per-section raw bytes) is preserved and replayed verbatim.
Individual Havok class fields (hkpRigidBody internals, shape internals, fixup *entries*)
are never parsed — those bytes are opaque payload as far as this module is concerned. That
semantic parsing is a separate, pre-existing, intentionally lossy layer
(`havok/reader/`, namespace `Hkx::`) used for physics-object analysis; this module does not
touch or replace it.

`.hkx` also ships as tagged binary (magic `0xCAB00D1E 0xD011FACE`) and XML — both out of
scope here; `hkx_packfile_parse` rejects them (throws `HkxPackfileError`) rather than
misinterpreting them.

## Binary Layout

### File Header (64 bytes)

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       magic0 — 0x57E0E057
0x04    4     u32       magic1 — 0x10C0C010
0x08    4     u32       userTag
0x0C    4     u32       fileVersion — 4, 5, 6, or 7 in the real corpus
0x10    1     u8        pointerSize — always 4 (32-bit)
0x11    1     u8        littleEndian — always 1
0x12    1     u8        reusePaddingOptimization — always 0
0x13    1     u8        emptyBaseClassOptimization — always 1
0x14    4     u32       numSections — always 3
0x18    4     u32       contentsSectionIndex — section holding the root object
0x1C    4     u32       contentsSectionOffset — always 0
0x20    4     u32       contentsClassNameSectionIndex — always 0 (__classnames__)
0x24    4     u32       contentsClassNameSectionOffset
0x28    16    char[16]  contentsVersion — e.g. "Havok-7.1.0-r1\0", last byte always 0xFF
0x38    4     u32       flags — always 0xFFFFFFFF
0x3C    2     i16       maxPredicate — always -1
0x3E    2     i16       predicateArraySizePlusPadding — always -1
```

### Section Header (48 bytes each, table starts at offset 0x40)

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    20    char[20]  sectionTag — e.g. "__classnames__\0", last byte always 0xFF
0x14    4     u32       absoluteDataStart — file-relative offset of this section's bytes
0x18    4     u32       localFixupsOffset — section-relative: end of data / start of local fixups
0x1C    4     u32       globalFixupsOffset — end of local fixups / start of global fixups
0x20    4     u32       virtualFixupsOffset — end of global fixups / start of virtual fixups
0x24    4     u32       exportsOffset — end of virtual fixups / start of exports
0x28    4     u32       importsOffset — end of exports / start of imports
0x2C    4     u32       endOffset — end of imports / total section length
```

### File shape

```
[header: 64 bytes]
[section header table: numSections * 48 bytes]
[section 0 bytes: data | localFixups | globalFixups | virtualFixups | exports | imports]
[section 1 bytes: ...]
[section 2 bytes: ...]
```

Sections are perfectly contiguous with no inter-section padding and no footer: the first
section starts immediately at `64 + numSections*48`, and
`section[i].absoluteDataStart + section[i].endOffset == section[i+1].absoluteDataStart` for
every real file sampled. The last section's end equals the file size exactly.

## The trailing 0xFF sentinel byte

Both fixed-size string fields (`contentsVersion`, `sectionTag`) are zero-padded after their
NUL terminator **except the field's very last byte, which is always `0xFF`**, never `0x00`.
This held across every one of 3372 real packfile files surveyed — e.g. `contentsVersion`
bytes 0-13 are `"Havok-7.1.0-r1"`, byte 14 is `0x00`, and byte 15 (the last of the 16-byte
field) is `0xFF`. The likely origin is a Havok debug/tracker buffer-fill pattern baked into
the exporter, but regardless of origin it's constant, so the writer hardcodes it
(`write_fixed_str_havok_padded` in `hkx_packfile_writer.cpp`) instead of storing it as a
per-file field.

## Fixup sub-regions are preserved as raw bytes, not parsed entries

A section's bytes after the data payload split into local fixups, global fixups, virtual
fixups, exports, and imports, per the section header's own offset fields. Initial
inspection assumed a fixed entry size per table (local: 8 bytes = `(srcOffset, dstOffset)`;
global/virtual: 12 bytes = `(offset, sectionIndex/classname, offset)`), and that assumption
mostly holds for the visible fields — but a deeper per-file check found global/virtual
fixup region byte-lengths aren't always a clean multiple of 12: about half the corpus has a
trailing partial "entry" of 4 or 8 extra `0xFFFFFFFF` bytes that isn't a fixed-size
sentinel record, just leftover fill from however the original array was
grown-then-trimmed. Rather than model a variable-length terminator convention, each
sub-region (`data`, `localFixups`, `globalFixups`, `virtualFixups`, `exports`, `imports`)
is preserved as one opaque byte blob per section — the region's total length is already
fully determined by the section header's offset fields, and the bytes are simply not
Havok class data this module needs to interpret (per the "container-level, not
object-level" scope for this task). Exports and imports regions were empty (`0` bytes) in
every real file sampled.

## Versions

| Havok Version | Packfile `fileVersion` | `contentsVersion` string | Files in `vanilla_unpacked` corpus |
|---|---|---|---|
| Havok 4.5 | 4 | `Havok-4.5.0-r1` | 10 |
| Havok 5.1 | 5 | `Havok-5.1.0-r1` | 68 |
| Havok 6.5 | 6 | `Havok-6.5.0-r1` | 180 |
| Havok 7.0 | 7 | `Havok-7.0.0-r1` | 463 |
| Havok 7.1 | 7 | `Havok-7.1.0-r1` | 2651 |

Packfile version 7 covers both Havok 7.0 and 7.1 — the version number alone doesn't
distinguish them, only `contentsVersion` does. Every file in every client version sampled
(`vanilla_unpacked`, 1.7.45, 1.9.76, 1.10.64, 0.190.28) has `pointerSize == 4` and
`littleEndian == 1`; no 64-bit-pointer or big-endian packfile ships.

`numSections` is always 3, with tags `__classnames__`, `__types__`, `__data__`.
`__classnames__` is always first; the other two swap order (`classnames, types, data` in
~98% of files, `classnames, data, types` in the remainder) — both orders parse and
round-trip correctly since section identity comes from the tag string, not position.

## Round-Trip Coverage

3372/3372 packfile-format `.hkx` files under `vanilla_unpacked` round-trip byte-identical
(0 parse failures, 0 mismatches). Also verified clean (0 failures) against the full `.hkx`
corpora of client versions 1.7.45 (3262 files), 1.9.76 (3287 files), 1.10.64 (3372 files),
and 0.190.28 (59 files) — no known exceptions.

## References

- Ghidra RE of legouniverse.exe — packfile header/section structure
- HKXDocs (github.com/SimonNitzsche/HKXDocs) — general HKX format documentation
- Corpus survey of `vanilla_unpacked` (3372 packfile files) and four additional client
  version dumps — see the module-level comment in `hkx_packfile_types.h`
