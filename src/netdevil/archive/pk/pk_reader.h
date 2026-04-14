#pragma once
#include "netdevil/archive/pk/pk_types.h"

#include <span>
#include <string>
#include <vector>
#include <functional>

namespace lu::assets {

// Represents an open PK archive for random-access reading.
class PkArchive {
public:
    // Open a PK archive from a memory-mapped or loaded file buffer.
    // The span must remain valid for the lifetime of the PkArchive.
    explicit PkArchive(std::span<const uint8_t> data);

    // Number of entries in the archive.
    size_t entry_count() const { return entries_.size(); }

    // Get an entry by index.
    const PackIndexEntry& entry(size_t index) const;

    // Extract raw file data for an entry (decompresses SD0 if needed).
    std::vector<uint8_t> extract(size_t index) const;

    // Extract raw file data for an entry (decompresses SD0 if needed).
    std::vector<uint8_t> extract(const PackIndexEntry& entry) const;

    // Find entry by CRC. Returns nullptr if not found.
    const PackIndexEntry* find_by_crc(uint32_t crc) const;

    // Iterate all entries with a callback.
    void for_each(std::function<void(size_t index, const PackIndexEntry& entry)> callback) const;

private:
    std::span<const uint8_t> data_;
    std::vector<PackIndexEntry> entries_;
};

} // namespace lu::assets
