#include <gtest/gtest.h>
#include "netdevil/database/fdb/fdb_reader.h"

#include <cstring>
#include <vector>

using namespace lu::assets;

namespace {

// Helper to append little-endian values to a buffer
struct FdbBuilder {
    std::vector<uint8_t> data;
    void i32(int32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void u8(uint8_t v) { data.push_back(v); }
    size_t pos() const { return data.size(); }
    // Patch a previously written i32 at offset
    void patch_i32(size_t offset, int32_t v) {
        std::memcpy(data.data() + offset, &v, 4);
    }
};

// Build a minimal FDB with one table, one column, zero rows.
// Layout:
//   [0]  i32 table_count = 1
//   [4]  i32 ptr -> table_ptr_array (offset 8)
//   [8]  i32 ptr -> table_header (offset 12)
//  [12]  i32 num_cols = 1           (table header)
//  [16]  i32 ptr -> table_name (offset 40)
//  [20]  i32 ptr -> column_defs (offset 28)
//  [24]  i32 ptr -> row_header (offset 48)
//  --- column_defs ---
//  [28]  i32 data_type = 1 (INT32)
//  [32]  i32 ptr -> col_name (offset 44)
//  --- padding/strings ---
//  [36]  i32 unused
//  [40]  "Test\0"   (table name)
//  [45]  "id\0"     (column name, at offset 44 below)
//  --- row header ---
//  [48]  i32 num_allocated_rows = 0
//  [52]  i32 ptr -> row_ptr_array (offset 56)
//  [56]  (empty)
std::vector<uint8_t> build_minimal_fdb() {
    FdbBuilder b;

    // [0] table_count
    b.i32(1);
    // [4] pointer to table_ptr_array
    b.i32(8);
    // [8] table_ptr_array[0]: pointer to table header
    b.i32(12);
    // [12] table header: num_cols
    b.i32(1);
    // [16] pointer to table name string
    b.i32(40);
    // [20] pointer to column_defs array
    b.i32(28);
    // [24] pointer to row_header
    b.i32(48);

    // [28] column_defs[0].data_type = INT32
    b.i32(static_cast<int32_t>(FdbDataType::INT32));
    // [32] column_defs[0].name pointer
    b.i32(36);

    // [36] column name: "id\0"
    b.u8('i'); b.u8('d'); b.u8(0); b.u8(0); // pad to 4 bytes

    // [40] table name: "Test\0"
    b.u8('T'); b.u8('e'); b.u8('s'); b.u8('t'); b.u8(0);
    b.u8(0); b.u8(0); b.u8(0); // pad to offset 48

    // [48] row_header: num_allocated_rows
    b.i32(0);
    // [52] pointer to row_ptr_array (empty, point past end)
    b.i32(56);

    return b.data;
}

} // anonymous namespace

TEST(FDB, DataTypeEnumValues) {
    EXPECT_EQ(static_cast<uint32_t>(FdbDataType::NOTHING), 0u);
    EXPECT_EQ(static_cast<uint32_t>(FdbDataType::INT32),   1u);
    EXPECT_EQ(static_cast<uint32_t>(FdbDataType::UNUSED_2), 2u);
    EXPECT_EQ(static_cast<uint32_t>(FdbDataType::REAL),    3u);
    EXPECT_EQ(static_cast<uint32_t>(FdbDataType::TEXT_4),  4u);
    EXPECT_EQ(static_cast<uint32_t>(FdbDataType::BOOL),    5u);
    EXPECT_EQ(static_cast<uint32_t>(FdbDataType::INT64),   6u);
    EXPECT_EQ(static_cast<uint32_t>(FdbDataType::TEXT_8),  8u);
}

TEST(FDB, ParseMinimalFdb) {
    auto data = build_minimal_fdb();
    auto tables = fdb_parse(data);

    ASSERT_EQ(tables.size(), 1u);
    EXPECT_EQ(tables[0].name, "Test");
    ASSERT_EQ(tables[0].columns.size(), 1u);
    EXPECT_EQ(tables[0].columns[0].name, "id");
    EXPECT_EQ(tables[0].columns[0].type, FdbDataType::INT32);
    // fdb_parse does not load rows, so rows should be empty
    EXPECT_TRUE(tables[0].rows.empty());
}

TEST(FDB, ParseEmptyThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(fdb_parse(empty), FdbError);
}

TEST(FDB, ParseTooSmallThrows) {
    // Only 2 bytes -- not enough for initial i32 table_count
    std::vector<uint8_t> tiny = {0x01, 0x00};
    EXPECT_THROW(fdb_parse(tiny), FdbError);
}

TEST(FDB, UnreasonableTableCountThrows) {
    FdbBuilder b;
    b.i32(99999); // table_count way too high
    b.i32(8);     // pointer (will be invalid)
    EXPECT_THROW(fdb_parse(b.data), FdbError);
}
