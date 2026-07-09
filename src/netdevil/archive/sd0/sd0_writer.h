#pragma once
#include "netdevil/archive/sd0/sd0_types.h"

namespace lu::assets {

// Compress raw data into SD0 format: header + repeating [u32 compressed_size][zlib data]
// chunks, each covering up to SD0_CHUNK_SIZE (256KB) bytes of the input — the same
// chunking real client-shipped files use (verified against real multi-chunk samples:
// every chunk decompresses to exactly SD0_CHUNK_SIZE bytes except the last, which holds
// the remainder). Not byte-identical to any specific original SD0 file (zlib's chosen
// compression level/output isn't something we can pin down from decompressed content
// alone), but sd0_decompress(sd0_compress(data)) == data always holds, and the chunk
// boundaries match the real chunking scheme.
std::vector<uint8_t> sd0_compress(std::span<const uint8_t> data);

} // namespace lu::assets
