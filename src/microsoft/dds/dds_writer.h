#pragma once
#include "microsoft/dds/dds_types.h"

#include <span>
#include <vector>

namespace lu::assets {

// Serialize the 128-byte DDS preamble (magic + header) followed by `payload` — every byte
// of the original file after offset 128 (surface data, and the DX10 extended header first
// when present). dds_write(dds_parse_header(d), span(d).subspan(128)) is byte-identical
// to d: the header struct stores every field verbatim, including the reserved words.
std::vector<uint8_t> dds_write(const DdsFile& dds, std::span<const uint8_t> payload);

} // namespace lu::assets
