#pragma once
// pki_types.h — Pack Index (primary.pki) data structures.
//
// The PKI file maps CRC hashes to the .pk pack file that contains them.
// Used by the LEGO Universe client to locate files across multiple packs.
//
// Binary format (version 3):
//   u32 version
//   u32 pack_count
//   For each pack: u32 string_len + string (backslash-separated path)
//   u32 entry_count
//   For each entry (20 bytes): u32 crc, s32 lower_crc, s32 upper_crc, u32 pack_index, u32 unknown

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace lu::assets {

struct PkiEntry {
    uint32_t crc;
    int32_t lower_crc;
    int32_t upper_crc;
    uint32_t pack_index;
    uint32_t unknown;
};

struct PkiFile {
    uint32_t version = 0;
    std::vector<std::string> pack_paths;   // index → pack file path (forward slashes)
    std::vector<PkiEntry> entries;
    std::unordered_map<uint32_t, uint32_t> crc_to_pack;  // CRC → pack index
};

} // namespace lu::assets
