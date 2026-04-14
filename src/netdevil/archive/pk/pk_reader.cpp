#include "netdevil/archive/pk/pk_reader.h"
#include "netdevil/archive/sd0/sd0_reader.h"
#include <string>

#include <cstring>
#include <algorithm>

namespace lu::assets {

PkArchive::PkArchive(std::span<const uint8_t> data)
    : data_(data)
{
    if (data_.size() < PK_HEADER_SIZE + 8) {
        throw PkError("PK: file too small");
    }

    // Validate magic header
    if (std::memcmp(data_.data(), PK_MAGIC, PK_HEADER_SIZE) != 0) {
        throw PkError("PK: invalid magic header");
    }

    // Read TOC offset and file revision from end of file
    // Layout at EOF: [u32 toc_offset] [u32 file_revision]
    uint32_t toc_offset = 0;
    std::memcpy(&toc_offset, data_.data() + data_.size() - 8, 4);

    // Seek to TOC and read entry count
    if (toc_offset + 4 > data_.size()) {
        throw PkError("PK: TOC offset out of bounds");
    }

    uint32_t num_entries = 0;
    std::memcpy(&num_entries, data_.data() + toc_offset, 4);

    // Validate entry data fits
    size_t entries_start = toc_offset + 4;
    size_t entries_size = static_cast<size_t>(num_entries) * PK_ENTRY_SIZE;
    if (entries_start + entries_size > data_.size()) {
        throw PkError("PK: entry table exceeds file size");
    }

    // Read all entries
    entries_.resize(num_entries);
    for (uint32_t i = 0; i < num_entries; ++i) {
        std::memcpy(&entries_[i], data_.data() + entries_start + i * PK_ENTRY_SIZE, PK_ENTRY_SIZE);
    }
}

const PackIndexEntry& PkArchive::entry(size_t index) const {
    if (index >= entries_.size()) {
        throw PkError("PK: entry index out of range");
    }
    return entries_[index];
}

std::vector<uint8_t> PkArchive::extract(size_t index) const {
    return extract(entry(index));
}

std::vector<uint8_t> PkArchive::extract(const PackIndexEntry& entry) const {
    // Compression check: only the low byte of is_compressed matters.
    // Verified from DarkflameServer Pack.cpp: isCompressed = (m_IsCompressed & 0xff) > 0
    // When compressed, data is in SD0 format (5-byte header + zlib chunks).
    bool is_compressed = (entry.is_compressed & 0xFF) > 0;

    uint32_t raw_size = is_compressed ? entry.compressed_size : entry.uncompressed_size;

    if (raw_size == 0) {
        return {};
    }

    if (entry.data_offset + raw_size > data_.size()) {
        throw PkError("PK: data region exceeds file bounds for entry at offset " +
                       std::to_string(entry.data_offset));
    }

    std::span<const uint8_t> raw_data(
        data_.data() + entry.data_offset, raw_size
    );

    if (!is_compressed) {
        // Uncompressed: read uncompressed_size bytes directly
        return std::vector<uint8_t>(raw_data.begin(), raw_data.end());
    }

    // Compressed data is in SD0 format (5-byte header "sd0\x01\xff" + zlib chunks)
    // Verified from DarkflameServer Pack.cpp: skips 5 bytes then reads [u32 size][zlib] chunks
    return sd0_decompress(raw_data);
}

const PackIndexEntry* PkArchive::find_by_crc(uint32_t crc) const {
    for (const auto& e : entries_) {
        if (e.crc == crc) {
            return &e;
        }
    }
    return nullptr;
}

void PkArchive::for_each(std::function<void(size_t, const PackIndexEntry&)> callback) const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        callback(i, entries_[i]);
    }
}

} // namespace lu::assets
