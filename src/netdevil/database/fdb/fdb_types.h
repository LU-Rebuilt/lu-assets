#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// References:
//   - lcdr/utils fdb_to_sqlite (github.com/lcdr/utils) — FDB format documentation and SQLite converter
//   - DarkflameServer (github.com/DarkflameUniverse/DarkflameServer) — CDClient table usage reference

// FDB (Flat DataBase) parser for LEGO Universe's cdclient.fdb.
//
// Binary format verified against legouniverse.exe:
//   - CreateDataCache (0x00fae126): loads "cdclient.fdb"
//   - LoadFDBFile (0x00faa180): opens and memory-maps the file
//   - FUN_00776530: iterates tables (8-byte stride per table pointer)
//
// Format structure:
//   [u32 table_count]
//   [table_count * table_header_ptr]: each is a u32 pointer to table header
//     table_header:
//       [u32 column_count]
//       [u32 table_name_ptr] -> null-terminated string
//       [column_count * column_def]:
//         [u32 data_type]  (see FdbDataType)
//         [u32 column_name_ptr] -> null-terminated string
//       [u32 row_bucket_count] (always power of 2)
//       [row_bucket_count * u32 row_ptr]: -1 = empty, else pointer to row_info
//         row_info:
//           [u32 column_count]
//           [column_count * field_data]:
//             [u32 data_type]
//             [type-dependent value: 4 or 8 bytes]
//           [u32 next_row_ptr]: -1 = end, else pointer to next row_info (hash chain)

struct FdbError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class FdbDataType : uint32_t {
    NOTHING  = 0,   // NULL (4 bytes: 0x00000000)
    INT32    = 1,   // 32-bit signed integer
    UNUSED_2 = 2,   // Unused — gap in enum, not in DLU eSqliteDataType or shipped cdclient.fdb
    REAL     = 3,   // 32-bit float
    TEXT_4   = 4,   // String via 4-byte pointer indirection
    BOOL     = 5,   // 32-bit boolean (0 or 1)
    INT64    = 6,   // 64-bit signed integer via pointer indirection
    UNUSED_7 = 7,   // Unused — gap in enum, not in DLU eSqliteDataType or shipped cdclient.fdb
    TEXT_8   = 8,   // String via pointer indirection (same as TEXT_4 in practice)
};

using FdbValue = std::variant<
    std::monostate,   // NOTHING / NULL
    int32_t,          // INT32
    float,            // REAL
    std::string,      // TEXT_4 / TEXT_8
    bool,             // BOOL
    int64_t           // INT64
>;

struct FdbColumn {
    std::string name;
    FdbDataType type;
};

struct FdbRow {
    std::vector<FdbValue> fields;
};

struct FdbTable {
    std::string name;
    std::vector<FdbColumn> columns;
    std::vector<FdbRow> rows;
};
} // namespace lu::assets
