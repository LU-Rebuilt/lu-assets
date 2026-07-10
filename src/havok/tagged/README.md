# HKX Tagged Binary Container

**Extension:** `.hkx` (tagged binary variant only)
**Used by:** The LEGO Universe client for Havok physics collision data and scene geometry, in a smaller subset of files than the binary packfile variant (see `src/havok/packfile/`)

## Overview

This module round-trips the Havok "tagged binary" container format (magic `0xCAB00D1E
0xD011FACE`) byte-for-byte. It is the sibling of `havok/packfile/`, which handles the
*other* `.hkx` container variant (binary packfile, magic `0x57E0E057 0x10C0C010`) — the
two modules share a philosophy (container-level fidelity, no Havok class semantics) but
necessarily differ in shape, because the two wire formats are structurally unrelated:
packfile is a fixed-header-plus-section-table container, while tagged binary is a
variable-length, tag-dispatched stream with an inline string pool and an in-band type
definition table.

A pre-existing, intentionally lossy semantic reader for this format already exists at
`havok/reader/hkx_tagged_reader.{h,cpp}` (namespace `Hkx::`) — it walks the full Havok
object graph to extract rigid body / shape / scene fields for physics analysis. This
module does not reuse or modify that reader (per this project's `.hkx` architecture, the
lossy semantic layer and the byte-perfect container layer are deliberately separate). It
solves a different problem — byte-perfect round-trip — with an independently designed
data model.

`.hkx` also ships as binary packfile (round-tripped by `havok/packfile/`) and XML (out of
scope for this project entirely — zero real client files use it); `hkx_tagged_binary_parse`
rejects both (throws `HkxTaggedBinaryError`) rather than misinterpreting them.

## Binary Layout

Tagged binary is a **tag-dispatched byte stream**, not a fixed-offset container, so there
is no single offset table to document. Every real file (823/823 in the primary corpus,
plus 1704/1704 sampled from five additional client version dumps — see Round-Trip
Coverage below) has exactly this shape, with zero exceptions found:

```
[magic0: u32 = 0xCAB00D1E] [magic1: u32 = 0xD011FACE]
[fileinfo: tag-varint(1) + version-varint]        -- version is always 0 in the real corpus
[type 0: tag-varint(2) + type record]
[type 1: tag-varint(2) + type record]
...
[type N-1: tag-varint(2) + type record]
[object stream: everything else, through EOF]      -- opaque to this module; see below
```

### Varint encoding

All integers in this format (tags, lengths, type fields) use the same variable-length
encoding: the first byte holds a sign bit (bit 0) and 6 value bits (bits 1-6), with bit 7
as a continuation flag; each subsequent byte contributes 7 more value bits, again with
bit 7 as the continuation flag. This is not zigzag-style (protobuf) — the sign is an
explicit bit in the first byte, confirmed via Ghidra RE of `hkBinaryTagfileReader`,
matching `Hkx::HkxTaggedReader::ReadVarInt` in the pre-existing lossy reader exactly (this
is a hard constraint of the wire format, not a design choice either module varies
independently).

A corpus-wide check re-derived the canonical (minimal-length) encoding for every varint in
the header + type-table region and compared it against the original bytes: **all
964,859 varints across all 823 primary-corpus files already use the canonical form** —
none is a non-minimal ("padded") encoding of the same value. This is what licenses parsing
that region into plain `int32_t` fields and *re-encoding* them (rather than storing raw
bytes) while still reproducing the original file exactly.

### String pool

Strings (type names, member names, class names) are stored in an implicit pool built up
as the stream is read: a string's length-prefix varint is either a positive byte count
(a new string, appended to the pool) or `<= 0`, meaning `-value` is an index into
previously-seen strings (a backreference). The pool is **pre-seeded with two empty
strings** at indices 0 and 1 (matching `Hkx::HkxTaggedReader::Parse()`'s
`m_StringPool.push_back("")` called twice), so a fresh string's first occurrence lands at
pool index 2, not 0.

Both pre-seeded slots decode identically (`""`), but real encoders are **not** indifferent
between them: a corpus check found every empty-string backreference in every sampled file
(0/823 in the primary `vanilla_unpacked` corpus — it happens to never reference an empty
string in the header/type-table region at all; 1704/1704 files sampled from five
additional client version dumps — 0.179.12, 0.185.20, 0.190.28, 1.0.8, 1.7.45, 1.9.76 —
where it does occur) always references index **1**, never index 0. The writer's pool
simulation therefore pre-maps `"" -> 1`, not `"" -> 0`, to reproduce that exact
observed convention rather than an arbitrary (but equally valid on decode) choice.

String pool backreferences are pervasive in the type table: 101,502 of them across the
823-file primary corpus (type names get reused heavily as member `className` values), so
the writer's pool simulation must track "first occurrence wins" state identically to the
reader's, not just re-emit fresh strings — verified by the full-corpus round-trip (see
below).

### Type records (wire tag 2)

