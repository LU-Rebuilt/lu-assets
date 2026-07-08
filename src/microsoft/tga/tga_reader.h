#pragma once
#include "microsoft/tga/tga_types.h"

namespace lu::assets {

// Load a TGA image from raw file data. Decodes to RGBA (4 channels) by default.
TgaImage tga_load(std::span<const uint8_t> data);

// Parse the TGA container structurally (header fields + verbatim payload) without
// decoding pixels. Round-trips byte-identically through tga_write.
TgaFile tga_parse(std::span<const uint8_t> data);

} // namespace lu::assets
