#include "netdevil/database/fdb/lwo_data_cache.h"
#include "netdevil/database/fdb/fdb_reader.h"

#include <fstream>

namespace lu::assets {

bool LWODataCache::LoadFdbFile(const std::filesystem::path& fdb_path) {
    std::ifstream file(fdb_path, std::ios::binary | std::ios::ate);
    if (!file) {
        loaded_ = false;
        return false;
    }

    auto size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    return LoadFromBytes(data);
}

bool LWODataCache::LoadFromBytes(std::span<const uint8_t> data) {
    try {
        tables_ = fdb_parse_full(data);
    } catch (const FdbError&) {
        loaded_ = false;
        return false;
    }

    BuildTableNameIndex();
    loaded_ = true;
    return true;
}

void LWODataCache::BuildTableNameIndex() {
    pTableNameToIndex.clear();
    rowIdIndex_.clear();

    for (size_t i = 0; i < tables_.size(); ++i) {
        const FdbTable& table = tables_[i];
        pTableNameToIndex[table.name] = static_cast<int32_t>(i);

        auto& idIndex = rowIdIndex_[table.name];
        for (size_t rowIdx = 0; rowIdx < table.rows.size(); ++rowIdx) {
            const FdbRow& row = table.rows[rowIdx];
            if (row.fields.empty()) continue;
            if (auto* id = std::get_if<int32_t>(&row.fields[0])) {
                idIndex.emplace(*id, rowIdx);
            }
        }
    }
}

int32_t LWODataCache::FindTableIndexByName(const std::string& table_name) const {
    auto it = pTableNameToIndex.find(table_name);
    return it != pTableNameToIndex.end() ? it->second : -1;
}

const FdbTable* LWODataCache::GetTableByIndex(int32_t index) const {
    if (index < 0 || static_cast<size_t>(index) >= tables_.size()) return nullptr;
    return &tables_[static_cast<size_t>(index)];
}

const FdbTable* LWODataCache::GetTable(const std::string& table_name) const {
    int32_t index = FindTableIndexByName(table_name);
    return index >= 0 ? GetTableByIndex(index) : nullptr;
}

const FdbRow* LWODataCache::QueryTableRowById(const std::string& table_name, int32_t id) const {
    auto tableIt = rowIdIndex_.find(table_name);
    if (tableIt == rowIdIndex_.end()) return nullptr;

    auto rowIt = tableIt->second.find(id);
    if (rowIt == tableIt->second.end()) return nullptr;

    const FdbTable* table = GetTable(table_name);
    return table ? &table->rows[rowIt->second] : nullptr;
}

std::vector<const FdbRow*> LWODataCache::QueryTableRowsById(const std::string& table_name,
                                                              int32_t id) const {
    std::vector<const FdbRow*> results;

    auto tableIt = rowIdIndex_.find(table_name);
    if (tableIt == rowIdIndex_.end()) return results;

    const FdbTable* table = GetTable(table_name);
    if (!table) return results;

    auto range = tableIt->second.equal_range(id);
    for (auto it = range.first; it != range.second; ++it) {
        results.push_back(&table->rows[it->second]);
    }
    return results;
}

} // namespace lu::assets