```
name          : pool string
version       : varint       -- the TYPE's own schema version (not the fileinfo version);
                                 confirmed non-constant across real types, e.g. hkxScene
                                 has version 1 while most types have version 0
parentIndex   : varint       -- 0 = no parent, else a 1-based index into the file's type list
memberCount   : varint
members[memberCount]:
    name        : pool string
    type        : varint (low byte kept; see below)
    tupleSize   : varint     -- only present when type & 0x20 (TupleFlag)
    className   : pool string -- only present when (type & 0x0F) is Object(8) or Struct(9)
```

The member `type` byte encodes a base type in its low 4 bits (0=Void, 1=Byte, 2=Int,
3=Real, 4-7=Vec4/8/12/16, 8=Object, 9=Struct, 10=CString) plus `ArrayFlag` (0x10) and
`TupleFlag` (0x20) — identical to `Hkx::TaggedBaseType` in the lossy reader, since this is
a wire-format constant, not something either module can vary. This module stores `type` as
the raw byte rather than decomposing it into separate fields, so an unrecognized future
base-type value still round-trips correctly (it's opaque data to this module either way).

Across the primary corpus, type tables range from 32 to 100 types per file, with at most
54 members per type and no string longer than 57 bytes — small, well-bounded values that
this module still bounds-checks against a generous sanity ceiling before any
`reserve()`/`resize()`, matching `havok/packfile/`'s `bounded_size()` precedent, since a
corrupt/hostile file could otherwise claim an enormous count.

### Object stream (everything after the type table)

This is where tagged binary's design diverges most from packfile's. The type table is
always immediately followed by exactly one root object (wire tag 3, confirmed the only
value ever used for the root object across all 823 files — tag 4, "remembered object", is
never used at the top level), which recursively contains the rest of the object graph
(nested objects, arrays, structs) via the same tag/type/bitmap dispatch the lossy reader
implements.

**This module does not walk that graph.** The reason is a genuine, confirmed format quirk
that surfaced during the corpus survey, not a shortcut: walking the object stream with the
exact same semantics as the pre-existing, Ghidra-RE-confirmed `Hkx::HkxTaggedReader`
leaves **nonzero trailing, unconsumed bytes in 716 of 823 files (87%)** — not a rare edge
case, the majority case. Deep-diving one example
(`res/BrickModels/ndmade/00000000000000010426.hkx`) traced the discrepancy to a concrete,
reproducible cause: `hkMemoryResourceContainer::resourceHandles` (a `TB_Object` array of
`hkMemoryResourceHandle`) is declared with array length **2** in every sampled file that
has this field, but only ONE tag-prefixed object body is ever actually present at that
position in the stream — the byte where a second element's tag should be instead holds a
value that isn't one of the reader's known dispatch tags (0/3/4/5/6), immediately followed
by more struct-shaped float/int data that the object-graph walk never accounts for. This
looks like a genuine, second Havok-internal encoding convention for resource-handle arrays
that the existing semantic reader doesn't model (`hkMemoryResourceContainer` is Havok
bookkeeping infrastructure, not gameplay-relevant physics data, so the lossy reader was
never extended to cover it fully). Resolving that convention is out of scope for this
module and not needed for it: since this module's job is container-level byte fidelity,
not object semantics, **the entire object stream — from the root object's tag byte through
end of file — is preserved as one opaque raw byte blob**, exactly like a packfile
section's sub-regions. This sidesteps the `resourceHandles` quirk (and any other
undiscovered ones shaped like it) entirely: the bytes are captured and replayed verbatim,
whatever they encode, with zero risk of a subtly-wrong re-derivation.

## Round-Trip Coverage

**823/823** tagged-binary `.hkx` files under `vanilla_unpacked` round-trip byte-identical
(0 parse failures, 0 mismatches). Also verified clean (0 failures) against **1704**
additional tagged-binary files sampled from five other client version dumps (0.179.12,
0.185.20, 0.190.28, 1.0.8, 1.7.45, 1.9.76) — **2527/2527 total, no known exceptions**.

(One mismatch surfaced during that wider sampling before a fix: a file in the 1.9.76 dump
used the empty-string pool backreference convention described above, index 1 rather than
0 — vanilla_unpacked's own 823-file corpus happens not to exercise that path at all. Fixed
by pre-seeding the writer's pool simulation with `"" -> 1`; see the String pool section
above.)

## References

- Ghidra RE of legouniverse.exe — `hkBinaryTagfileReader` varint/tag/object dispatch
- HKXDocs (github.com/SimonNitzsche/HKXDocs) — general HKX format documentation
- `havok/reader/hkx_tagged_reader.{h,cpp}` — the pre-existing lossy semantic reader this
  module's corpus survey cross-checked against (not modified by this module)
- Corpus survey of `vanilla_unpacked` (823 tagged-binary files) and five additional client
  version dumps (1704 files) — see this README and the module-level comment in
  `hkx_tagged_binary_types.h`
