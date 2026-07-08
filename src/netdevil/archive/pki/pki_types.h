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
//   For each entry (20 bytes): u32 crc, s32 lower_crc, s32 upper_crc, u32 pack_index,
//                              u32 is_compressed (see PkiEntry)

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
    // Whether the entry is SD0-compressed inside its pack, stored as a bool32 whose upper
    // three bytes are uninitialized garbage from NetDevil's packer — only the low byte is
    // meaningful (verified 0/1 across every shipped primary.pki; matches lcdr's PKI spec).
    // Preserved verbatim so pki_write round-trips byte-identically; use is_compressed().
    uint32_t is_compressed_raw;

    bool is_compressed() const { return (is_compressed_raw & 0xFF) != 0; }
};

struct PkiFile {
    uint32_t version = 0;
    // Index → pack file path, verbatim as stored (backslash separators) so pki_write
    // round-trips byte-identically. Use pack_path_normalized() for POSIX lookups.
    std::vector<std::string> pack_paths;
    std::vector<PkiEntry> entries;
    std::unordered_map<uint32_t, uint32_t> crc_to_pack;  // CRC → pack index

    std::string pack_path_normalized(size_t index) const {
        std::string p = pack_paths.at(index);
        for (char& c : p) {
            if (c == '\\') c = '/';
        }
        return p;
    }
};

} // namespace lu::assets
