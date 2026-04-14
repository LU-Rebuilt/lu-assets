#pragma once
#include "netdevil/archive/pki/pki_types.h"

#include <span>

namespace lu::assets {

// Parse a primary.pki pack index file.
PkiFile pki_parse(std::span<const uint8_t> data);

} // namespace lu::assets
