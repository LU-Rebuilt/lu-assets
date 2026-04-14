# GFX (Scaleform GFx) Format

**Extension:** `.gfx`
**Used by:** The LEGO Universe client (1,010 files in ui/ and textures/) for all UI elements, menus, HUD, and Flash-based interfaces

## Overview

GFX files are Scaleform's modified SWF (Adobe Flash) format. They contain vector graphics, bitmap assets, ActionScript bytecode, and UI layout data. Files may be uncompressed or zlib-compressed.

## Binary Layout

### File Header (8 bytes, always uncompressed)

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    3     u8[3]     magic — "GFX" (uncompressed) or "CFX" (zlib-compressed)
                        Also accepts "FWS" / "CWS" (standard SWF)
0x03    1     u8        version — SWF version (10 for LU client files)
0x04    4     u32       file_length — uncompressed file length (LE, approximate)
```

If magic is "CFX" or "CWS": bytes 8 onward are a zlib-compressed stream.

### SWF Body (after decompression, starting at byte 8)

```
Offset  Size    Type      Field
------  ----    ----      -----
+0x00   var     RECT      frame_size — bounding box in TWIPS (1 TWIP = 1/20 pixel)
                          Bit layout: [5-bit Nbits][Nbits*Xmin][Nbits*Xmax]
                                      [Nbits*Ymin][Nbits*Ymax]
                          Padded to byte boundary
+var    2       u16       frame_rate — 8.8 fixed-point (fps = value / 256)
+var    2       u16       frame_count
```

### SWF Tag Records

```
Per tag:
+0x00   2       u16       record_header
                          bits [15:6] = tag_type
                          bits [5:0]  = short_len
                          if short_len == 0x3F: u32 long_len follows
+0x02   N       u8[N]     data — tag payload (N = resolved length)
```

### Common Tag Types

```
Type  Name               Description
----  ----               -----------
0     End                Terminates tag stream
2     DefineShape        Vector shape definition
9     SetBackgroundColor Background color
11    DefineText         Static text
12    DoAction           ActionScript 2 bytecode
26    PlaceObject2       Object placement
37    DefineEditText     Editable text field
39    DefineSprite       Movie clip
69    FileAttributes     File metadata flags
82    DoABC              ActionScript 3 bytecode
1000+ GFx extension tags Scaleform-proprietary (undocumented)
```

## Key Details

- Little-endian byte order
- Magic: "GFX" (0x474658) or "CFX" (0x434658) for Scaleform; "FWS"/"CWS" for standard SWF
- SWF version 10 in all LU client files
- RECT frame_size uses variable-length bit packing (5-bit count + 4 signed fields)
- Tags with short_len == 63 use a following u32 for the actual length
- Scaleform extension tags (type >= 1000) are proprietary; payloads stored as raw bytes
- file_length field may not exactly match actual decompressed size

## References

- SWF 19 specification (Adobe) — standard SWF tag definitions
- Scaleform GFx documentation (proprietary extension tags)
