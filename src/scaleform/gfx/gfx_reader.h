#pragma once
#include "scaleform/gfx/gfx_types.h"

namespace lu::assets {

// Parse a GFx/SWF file.  Decompresses CFX/CWS, parses the RECT, frame info,
// and all tag records.  All tag payloads stored as raw bytes.
GfxFile gfx_parse(std::span<const uint8_t> data);

} // namespace lu::assets
