#pragma once
#include "netdevil/database/fdb/fdb_types.h"

namespace lu::assets {

// Parse an FDB file and return table schemas (column names and types).
// Does NOT load row data into memory — use fdb_parse_full or fdb_to_sqlite_direct for that.
std::vector<FdbTable> fdb_parse(std::span<const uint8_t> data);

// Parse an FDB file and fully hydrate every table's row data (FdbTable::rows), walking the same
// table -> column -> row-bucket -> hash-chain structure the client's own reader does
// (LWODataCache::LoadFdbFile, see lwo_data_cache.h). This is the native in-memory
// representation LWODataCache indexes; unlike fdb_to_sqlite_direct it never touches disk/SQLite.
std::vector<FdbTable> fdb_parse_full(std::span<const uint8_t> data);

// Stream-convert FDB binary data directly to SQLite in a single pass.
// Memory efficient: reads field values from the binary and binds them directly
// to SQLite prepared statements without intermediate storage.
void fdb_to_sqlite_direct(std::span<const uint8_t> data, const std::string& output_path);

} // namespace lu::assets
