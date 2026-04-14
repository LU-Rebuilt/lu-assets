#pragma once
#include "gamebryo/kfm/kfm_types.h"

namespace lu::assets {

KfmFile kfm_parse(std::span<const uint8_t> data);

} // namespace lu::assets
