#pragma once
#include "microsoft/fxo/fxo_types.h"

namespace lu::assets {

// Parse an FXO/FXP file. Extracts parameters and technique names.
// Stores full file bytes in raw_data (no gaps — shader bytecode is included).
// The .fxp files (common.fxp, legoppcommon.fxp) are 0-byte pool files; returns
// an empty FxoFile with total_size = 0 for them.
FxoFile fxo_parse(std::span<const uint8_t> data);

} // namespace lu::assets
