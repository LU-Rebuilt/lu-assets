#include "gamebryo/kfm/kfm_reader.h"
#include "common/binary_reader/binary_reader.h"
#include <string>

#include <algorithm>

namespace lu::assets {

KfmFile kfm_parse(std::span<const uint8_t> data) {
    // Find the newline after the text header
    // Header format: ";Gamebryo KFM File Version X.X.X.Xb\n"
    size_t nl_pos = 0;
    for (size_t i = 0; i < std::min(data.size(), size_t(256)); ++i) {
        if (data[i] == 0x0A) { nl_pos = i + 1; break; }
    }
    if (nl_pos == 0) throw KfmError("KFM: could not find header newline");

    BinaryReader r(data);
    r.seek(nl_pos);

    // has_text_keys: u8 flag (always 1 in LU client, indicates text key extra data present)
    KfmFile kfm;
    kfm.has_text_keys = r.read_u8();

    // Read NIF path: u32 length + ASCII characters
    uint32_t path_len = r.read_u32();
    if (path_len > 1024) throw KfmError("KFM: path length too large: " + std::to_string(path_len));
    auto path_bytes = r.read_bytes(path_len);
    kfm.nif_path = std::string(reinterpret_cast<const char*>(path_bytes.data()), path_len);

    // Normalize path separators
    std::replace(kfm.nif_path.begin(), kfm.nif_path.end(), '\\', '/');

    return kfm;
}

} // namespace lu::assets
