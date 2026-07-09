#pragma once
#include "lego/brick_geometry/brick_geometry_types.h"

#include <span>

namespace lu::assets {

BrickGeometry brick_geometry_parse(std::span<const uint8_t> data);

} // namespace lu::assets
