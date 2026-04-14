#pragma once
// psb_writer.h — Write PSB emitter files from PsbFile structs.

#include "forkparticle/psb/psb_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lu::assets {

// Serialize a PsbFile back to binary PSB format.
// The original file data is needed to preserve the texture array,
// animation data, and emitter name that follow the 420-byte main block.
// If original_data is empty, only the main block is written (no textures/anim).
std::vector<uint8_t> psb_write(const PsbFile& psb,
                                std::span<const uint8_t> original_data = {});

} // namespace lu::assets
