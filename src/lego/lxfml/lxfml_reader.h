#pragma once
#include "lego/lxfml/lxfml_types.h"

namespace lu::assets {

// Parse LXFML from raw XML data (UTF-8 encoded).
LxfmlFile lxfml_parse(std::span<const uint8_t> data);

} // namespace lu::assets
