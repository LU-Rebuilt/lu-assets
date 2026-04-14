#pragma once
#include "microsoft/tga/tga_types.h"

namespace lu::assets {

// Load a TGA image from raw file data. Decodes to RGBA (4 channels) by default.
TgaImage tga_load(std::span<const uint8_t> data);

} // namespace lu::assets
