#pragma once
#include "microsoft/tga/tga_types.h"

#include <vector>

namespace lu::assets {

// Serialize a TgaFile back to the TGA container format. Byte-identical to the source
// file for any TgaFile produced by tga_parse (18-byte header + verbatim payload).
std::vector<uint8_t> tga_write(const TgaFile& tga);

} // namespace lu::assets
