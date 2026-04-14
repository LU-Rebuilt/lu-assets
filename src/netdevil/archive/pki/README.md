# PKI (Pack Index) Format

**Extension:** `.pki` (typically `primary.pki`)
**Used by:** The LEGO Universe client to locate files across multiple `.pk` pack archives

## Overview

The PKI file is a master index that maps CRC32 file path hashes to the specific `.pk` pack file that contains them. The client reads `primary.pki` at startup to know which pack to open for any given asset.

## Binary Layout

```
Offset  Size     Type      Field
------  ----     ----      -----
0x00    4        u32       version (3 in shipped client)

--- Pack path list ---
+0x00   4        u32       pack_count
  Per pack (pack_count times):
  +0x00  4       u32       string_len
  +0x04  N       char[N]   pack_path (backslash-separated, N = string_len)

--- Entry list ---
+0x00   4        u32       entry_count
  Per entry (entry_count times, 20 bytes each):
  +0x00  4       u32       crc — CRC32 hash of the file path
  +0x04  4       s32       lower_crc — lower 32 bits of extended CRC
  +0x08  4       s32       upper_crc — upper 32 bits of extended CRC
  +0x0C  4       u32       pack_index — index into the pack path list
  +0x10  4       u32       unknown
```

## Key Details

- Little-endian byte order
- Version 3 is used in all shipped client files
- Pack paths use backslash separators (normalized to forward slashes by the reader)
- Each entry is exactly 20 bytes
- CRC values match those in the PackIndexEntry structures within `.pk` files
- The `pack_index` field maps directly to the pack path list (0-based index)

## References

- lcdr/lu_formats (github.com/lcdr/lu_formats)
- DarkflameServer (github.com/DarkflameUniverse/DarkflameServer)
