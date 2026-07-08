#pragma once
#include "gamebryo/nif/nif_types.h"

namespace lu::assets {

// Serialize a NifFile back to the Gamebryo NIF container format (.nif, .kf, .etk — all
// share this container). Byte-identical to the source file for any NifFile produced by
// nif_parse whose block_data was left untouched: the header fields are preserved verbatim
// (header_line, endian, string_table_max_len, groups, roots) and blocks are emitted from
// their raw bytes, so block types without a dedicated parser round-trip losslessly.
//
// Block sizes are recomputed from block_data (not copied from block_sizes), so editing a
// block's bytes stays consistent automatically.
//
// Requires the per-block size table to have been present at parse time (NIF >= 20.2.0.7,
// which covers every version LU shipped) — without it nif_parse cannot slice raw blocks,
// and this throws NifError.
std::vector<uint8_t> nif_write(const NifFile& nif);

} // namespace lu::assets
