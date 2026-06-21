#pragma once

#include "netdevil/zone/luz/luz_types.h"
#include <vector>
#include <cstdint>

namespace lu::assets {

std::vector<uint8_t> luz_write(const LuzFile& luz);

} // namespace lu::assets
