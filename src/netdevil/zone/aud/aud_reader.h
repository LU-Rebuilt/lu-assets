#pragma once
#include "netdevil/zone/aud/aud_types.h"

namespace lu::assets {

// Parse AUD XML from raw data.
AudFile aud_parse(std::span<const uint8_t> data);

} // namespace lu::assets
