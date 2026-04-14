#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// SD0 (Segmented Data 0) decompression.
//
// Format: 5-byte header ("sd0\x01\xff") followed by repeating chunks of
// [u32 compressed_size][zlib_compressed_data]. Each chunk decompresses
// independently to ~256KB.

struct Sd0Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Header magic: "sd0" + 0x01 + 0xFF
inline constexpr uint8_t SD0_MAGIC[] = {0x73, 0x64, 0x30, 0x01, 0xFF};
inline constexpr size_t SD0_HEADER_SIZE = 5;

// Typical uncompressed chunk size (1024 * 256 bytes)
inline constexpr size_t SD0_CHUNK_SIZE = 262144;
} // namespace lu::assets
