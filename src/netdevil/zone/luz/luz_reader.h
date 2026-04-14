#pragma once
#include "netdevil/zone/luz/luz_types.h"

namespace lu::assets {

LuzFile luz_parse(std::span<const uint8_t> data);

} // namespace lu::assets
