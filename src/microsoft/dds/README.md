# DDS (DirectDraw Surface) Format

**Extension:** `.dds`
**Used by:** The LEGO Universe client for all textures (16,448 files), including diffuse, normal, and specular maps

## Overview

DDS is Microsoft's standard texture format for DirectX applications. LU uses DXT1, DXT3, and DXT5 block-compressed textures as well as uncompressed RGB/RGBA formats with optional mipmaps.

## Binary Layout

```
Offset  Size  Type            Field
------  ----  ----            -----
0x00    4     u32             magic — 0x20534444 ("DDS ")

--- DDS Header (124 bytes) ---
0x04    4     u32             size — must be 124
0x08    4     u32             flags
0x0C    4     u32             height
0x10    4     u32             width
0x14    4     u32             pitch_or_linear_size
0x18    4     u32             depth
0x1C    4     u32             mip_map_count
0x20    44    u32[11]         reserved1

--- DDS Pixel Format (32 bytes, embedded at offset 0x4C) ---
0x4C    4     u32             pf_size — must be 32
0x50    4     u32             pf_flags
0x54    4     u32             four_cc — FourCC code (DXT1/DXT3/DXT5/DX10)
0x58    4     u32             rgb_bit_count
0x5C    4     u32             r_bit_mask
0x60    4     u32             g_bit_mask
0x64    4     u32             b_bit_mask
0x68    4     u32             a_bit_mask

0x6C    4     u32             caps
0x70    4     u32             caps2
0x74    4     u32             caps3
0x78    4     u32             caps4
0x7C    4     u32             reserved2

--- Pixel Data ---
0x80    ...                   pixel/block data follows header
```

### Common FourCC Codes

```
Value        Hex          Description
-----        ---          -----------
"DXT1"       0x31545844   BC1 — 4:1 compression, 1-bit alpha
"DXT3"       0x33545844   BC2 — 4:1 compression, explicit 4-bit alpha
"DXT5"       0x35545844   BC3 — 4:1 compression, interpolated alpha
"DX10"       0x30315844   Extended header follows (DX10+)
```

### Pixel Format Flags

```
Flag                Value       Description
----                -----       -----------
DDPF_ALPHAPIXELS    0x00001     Alpha channel present
DDPF_FOURCC         0x00004     FourCC code valid (compressed)
DDPF_RGB            0x00040     Uncompressed RGB data
DDPF_LUMINANCE      0x20000     Luminance data
```

## Key Details

- Little-endian byte order
- Magic: `0x20534444` ("DDS " with trailing space)
- Header is always 124 bytes; pixel format struct is always 32 bytes
- Pixel data starts at offset 0x80 (128 bytes from file start)
- Mipmap chain follows the base level if mip_map_count > 1
- Standard Microsoft format; no LU-specific modifications

## References

- Microsoft DDS documentation: https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-packing
