#pragma once

#include "netdevil/zone/lvl/lvl_types.h"
#include <vector>
#include <cstdint>

namespace lu::assets {

std::vector<uint8_t> lvl_write(const LvlFile& lvl);

} // namespace lu::assets
