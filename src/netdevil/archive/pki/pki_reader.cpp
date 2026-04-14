#include "netdevil/archive/pki/pki_reader.h"

#include <algorithm>

namespace lu::assets {

PkiFile pki_parse(std::span<const uint8_t> data) {
    PkiFile pki;
    size_t off = 0;
    auto d = data.data();
    auto size = data.size();

    auto read_u32 = [&]() -> uint32_t {
        if (off + 4 > size) return 0;
        uint32_t v = d[off] | (d[off+1]<<8) | (d[off+2]<<16) | (static_cast<uint32_t>(d[off+3])<<24);
        off += 4;
        return v;
    };

    pki.version = read_u32();
    if (pki.version < 1 || pki.version > 10) return pki;

    uint32_t packCount = read_u32();
    if (packCount > 10000) return pki;

    pki.pack_paths.reserve(packCount);
    for (uint32_t i = 0; i < packCount; ++i) {
        uint32_t slen = read_u32();
        if (off + slen > size) return pki;
        std::string name(reinterpret_cast<const char*>(d + off), slen);
        off += slen;
        std::replace(name.begin(), name.end(), '\\', '/');
        pki.pack_paths.push_back(std::move(name));
    }

    uint32_t entryCount = read_u32();
    if (off + entryCount * 20 > size) return pki;

    pki.entries.reserve(entryCount);
    for (uint32_t i = 0; i < entryCount; ++i) {
        PkiEntry e;
        e.crc = read_u32();
        e.lower_crc = static_cast<int32_t>(read_u32());
        e.upper_crc = static_cast<int32_t>(read_u32());
        e.pack_index = read_u32();
        e.unknown = read_u32();
        if (e.pack_index < pki.pack_paths.size()) {
            pki.crc_to_pack[e.crc] = e.pack_index;
        }
        pki.entries.push_back(e);
    }

    return pki;
}

} // namespace lu::assets
