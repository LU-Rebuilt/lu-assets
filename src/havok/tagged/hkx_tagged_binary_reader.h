#pragma once

#include "havok/tagged/hkx_tagged_binary_types.h"

#include <span>

namespace lu::assets {

// Parses an HKX tagged binary file (magic 0xCAB00D1E 0xD011FACE) into a
// byte-preserving HkxTaggedBinary: structured fileinfo + type table, plus the
// object stream as an opaque blob. Throws HkxTaggedBinaryError on bad magic,
// truncated data, or corrupt/hostile count fields. Does not walk the Havok
// object graph -- see hkx_tagged_binary_types.h for why.
HkxTaggedBinary hkx_tagged_binary_parse(std::span<const uint8_t> data);

} // namespace lu::assets
