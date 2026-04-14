#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// DDS (DirectDraw Surface) texture header parser.
// Standard Microsoft DDS format used for all LU textures (16,448 files).
// Format reference: https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-packing
//
// DDS files start with magic "DDS " (0x20534444) followed by a 124-byte header.
// Pixel data follows the header (and optional DX10 extended header).

struct DdsError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr uint32_t DDS_MAGIC = 0x20534444; // "DDS "

// DDS pixel format flags
enum DdsPixelFormatFlags : uint32_t {
    DDPF_ALPHAPIXELS = 0x1,
    DDPF_ALPHA       = 0x2,
    DDPF_FOURCC      = 0x4,
    DDPF_RGB         = 0x40,
    DDPF_LUMINANCE   = 0x20000,
    DDPF_BUMPDUDV    = 0x80000,
};

// Common FourCC codes
inline constexpr uint32_t FOURCC_DXT1 = 0x31545844; // "DXT1"
inline constexpr uint32_t FOURCC_DXT3 = 0x33545844; // "DXT3"
inline constexpr uint32_t FOURCC_DXT5 = 0x35545844; // "DXT5"
inline constexpr uint32_t FOURCC_DX10 = 0x30315844; // "DX10"

struct DdsPixelFormat {
    uint32_t size;          // Must be 32
    uint32_t flags;
    uint32_t four_cc;       // FourCC code (DXT1, DXT3, DXT5, etc.)
    uint32_t rgb_bit_count;
    uint32_t r_bit_mask;
    uint32_t g_bit_mask;
    uint32_t b_bit_mask;
    uint32_t a_bit_mask;
};

struct DdsHeader {
    uint32_t size;          // Must be 124
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitch_or_linear_size;
    uint32_t depth;
    uint32_t mip_map_count;
    uint32_t reserved1[11];
    DdsPixelFormat pixel_format;
    uint32_t caps;
    uint32_t caps2;
    uint32_t caps3;
    uint32_t caps4;
    uint32_t reserved2;
};

struct DdsFile {
    DdsHeader header;
    size_t data_offset;     // Byte offset where pixel data starts
    uint32_t width;
    uint32_t height;
    uint32_t mip_count;
    bool is_compressed;     // True if DXT compressed
    uint32_t four_cc;       // FourCC code if compressed
    uint32_t bits_per_pixel; // If uncompressed
};
} // namespace lu::assets
