#pragma once

#include "havok/tagged/hkx_tagged_binary_types.h"

#include <cstdint>
#include <vector>

namespace lu::assets {

// Reserializes an HkxTaggedBinary back into its exact original byte layout:
// magic, fileinfo tag, type table (re-encoded from structured fields via a
// string pool simulation identical to the reader's), then the opaque object
// stream blob replayed verbatim. See hkx_tagged_binary_types.h for why the
// type table can be safely re-encoded (rather than replayed as raw bytes)
// while the object stream cannot.
std::vector<uint8_t> hkx_tagged_binary_write(const HkxTaggedBinary& file);

} // namespace lu::assets
