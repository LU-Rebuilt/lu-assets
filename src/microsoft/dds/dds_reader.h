#pragma once
#include "microsoft/dds/dds_types.h"

namespace lu::assets {

// Parse DDS header. Does NOT decompress pixel data — returns metadata
// and the offset where pixel data starts for the renderer to consume.
DdsFile dds_parse_header(std::span<const uint8_t> data);

} // namespace lu::assets
