#pragma once
#include "gamebryo/kfm/kfm_types.h"

#include <vector>

namespace lu::assets {

// Serialize a KfmFile back to the Gamebryo KFM binary format. Byte-identical to the
// source file for any KfmFile produced by kfm_parse: the header line, path strings
// (including their original backslash separators), and every field are preserved verbatim.
std::vector<uint8_t> kfm_write(const KfmFile& kfm);

} // namespace lu::assets
