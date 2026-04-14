# TGA (Truevision TGA) Format

**Extension:** `.tga`
**Used by:** The LEGO Universe client for UI textures and other images

## Overview

TGA is Truevision's raster image format. LU uses TGA files for various UI textures and image assets. The reader decodes TGA data into raw RGBA pixel data using stb_image.

## Binary Layout (Standard TGA)

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    1     u8        id_length — length of image ID field
0x01    1     u8        color_map_type — 0=none, 1=present
0x02    1     u8        image_type — 0=none, 1=mapped, 2=RGB, 3=grey, 9/10/11=RLE
0x03    5     varies    color_map_spec — first entry index(u16), length(u16), entry size(u8)
0x08    2     u16       x_origin
0x0A    2     u16       y_origin
0x0C    2     u16       width
0x0E    2     u16       height
0x10    1     u8        pixel_depth — bits per pixel (8, 16, 24, or 32)
0x11    1     u8        image_descriptor — alpha bits + origin flags
0x12    var   u8[]      image_id — id_length bytes (optional)
+var    var   varies    color_map_data (if color_map_type == 1)
+var    var   u8[]      pixel_data
```

## Version

No versioning -- standard Truevision TGA format with no version field. The `image_type` byte determines the encoding (uncompressed vs. RLE) but there is no format version evolution. LU client files use the original TGA specification.

## Key Details

- Little-endian byte order
- No magic number (identified by extension or header validation)
- Common image types in LU: 2 (uncompressed RGB/RGBA) and 10 (RLE-compressed RGB/RGBA)
- Decoded to raw RGBA pixel buffer by the reader using stb_image
- Standard Truevision format; no LU-specific modifications

## References

- Truevision TGA specification (version 2.0)
- stb_image (github.com/nothings/stb) — decoding library
