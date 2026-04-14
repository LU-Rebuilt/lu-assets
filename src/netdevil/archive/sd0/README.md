# SD0 (Segmented Data 0) Compression Format

**Extension:** (internal, embedded within `.pk` archives)
**Used by:** PK archives to compress individual file entries

## Overview

SD0 is NetDevil's chunked compression wrapper around zlib. Compressed data is split into independently decompressible chunks of approximately 256 KB each, allowing partial decompression.

## Binary Layout

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    5     u8[5]     magic — "sd0" + 0x01 + 0xFF

--- Repeating chunks until end of data ---

+0x00   4     u32       compressed_size — size of the following zlib block
+0x04   N     u8[N]     zlib_data — zlib-compressed chunk (N = compressed_size)
```

## Version

The SD0 magic bytes include a version byte (`0x01`) at offset 3, but no version-conditional parsing exists. A single format is used by all LU client PK archives.

## Key Details

- Little-endian byte order
- Magic bytes: `73 64 30 01 FF` ("sd0" + version byte + terminator)
- Header is exactly 5 bytes
- Each chunk decompresses independently to approximately 256 KB (262,144 bytes)
- Chunks repeat until all compressed data is consumed
- Standard zlib (RFC 1950) compression within each chunk
- Used exclusively inside PK archives for compressed file entries

## References

- lcdr/lu_formats sd0.ksy (github.com/lcdr/lu_formats)
