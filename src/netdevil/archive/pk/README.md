# PK (NDPK) Archive Format

**Extension:** `.pk`
**Used by:** The LEGO Universe client to store game assets in packed archive files

## Overview

PK files are NetDevil's archive format for bundling game assets. Each `.pk` file contains compressed or uncompressed file data with a table of contents (TOC) at the end of the file, indexed by CRC32 hashes of file paths.

## Binary Layout

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    7     u8[7]     magic — "ndpk" + 0x01 0xFF 0x00

--- File data entries, separated by 5-byte dividers ---

Each divider: FF 00 00 DD 00

--- Table of Contents (at end of file) ---

+0x00   4     u32       entry_count
+0x04   N*100 struct[]  PackIndexEntry[entry_count]
+N      4     u32       toc_offset — absolute offset to entry_count
+N+4    4     u32       file_revision
```

### PackIndexEntry (100 bytes)

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       crc — CRC32 hash of the file path
0x04    4     s32       lower_crc — lower 32 bits of extended CRC (-1 if unused)
0x08    4     s32       upper_crc — upper 32 bits of extended CRC (-1 if unused)
0x0C    4     u32       uncompressed_size — original file size in bytes
0x10    32    char[32]  uncompressed_hash — MD5 hash of uncompressed data (hex ASCII)
0x30    4     u32       padding1
0x34    4     u32       compressed_size — compressed data size (0 or same if not compressed)
0x38    32    char[32]  compressed_hash — MD5 hash of compressed data (hex ASCII)
0x58    4     u32       padding2
0x5C    4     u32       data_offset — absolute file offset to data in .pk file
0x60    4     u32       is_compressed — low byte: 1=SD0 compressed, 0=uncompressed
```

## Version

PK files have a 7-byte magic header `"ndpk"` + `0x01 0xFF 0x00` where the `0x01` byte serves as a format version indicator. A `file_revision` field (u32) is stored at the very end of the file, after the TOC offset. There is no version-conditional parsing -- all LU client PK files share the same binary layout.

## Key Details

- Little-endian byte order
- Magic bytes: `6E 64 70 6B 01 FF 00` ("ndpk" + version bytes)
- Data divider between entries: `FF 00 00 DD 00`
- Compressed entries use SD0 compression (see sd0/ module)
- Files are located by CRC32 hash, not by name (the PKI index maps CRC to pack)
- Verified against legouniverse.exe FUN_01037410

## References

- lcdr/lu_formats pk.ksy (github.com/lcdr/lu_formats)
- DarkflameServer Pack.h PackRecord (github.com/DarkflameUniverse/DarkflameServer)
- Ghidra RE of legouniverse.exe @ FUN_01037410
