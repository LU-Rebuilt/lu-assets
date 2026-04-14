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
} // namespace lu::assets
