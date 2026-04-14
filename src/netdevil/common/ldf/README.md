# LDF (LEGO Data Format)

**Extension:** (embedded format, not a standalone file)
**Used by:** LVL object config strings, particle config_data, LUZ path waypoint configs, and network packets throughout the LU client

## Overview

LDF is a typed key-value configuration system used pervasively in LEGO Universe. It appears in two forms: text LDF (in LVL config strings and various embedded contexts) and binary LDF (in LUZ path waypoint data).

## Text LDF Layout

Each entry is one line with the format: `key=type_id:value`

Lines are separated by `\n` (LF). Keys and values are ASCII (after UTF-16LE decode from the source context).

```
Example:
  CheckPrecondition=0:          (wstring, empty value)
  create_physics=7:1            (bool, true)
  modelScale=3:1.5              (float, 1.5)
  objectLOT=1:6326              (s32, 6326)
```

### LdfType Enum

```
ID   Type      Storage       Description
--   ----      -------       -----------
0    WString   string        UTF-16 string (appears as ASCII after conversion)
1    S32       int32_t       Signed 32-bit integer
3    Float     float         Single-precision float
4    Double    double        Double-precision float
5    U32       uint32_t      Unsigned 32-bit integer
7    Bool      bool          Boolean (stored as "0" or "1")
8    U64       uint64_t      Unsigned 64-bit integer
9    ObjId     int64_t       Signed 64-bit object ID (LWOOBJID)
13   Utf8      string        UTF-8 string
```

## Binary LDF Layout (LUZ paths)

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       count — number of key-value pairs
  Per pair (count times):
  +0x00  1    u8        key_len
  +0x01  N    wchar[N]  key — UTF-16LE encoded (N = key_len)
  +N     1    u8        value_len
  +N+1   M    wchar[M]  value — UTF-16LE encoded (M = value_len)
```

## Version

No versioning -- both text and binary LDF use a single format with no version field. The type ID enum is fixed across all usage contexts.

## Key Details

- Text LDF: type IDs 2, 6, 10-12, 14+ are not used in practice
- Text LDF strings are encoded as UTF-16LE in the source file, decoded to ASCII for parsing
- Binary LDF stores raw string pairs without type ID encoding
- The format is used in LVL chunk 2001 (object config), LUZ path waypoints, and network replication

## References

- DarkflameServer LDFFormat.h eLDFType enum (github.com/DarkflameUniverse/DarkflameServer)
- Ghidra RE of legouniverse.exe object config string parsing
