#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// GFX (Scaleform GFx) file parser.
// 1,010 .gfx files in the client's ui/ and textures/ directories.
//
// GFx files are Scaleform's modified SWF (Flash) format.
//
// === File Header (8 bytes, uncompressed) ===
//   [0] u8[3]  magic    — "GFX" (uncompressed) or "CFX" (zlib-compressed)
//                         Also accept standard SWF: "FWS" / "CWS"
//   [3] u8     version  — SWF version (10 for LU client files)
//   [4] u32    file_len — uncompressed file length (LE). NOTE: may not match
//                         actual decompressed size; approximate in some SWF files.
//
//   If CFX/CWS: bytes [8..] are a zlib-compressed stream.
//
// === SWF Body (after decompression, starting at byte 8) ===
//   RECT frame_size  — frame bounding box in TWIPS (1 TWIP = 1/20 pixel)
//     Bit layout: [5-bit Nbits][Nbits×Xmin][Nbits×Xmax][Nbits×Ymin][Nbits×Ymax]
//     All values are signed. Total = 5 + 4*Nbits bits, padded to byte boundary.
//   u16  frame_rate  — 8.8 fixed-point (fps = frame_rate / 256)
//   u16  frame_count — number of frames
//
// === SWF Tag Records (follow immediately after frame_count) ===
//   Each tag:
//     u16  record_header  — bits [15:6] = tag_type, bits [5:0] = short_len
//       if short_len == 63 (0x3F): u32 long_len follows; use long_len
//       else: use short_len
//     u8[] data           — tag_len bytes of tag payload
//
//   Tag types include:
//     0   = End              — terminates tag stream
//     2   = DefineShape
//     9   = SetBackgroundColor
//     11  = DefineText
//     12  = DoAction         — ActionScript bytecode
//     26  = PlaceObject2
//     37  = DefineEditText   — editable text field
//     39  = DefineSprite     — movie clip
//     43  = FrameLabel
//     48  = DefineFont2
//     56  = ExportAssets
//     59  = DoInitAction
//     69  = FileAttributes
//     75  = DefineFont3
//     76  = SymbolClass
//     82  = DoABC            — ActionScript 3 bytecode
//     88  = DefineFontName
//     1000 = GFx custom tag (Scaleform-specific; format undocumented)
//     1001 = GFx custom tag (Scaleform-specific; format undocumented)
//     ... other Scaleform extension tags
//
//   All tag payloads are stored as raw bytes. Full SWF/GFx tag spec:
//   SWF 19 specification (Adobe) for standard tags.
//   Scaleform-specific tags (≥ 1000) are proprietary; raw storage only.

struct GfxError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// A parsed SWF/GFx tag record.
struct GfxTag {
    uint16_t type = 0;            // SWF tag type (0 = End, see above)
    std::vector<uint8_t> data;    // Raw tag payload bytes
};

// Parsed SWF frame bounding box (in TWIPS; 1 TWIP = 1/20 px).
struct GfxRect {
    int32_t x_min = 0;
    int32_t x_max = 0;
    int32_t y_min = 0;
    int32_t y_max = 0;
};

struct GfxFile {
    // Header
    bool     is_compressed       = false;
    uint8_t  swf_version         = 0;
    uint32_t file_length_field   = 0;  // raw value from header (may be approximate)

    // SWF body
    GfxRect  frame_size;
    uint16_t frame_rate  = 0;  // 8.8 fixed-point; fps = frame_rate / 256
    uint16_t frame_count = 0;

    // All tag records including the terminal End tag
    std::vector<GfxTag> tags;

    // Full decompressed SWF bytes (no gaps)
    std::vector<uint8_t> swf_data;
};
} // namespace lu::assets
