#pragma once
#include "gamebryo/nif/nif_types.h"

namespace lu::assets {

NifFile nif_parse(std::span<const uint8_t> data);

} // namespace lu::assets
