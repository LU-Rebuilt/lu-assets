# HKX Readers

Parses HKX files in three formats, auto-detected by magic bytes:

| File | Format | Magic |
|------|--------|-------|
| `hkx_reader.h/cpp` | Binary packfile dispatcher | `0x57E0E057` |
| `hkx_binary_reader.cpp` | Binary packfile extraction | (internal) |
| `hkx_binary_offsets.h` | Field offset constants | (internal) |
| `hkx_tagged_reader.h/cpp` | Tagged binary format | `0xCAB00D1E` |
| `hkx_xml_reader.h/cpp` | Havok XML format | `<?xml` / `<hkpackfile>` |

All readers produce the same `ParseResult` output (defined in [../types/](../types/)).
