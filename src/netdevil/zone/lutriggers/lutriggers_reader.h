#pragma once
#include "netdevil/zone/lutriggers/lutriggers_types.h"

namespace lu::assets {

// Parse LU trigger XML from raw data.
LuTriggersFile lutriggers_parse(std::span<const uint8_t> data);

} // namespace lu::assets
