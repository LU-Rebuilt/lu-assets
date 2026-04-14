# FDB (Flat DataBase) Format

**Extension:** `.fdb` (typically `cdclient.fdb`)
**Used by:** The LEGO Universe client as its primary game data database (items, NPCs, skills, missions, etc.)

## Overview

FDB is NetDevil's flat-file database format used for `cdclient.fdb`, the central game data store. It uses pointer-based indirection with hash-bucketed rows, designed for memory-mapped random access.

## Binary Layout

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       table_count

--- Table pointer array (table_count entries, 8-byte stride) ---
  Per table:
  +0x00  4    u32       table_header_ptr — file offset to table header

--- Table header (at table_header_ptr) ---
  +0x00  4    u32       column_count
  +0x04  4    u32       table_name_ptr — file offset to null-terminated string

  --- Column definitions (column_count entries) ---
    Per column:
    +0x00  4  u32       data_type (see FdbDataType enum)
    +0x04  4  u32       column_name_ptr — file offset to null-terminated string

  --- Row buckets ---
  +0x00  4    u32       row_bucket_count (always power of 2)
    Per bucket (row_bucket_count entries):
    +0x00  4  u32       row_ptr — file offset to row_info (-1 = empty)

--- Row info (at row_ptr) ---
  +0x00  4    u32       column_count
    Per field (column_count entries):
    +0x00  4  u32       data_type
    +0x04  4-8 varies   value (type-dependent: 4 or 8 bytes)
  +N     4    u32       next_row_ptr — file offset to next row (-1 = end of chain)
```

### FdbDataType Enum

```
Value  Type      Size    Description
-----  ----      ----    -----------
0      NOTHING   4       NULL (4 bytes of zeros)
1      INT32     4       32-bit signed integer
3      REAL      4       32-bit float
4      TEXT_4    4       String via 4-byte pointer indirection
5      BOOL      4       32-bit boolean (0 or 1)
6      INT64     8       64-bit signed integer via pointer indirection
8      TEXT_8    4       String via pointer indirection (same as TEXT_4)
```

## Key Details

- Little-endian byte order
- No magic number or file header beyond the table count
- All string/int64 values use pointer indirection (file offsets to actual data)
- Row buckets use hash chaining (linked list via next_row_ptr)
- Values 2 and 7 in the data type enum are unused gaps
- Designed for memory-mapped access; all offsets are absolute file positions
- Verified against legouniverse.exe: CreateDataCache @ 0x00fae126, LoadFDBFile @ 0x00faa180

## References

- lcdr/utils fdb_to_sqlite (github.com/lcdr/utils)
- DarkflameServer CDClient table usage (github.com/DarkflameUniverse/DarkflameServer)
- Ghidra RE of legouniverse.exe @ FUN_00776530
