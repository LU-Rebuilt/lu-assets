// vfs.cpp — Virtual File System for packed and unpacked client resources.

#include "netdevil/archive/vfs/vfs.h"
#include "netdevil/archive/pk/pk_reader.h"
#include "netdevil/archive/pki/pki_reader.h"
#include <algorithm>
#include <fstream>
#include <iostream>

namespace lu::assets {

namespace fs = std::filesystem;

Vfs::~Vfs() = default;

static std::vector<uint8_t> read_file_raw(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    auto size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

std::string Vfs::normalize_path(const std::string& path) {
    std::string result = path;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return c == '\\' ? '/' : static_cast<char>(std::tolower(c)); });
    return result;
}

uint32_t Vfs::path_crc(const std::string& normalized_path) {
    // Standard CRC32 (polynomial 0xEDB88320) matching the LU client's pack file CRCs
    uint32_t crc = 0xFFFFFFFF;
    for (unsigned char c : normalized_path) {
        crc ^= c;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc ^ 0xFFFFFFFF;
}

bool Vfs::init(const fs::path& client_root) {
    root_ = client_root;

    fs::path pack_dir = root_ / "res" / "pack";
    if (fs::exists(pack_dir) && fs::is_directory(pack_dir)) {
        mode_ = VfsMode::Packed;
        if (!init_packed(pack_dir)) {
            return false;
        }
        return true;
    }

    if (fs::exists(root_ / "res")) {
        mode_ = VfsMode::Unpacked;
        return true;
    }

    return false;
}

bool Vfs::init_packed(const fs::path& pack_dir) {
    // Load all .pk files from the pack directory
    std::vector<fs::path> pk_files;
    for (const auto& entry : fs::directory_iterator(pack_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".pk") {
            pk_files.push_back(entry.path());
        }
    }
    std::sort(pk_files.begin(), pk_files.end());

    for (const auto& pk_path : pk_files) {
        auto file_data = read_file_raw(pk_path);
        if (file_data.empty()) continue;

        size_t archive_idx = archives_.size();
        auto& handle = archives_.emplace_back();
        handle.file_data = std::move(file_data);

        try {
            handle.archive = std::make_unique<PkArchive>(handle.file_data);

            handle.archive->for_each([&](size_t entry_idx, const PackIndexEntry& entry) {
                crc_index_[entry.crc] = PkLookup{archive_idx, entry_idx};
            });
        } catch (const std::exception&) {
            archives_.pop_back();
        }
    }

    return !archives_.empty();
}

// Game paths are case-insensitive (the original client ran on case-insensitive NTFS, and
// normalize_path lowercases for PK CRCs anyway), but an unpacked tree on a case-sensitive
// filesystem preserves whatever casing the assets shipped with (e.g. res/maps/01_Live_Maps).
// Walk the path one component at a time: take an exact match when present, otherwise scan the
// directory for a case-insensitive one.
fs::path Vfs::resolve_unpacked(const std::string& normalized) const {
    fs::path current = root_;
    for (const fs::path& component : fs::path(normalized)) {
        fs::path exact = current / component;
        if (fs::exists(exact)) {
            current = std::move(exact);
            continue;
        }

        std::error_code ec;
        fs::path found;
        for (const auto& entry : fs::directory_iterator(current, ec)) {
            if (normalize_path(entry.path().filename().string()) == component.string()) {
                found = entry.path();
                break;
            }
        }
        if (found.empty()) return {};
        current = std::move(found);
    }
    return current;
}

std::optional<std::vector<uint8_t>> Vfs::read(const std::string& path) const {
    std::string normalized = normalize_path(path);

    if (mode_ == VfsMode::Unpacked) {
        fs::path full = resolve_unpacked(normalized);
        if (full.empty()) return std::nullopt;
        auto data = read_file_raw(full);
        if (data.empty()) return std::nullopt;
        return data;
    }

    // Packed mode — look up by CRC
    uint32_t crc = path_crc(normalized);
    auto it = crc_index_.find(crc);
    if (it == crc_index_.end()) return std::nullopt;

    const auto& lookup = it->second;
    try {
        return archives_[lookup.archive_idx].archive->extract(lookup.entry_idx);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool Vfs::exists(const std::string& path) const {
    std::string normalized = normalize_path(path);

    if (mode_ == VfsMode::Unpacked) {
        return !resolve_unpacked(normalized).empty();
    }

    uint32_t crc = path_crc(normalized);
    return crc_index_.contains(crc);
}

} // namespace lu::assets
