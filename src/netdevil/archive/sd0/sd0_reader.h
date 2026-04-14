#pragma once
#include "netdevil/archive/sd0/sd0_types.h"

namespace lu::assets {

// Check if data starts with the SD0 magic header.
bool sd0_is_compressed(std::span<const uint8_t> data);

// Decompress SD0 data. Throws Sd0Error on invalid data or decompression failure.
std::vector<uint8_t> sd0_decompress(std::span<const uint8_t> data);

} // namespace lu::assets
