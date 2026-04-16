#pragma once
// vfs.h — Virtual File System for LU client resources.
//
// Supports both packed (.pk archive) and unpacked (directory tree) modes.
// Auto-detects based on the presence of res/pack/ vs res/maps/.
// All file paths are normalized to lowercase forward-slash format
// matching the CRC convention used in PK archives.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lu::assets {

class PkArchive;

enum class VfsMode {
    Unpacked,   // Read from directory tree
    Packed      // Read from .pk archives
};

class Vfs {
public:
    Vfs() = default;
    ~Vfs();
    Vfs(const Vfs&) = delete;
    Vfs& operator=(const Vfs&) = delete;

    // Initialize the VFS with the client root directory.
    // Auto-detects packed vs unpacked mode.
    bool init(const std::filesystem::path& client_root);

    VfsMode mode() const { return mode_; }
    const std::filesystem::path& root() const { return root_; }

    // Read a file by its game-relative path (e.g., "res/maps/avantgardens/avantgardens.luz").
    // Returns empty optional if not found.
    std::optional<std::vector<uint8_t>> read(const std::string& path) const;

    // Check if a file exists in the VFS.
    bool exists(const std::string& path) const;

    // Number of loaded PK archives (packed mode only)
    size_t archive_count() const { return archives_.size(); }

    // Normalize a path to the format used in PK CRC lookups
    static std::string normalize_path(const std::string& path);

    // Compute CRC32 for a normalized path (same algorithm used by PK archives)
    static uint32_t path_crc(const std::string& normalized_path);

private:
    bool init_packed(const std::filesystem::path& pack_dir);

    struct PkHandle {
        std::vector<uint8_t> file_data;
        std::unique_ptr<PkArchive> archive;
    };

    VfsMode mode_ = VfsMode::Unpacked;
    std::filesystem::path root_;
    std::vector<PkHandle> archives_;

    struct PkLookup {
        size_t archive_idx;
        size_t entry_idx;
    };
    std::unordered_map<uint32_t, PkLookup> crc_index_;
};

} // namespace lu::assets
