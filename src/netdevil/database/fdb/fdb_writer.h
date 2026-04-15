#pragma once
#include <string>

namespace lu::assets {

// Convert a SQLite database to FDB binary format.
// Reads all tables from the SQLite file, builds the FDB pointer-based
// binary layout with hash-bucketed rows, and writes to output_path.
void sqlite_to_fdb(const std::string& sqlite_path, const std::string& fdb_path);

} // namespace lu::assets
