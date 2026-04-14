#include "netdevil/archive/sd0/sd0_reader.h"

#include <zlib.h>
#include <cstring>
#include <string>

namespace lu::assets {

bool sd0_is_compressed(std::span<const uint8_t> data) {
    if (data.size() < SD0_HEADER_SIZE) return false;
    return std::memcmp(data.data(), SD0_MAGIC, SD0_HEADER_SIZE) == 0;
}

std::vector<uint8_t> sd0_decompress(std::span<const uint8_t> data) {
    if (data.size() < SD0_HEADER_SIZE) {
        throw Sd0Error("SD0: data too small for header");
    }
    if (!sd0_is_compressed(data)) {
        throw Sd0Error("SD0: invalid magic header");
    }

    std::vector<uint8_t> output;
    // Reserve a reasonable estimate - most files decompress to a few multiples of chunk size
    output.reserve(data.size() * 2);

    size_t pos = SD0_HEADER_SIZE;

    while (pos < data.size()) {
        // Read chunk compressed size (u32 little-endian)
        if (pos + 4 > data.size()) {
            throw Sd0Error("SD0: truncated chunk size at offset " + std::to_string(pos));
        }

        uint32_t chunk_size = 0;
        std::memcpy(&chunk_size, data.data() + pos, 4);
        pos += 4;

        if (chunk_size == 0) {
            throw Sd0Error("SD0: zero-length chunk at offset " + std::to_string(pos - 4));
        }
        if (pos + chunk_size > data.size()) {
            throw Sd0Error("SD0: chunk size " + std::to_string(chunk_size) +
                           " exceeds remaining data at offset " + std::to_string(pos));
        }

        // Decompress this chunk with zlib
        // Start with typical chunk size, grow if needed
        uLongf dest_len = SD0_CHUNK_SIZE;
        size_t output_offset = output.size();
        output.resize(output_offset + dest_len);

        int ret = uncompress(
            output.data() + output_offset, &dest_len,
            data.data() + pos, chunk_size
        );

        if (ret == Z_BUF_ERROR) {
            // Buffer too small - retry with larger buffer
            dest_len = SD0_CHUNK_SIZE * 4;
            output.resize(output_offset + dest_len);
            ret = uncompress(
                output.data() + output_offset, &dest_len,
                data.data() + pos, chunk_size
            );
        }

        if (ret != Z_OK) {
            throw Sd0Error("SD0: zlib decompression failed (error " + std::to_string(ret) +
                           ") at offset " + std::to_string(pos));
        }

        // Trim to actual decompressed size
        output.resize(output_offset + dest_len);
        pos += chunk_size;
    }

    return output;
}

} // namespace lu::assets
