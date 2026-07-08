#pragma once
#include "netdevil/database/fdb/fdb_types.h"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace lu::assets {

// LWODataCache — the client-side CDClient FDB reader, reconstructed to match the original
// module (lwodb\source\defaultsfdb.cpp) and class documented in
// subsystems/resource/CDClientFdb.md: LoadFDBFile -> LWODataCache::LoadFdbFile (0x00776350),
// BuildTableNameIndex (0x00776530), FindTableIndexByName (0x00776610, was
// FdbCacheGetTable), GetTableByIndex (0x00776690).
//
// The original memory-maps the file and relocates in-place file offsets to absolute pointers
// (FdbHeader::RelocateOffsets, 0x00773970) so every lookup walks the mapped bytes directly.
// This reconstruction reads the file into owned FdbTable/FdbRow structures once at load time
// instead of mapping+relocating in place — same externally-observable behavior (table name ->
// rows by id), without relying on pointer-punning a mapped file, which has no portable
// equivalent across platforms. No SQLite conversion anywhere in this path, matching the
// original: the client never touches SQLite, it reads the FDB binary format natively.
class LWODataCache {
public:
    // LoadFdbFile (0x00776350) — opens+parses an FDB file directly off disk. Returns false on
    // failure (the original logs "Failed to open/read fdb %s, exiting game" and fatally exits;
    // callers here get a bool instead so client startup can decide how to handle a missing
    // database). Prefer LoadFromBytes when the caller sources bytes through a VFS (packed .pk
    // archives or unpacked directory tree) rather than a bare filesystem path.
    bool LoadFdbFile(const std::filesystem::path& fdb_path);

    // Parses already-read FDB bytes (e.g. from Vfs::read, which transparently handles both
    // packed and unpacked client layouts).
    bool LoadFromBytes(std::span<const uint8_t> data);

    // FindTableIndexByName (0x00776610, was FdbCacheGetTable) — tableName -> table index, or -1
    // on miss (original logs "Cannot Find Table with name %S").
    int32_t FindTableIndexByName(const std::string& table_name) const;

    // GetTableByIndex (0x00776690) — index -> table, or nullptr if out of range.
    const FdbTable* GetTableByIndex(int32_t index) const;

    const FdbTable* GetTable(const std::string& table_name) const;

    // FdbCacheQueryTableById (0x00f5a5c0) / GetDataCacheObjects (0x00f58012) — table+id -> row.
    // CDClient tables key their first column as an id; returns nullptr on miss.
    const FdbRow* QueryTableRowById(const std::string& table_name, int32_t id) const;

    // Some CDClient tables (ComponentsRegistry, ObjectSkills, ...) are one-to-many keyed on
    // their first column; this returns every matching row rather than just the first.
    std::vector<const FdbRow*> QueryTableRowsById(const std::string& table_name, int32_t id) const;

    bool IsLoaded() const { return loaded_; }

private:
    std::vector<FdbTable> tables_;                                    // pHeader->directory, hydrated
    std::unordered_map<std::string, int32_t> pTableNameToIndex;       // +0x04
    std::unordered_map<std::string, std::unordered_multimap<int32_t, size_t>> rowIdIndex_;
    bool loaded_ = false;

    void BuildTableNameIndex(); // 0x00776530
};

} // namespace lu::assets
