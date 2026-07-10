#pragma once

#include "havok/packfile/hkx_packfile_types.h"

#include <cstdint>
#include <vector>

namespace lu::assets {

// Reserializes an HkxPackfile back into its exact original binary layout: 64-byte
// header, section header table, then each section's raw bytes in file order. See
// hkx_packfile_types.h for why this is a pure byte replay rather than a rebuild from a
// semantic object model.
std::vector<uint8_t> hkx_packfile_write(const HkxPackfile& file);

} // namespace lu::assets
