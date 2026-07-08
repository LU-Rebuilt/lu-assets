#pragma once
#include "netdevil/zone/zal/zal_types.h"

#include <vector>

namespace lu::assets {

// Serialize back to the on-disk text format. Byte-identical to the source file for any
// ZalFile produced by zal_parse: emits the preserved verbatim lines.
std::vector<uint8_t> zal_write(const ZalFile& f);

} // namespace lu::assets
