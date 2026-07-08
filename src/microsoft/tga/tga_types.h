#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// TGA (Truevision TGA) image loader using stb_image.
// LU uses TGA files for UI textures and other images.
// This loader decodes TGA data into raw RGBA pixel data.

struct TgaError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct TgaImage {
    int width = 0;
    int height = 0;
    int channels = 0;               // Number of channels in the decoded data
    std::vector<uint8_t> pixel_data; // Raw pixel data (width * height * channels bytes)
};
// Structural (container-level) view of a TGA file: the 18-byte header parsed field by
// field, everything after it carried verbatim. This is the round-trip path — tga_write
// re-emits it byte-identically. tga_load above is the *decode* path (pixels out, RLE and
// header discarded); decoding is inherently one-way, so editors that need to write TGA
// pixels must re-encode into `payload` themselves.
struct TgaFile {
    // Header fields (Truevision TGA spec, 18 bytes little-endian)
    uint8_t id_length = 0;        // length of the image ID field after the header
    uint8_t color_map_type = 0;   // 0 = none, 1 = palette present
    uint8_t image_type = 0;       // 2 = uncompressed truecolor, 10 = RLE truecolor, ...
    uint16_t color_map_first = 0; // first palette entry index
    uint16_t color_map_length = 0;
    uint8_t color_map_depth = 0;  // bits per palette entry
    uint16_t x_origin = 0;
    uint16_t y_origin = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t bits_per_pixel = 0;
    uint8_t descriptor = 0;       // alpha depth + origin corner flags

    // Every byte after the 18-byte header, verbatim: image ID, palette, (possibly RLE)
    // pixel data, and the optional TGA 2.0 footer/extension area.
    std::vector<uint8_t> payload;
};
} // namespace lu::assets

