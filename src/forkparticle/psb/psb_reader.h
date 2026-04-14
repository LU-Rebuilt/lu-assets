#pragma once
#include "forkparticle/psb/psb_types.h"

namespace lu::assets {

// Parse a PSB file. Throws PsbError on structural errors.
// data must be the complete file contents (not just the header).
PsbFile psb_parse(std::span<const uint8_t> data);

} // namespace lu::assets
