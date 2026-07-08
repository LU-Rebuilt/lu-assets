#include "netdevil/archive/pki/pki_writer.h"
#include "common/binary_writer/binary_writer.h"

namespace lu::assets {

std::vector<uint8_t> pki_write(const PkiFile& pki) {
    BinaryWriter w;

    w.write_u32(pki.version);

    w.write_u32(static_cast<uint32_t>(pki.pack_paths.size()));
    for (const std::string& path : pki.pack_paths) {
        w.write_string32(path);
    }

    w.write_u32(static_cast<uint32_t>(pki.entries.size()));
    for (const PkiEntry& e : pki.entries) {
        w.write_u32(e.crc);
        w.write_s32(e.lower_crc);
        w.write_s32(e.upper_crc);
        w.write_u32(e.pack_index);
        w.write_u32(e.is_compressed_raw);
    }

    return std::move(w.data());
}

} // namespace lu::assets
