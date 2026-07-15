#pragma once

#include "havok/unified/hkx_types.h"

#include <span>

namespace lu::assets {

// Parses any byte-perfect-round-trip-supported HKX file, detecting the container
// format from its magic bytes and dispatching to hkx_packfile_parse() or
// hkx_tagged_binary_parse() accordingly. Callers that don't already know which of
// the two formats a file is (the common case — both share the ".hkx" extension)
// should use this instead of sniffing magic bytes themselves.
//
// Throws HkxFormatError if the magic doesn't match either supported format (e.g.
// XML — see hkx_types.h), or the underlying format-specific parser's own error type
// (HkxPackfileError / HkxTaggedBinaryError) if the magic matches but the rest of the
// file is malformed.
HkxAny hkx_parse(std::span<const uint8_t> data);

} // namespace lu::assets
