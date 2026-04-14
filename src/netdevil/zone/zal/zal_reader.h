#pragma once
#include "netdevil/zone/zal/zal_types.h"

namespace lu::assets {

// Parse ZAL from raw text data.
ZalFile zal_parse(std::span<const uint8_t> data);

} // namespace lu::assets
