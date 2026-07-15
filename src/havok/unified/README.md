# HKX Unified Reader/Writer

**Purpose:** A single entry point (`hkx_parse()` / `hkx_write()`) for byte-perfect HKX
round-trip, dispatching to whichever of the two supported container formats a file
actually is — callers that just have a `.hkx` file and don't already know its
container format (the common case, since both share the extension) shouldn't have to
sniff magic bytes themselves.

## Why this exists

`.hkx` files ship in two byte-perfect-round-trip-supported container formats:

- **Binary packfile** (magic `0x57E0E057 0x10C0C010`) — `havok/packfile/`
- **Tagged binary** (magic `0xCAB00D1E 0xD011FACE`) — `havok/tagged/`

Before this module, consuming code had to inspect the magic bytes itself and call
`hkx_packfile_parse()` or `hkx_tagged_binary_parse()` accordingly (see, for example,
the pre-unification version of `tests/roundtrip_sweep.cpp`'s `check_hkx()`, which had
its own `packfile_magic`/`tagged_magic` byte arrays). This module centralizes that
detection in exactly one place.

## API

```cpp
#include "havok/unified/hkx_reader.h"
#include "havok/unified/hkx_writer.h"

using lu::assets::HkxAny;      // std::variant<HkxPackfile, HkxTaggedBinary>
using lu::assets::hkx_parse;    // std::span<const uint8_t> -> HkxAny
using lu::assets::hkx_write;    // const HkxAny& -> std::vector<uint8_t>

HkxAny hkx = hkx_parse(data);
if (auto* pf = std::get_if<lu::assets::HkxPackfile>(&hkx)) {
    // pf->header, pf->sections, pf->section_data ...
} else if (auto* tf = std::get_if<lu::assets::HkxTaggedBinary>(&hkx)) {
    // tf->fileInfoVersion, tf->types, tf->objectStream ...
}
auto bytes = hkx_write(hkx); // byte-identical to the original for an unmodified hkx
```

`hkx_parse()` throws `HkxFormatError` if the magic matches neither format (e.g. XML
HKX, or unrelated data — XML has no real-file round-trip support in this project,
since zero real client files use it; see `havok/packfile/README.md` and
`havok/tagged/README.md`). If the magic matches a known format but the rest of the
file is malformed, the underlying format's own error type propagates
(`HkxPackfileError` / `HkxTaggedBinaryError`), not `HkxFormatError`.

## What this module deliberately does NOT do

- It does not parse Havok object contents. Neither does `havok/packfile/` or
  `havok/tagged/` — see their READMEs for why (container-level byte fidelity only;
  the pre-existing, intentionally lossy `Hkx::` reader in `havok/reader/` is the
  separate layer for semantic physics-object analysis).
- It is named `HkxAny`, not `HkxFile`, to avoid reading as the same thing as the
  pre-existing `Hkx::HkxFile` class (a stateful parser in the lossy layer) — different
  namespace, but similar names for genuinely different kinds of things would be
  confusing.
- It does not support XML HKX. No real client file uses that format.

## Round-Trip Coverage

Combining both underlying formats: all 4195 real `.hkx` files under
`vanilla_unpacked` round-trip byte-identical via `hkx_parse()`/`hkx_write()` (3372
binary packfile + 823 tagged binary, 0 mismatches, 0 parse failures) — see
`havok/packfile/README.md` and `havok/tagged/README.md` for the per-format corpus
survey details and discovered format quirks.

## References

- `havok/packfile/` and `havok/tagged/` — the two format-specific modules this
  dispatches to.
