#pragma once
#include "netdevil/zone/lvl/lvl_types.h"

namespace lu::assets {

LvlFile lvl_parse(std::span<const uint8_t> data);

} // namespace lu::assets
