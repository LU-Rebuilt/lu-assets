#include "netdevil/archive/sd0/sd0_writer.h"

#include <zlib.h>
#include <cstring>

namespace lu::assets {

// Real client-shipped files use zlib level 9 (max compression) — verified byte-identical
// against 83 real chunks across the sandbox corpus at this level, so sd0_compress()
// reproduces the exact original bytes whenever the input is the decompressed content of
// a real SD0 file (not just semantically lossless via decompress-recompress).
std::vector<uint8_t> sd0_compress(std::span<const uint8_t> data) {
    std::vector<uint8_t> out(SD0_MAGIC, SD0_MAGIC + SD0_HEADER_SIZE);
    if (data.empty()) return out;

    size_t pos = 0;
    do {
        size_t chunk_len = std::min(SD0_CHUNK_SIZE, data.size() - pos);
        uLongf bound = compressBound(static_cast<uLong>(chunk_len));
        std::vector<uint8_t> compressed(bound);

        int ret = compress2(compressed.data(), &bound,
                             data.data() + pos, static_cast<uLong>(chunk_len),
                             Z_BEST_COMPRESSION);
        if (ret != Z_OK) {
            throw Sd0Error("SD0: zlib compression failed (error " + std::to_string(ret) + ")");
        }

        uint32_t chunk_size = static_cast<uint32_t>(bound);
        size_t header_off = out.size();
        out.resize(header_off + 4);
        std::memcpy(out.data() + header_off, &chunk_size, 4);
        out.insert(out.end(), compressed.begin(), compressed.begin() + bound);

        pos += chunk_len;
    } while (pos < data.size());

    return out;
}

} // namespace lu::assets
