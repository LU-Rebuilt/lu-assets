#pragma once

#include "havok/packfile/hkx_packfile_types.h"

#include <span>

namespace lu::assets {

// Parses an HKX binary packfile (magic 0x57E0E057 0x10C0C010) into a byte-preserving
// HkxPackfile. Throws HkxPackfileError on bad magic, truncated data, or corrupt/hostile
// count fields. Does not parse Havok object contents — see hkx_packfile_types.h.
HkxPackfile hkx_packfile_parse(std::span<const uint8_t> data);

} // namespace lu::assets
