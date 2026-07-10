#pragma once

#include "havok/unified/hkx_types.h"

#include <vector>

namespace lu::assets {

// Serializes an HkxAny back to bytes, dispatching to hkx_packfile_write() or
// hkx_tagged_binary_write() based on which alternative is active. Byte-identical to
// the source file for any HkxAny produced by hkx_parse().
std::vector<uint8_t> hkx_write(const HkxAny& file);

} // namespace lu::assets
