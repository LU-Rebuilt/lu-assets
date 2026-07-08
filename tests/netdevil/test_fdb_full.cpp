#include <gtest/gtest.h>
#include "netdevil/database/fdb/fdb_reader.h"
#include "netdevil/database/fdb/fdb_writer.h"
#include "netdevil/database/fdb/lwo_data_cache.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sqlite3.h>
#include <vector>

using namespace lu::assets;

namespace {

// Builds a tiny FDB file by round-tripping through a SQLite database and the existing, already
// production-verified sqlite_to_fdb writer — rather than hand-encoding the FDB's multi-level
// pointer-indirection binary layout (table array -> column/row header pair -> row chain entry ->
// row values -> field data, each behind its own SeekPointer hop), which is exactly the kind of
// error-prone byte-offset bookkeeping this test suite's own history warns against (see the
// decal-threshold NIF bug in test_nif_texturing.cpp, found the same way this file's row-encoding
// bugs were: only by tracing the real reader byte-for-byte). Using the writer as the oracle means
// these tests exercise the same read path fdb_parse_full/LWODataCache use against real
// cdclient.fdb, without re-deriving the on-disk format by hand.
class TempFdb {
public:
    explicit TempFdb(const std::string& create_sql, const std::string& insert_sql) {
        auto unique = std::to_string(reinterpret_cast<uintptr_t>(this));
        sqlitePath_ = std::filesystem::temp_directory_path() / ("lu_test_" + unique + ".sqlite");
        fdbPath_ = std::filesystem::temp_directory_path() / ("lu_test_" + unique + ".fdb");

        sqlite3* db = nullptr;
        sqlite3_open(sqlitePath_.string().c_str(), &db);
        sqlite3_exec(db, create_sql.c_str(), nullptr, nullptr, nullptr);
        sqlite3_exec(db, insert_sql.c_str(), nullptr, nullptr, nullptr);
        sqlite3_close(db);

        sqlite_to_fdb(sqlitePath_.string(), fdbPath_.string());
    }

    ~TempFdb() {
        std::error_code ec;
        std::filesystem::remove(sqlitePath_, ec);
        std::filesystem::remove(fdbPath_, ec);
    }

    const std::filesystem::path& path() const { return fdbPath_; }

    std::vector<uint8_t> readBytes() const {
        std::ifstream f(fdbPath_, std::ios::binary | std::ios::ate);
        auto size = f.tellg();
        f.seekg(0);
        std::vector<uint8_t> data(static_cast<size_t>(size));
        f.read(reinterpret_cast<char*>(data.data()), size);
        return data;
    }

private:
    std::filesystem::path sqlitePath_;
    std::filesystem::path fdbPath_;
};

} // namespace

TEST(FdbFull, ParseFullHydratesRows) {
    TempFdb fdb(
        "CREATE TABLE TestTable (id INTEGER, name TEXT);",
        "INSERT INTO TestTable VALUES (1,'Alice'), (2,'Bob'), (3,'Carol');");

    auto data = fdb.readBytes();
    auto tables = fdb_parse_full(data);

    ASSERT_EQ(tables.size(), 1u);
    EXPECT_EQ(tables[0].name, "TestTable");
    ASSERT_EQ(tables[0].rows.size(), 3u);

    std::vector<int32_t> ids;
    std::vector<std::string> names;
    for (auto& row : tables[0].rows) {
        ids.push_back(std::get<int32_t>(row.fields[0]));
        names.push_back(std::get<std::string>(row.fields[1]));
    }
    std::sort(ids.begin(), ids.end());
    EXPECT_EQ(ids, (std::vector<int32_t>{1, 2, 3}));
    EXPECT_NE(std::find(names.begin(), names.end(), "Alice"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Bob"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Carol"), names.end());
}

TEST(FdbFull, ParseFullEmptyTableHasNoRows) {
    TempFdb fdb("CREATE TABLE EmptyTable (id INTEGER);", "");

    auto data = fdb.readBytes();
    auto tables = fdb_parse_full(data);

    ASSERT_EQ(tables.size(), 1u);
    EXPECT_TRUE(tables[0].rows.empty());
}

class LWODataCacheTest : public ::testing::Test {};

TEST_F(LWODataCacheTest, LoadFromBytesIndexesTableAndRowsById) {
    TempFdb fdb(
        "CREATE TABLE TestTable (id INTEGER, name TEXT);",
        "INSERT INTO TestTable VALUES (1,'Alice'), (2,'Bob');");

    LWODataCache cache;
    ASSERT_TRUE(cache.LoadFromBytes(fdb.readBytes()));
    EXPECT_TRUE(cache.IsLoaded());

    const FdbTable* table = cache.GetTable("TestTable");
    ASSERT_NE(table, nullptr);
    EXPECT_EQ(table->rows.size(), 2u);

    const FdbRow* row = cache.QueryTableRowById("TestTable", 2);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(std::get<std::string>(row->fields[1]), "Bob");

    EXPECT_EQ(cache.QueryTableRowById("TestTable", 999), nullptr);
    EXPECT_EQ(cache.QueryTableRowById("NoSuchTable", 1), nullptr);
}

TEST_F(LWODataCacheTest, QueryTableRowsByIdReturnsAllMatches) {
    // Two rows sharing the same id (e.g. ComponentsRegistry-style one-to-many).
    TempFdb fdb(
        "CREATE TABLE TestTable (id INTEGER, name TEXT);",
        "INSERT INTO TestTable VALUES (5,'First'), (5,'Second'), (6,'Other');");

    LWODataCache cache;
    ASSERT_TRUE(cache.LoadFromBytes(fdb.readBytes()));

    auto matches = cache.QueryTableRowsById("TestTable", 5);
    ASSERT_EQ(matches.size(), 2u);
    std::vector<std::string> names;
    for (auto* r : matches) names.push_back(std::get<std::string>(r->fields[1]));
    EXPECT_NE(std::find(names.begin(), names.end(), "First"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "Second"), names.end());

    EXPECT_EQ(cache.QueryTableRowsById("TestTable", 6).size(), 1u);
    EXPECT_TRUE(cache.QueryTableRowsById("TestTable", 999).empty());
}

TEST_F(LWODataCacheTest, FindTableIndexByNameAndGetTableByIndex) {
    TempFdb fdb("CREATE TABLE TestTable (id INTEGER, name TEXT);",
                "INSERT INTO TestTable VALUES (1,'Alice');");

    LWODataCache cache;
    ASSERT_TRUE(cache.LoadFromBytes(fdb.readBytes()));

    int32_t idx = cache.FindTableIndexByName("TestTable");
    EXPECT_GE(idx, 0);
    EXPECT_EQ(cache.GetTableByIndex(idx), cache.GetTable("TestTable"));

    EXPECT_EQ(cache.FindTableIndexByName("DoesNotExist"), -1);
    EXPECT_EQ(cache.GetTableByIndex(-1), nullptr);
    EXPECT_EQ(cache.GetTableByIndex(9999), nullptr);
}

TEST_F(LWODataCacheTest, LoadFdbFileReadsFromDisk) {
    TempFdb fdb("CREATE TABLE TestTable (id INTEGER, name TEXT);",
                "INSERT INTO TestTable VALUES (42,'OnDisk');");

    LWODataCache cache;
    ASSERT_TRUE(cache.LoadFdbFile(fdb.path()));
    EXPECT_TRUE(cache.IsLoaded());

    const FdbRow* row = cache.QueryTableRowById("TestTable", 42);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(std::get<std::string>(row->fields[1]), "OnDisk");
}

TEST_F(LWODataCacheTest, LoadFdbFileMissingFileFails) {
    LWODataCache cache;
    EXPECT_FALSE(cache.LoadFdbFile("/nonexistent/path/does-not-exist.fdb"));
    EXPECT_FALSE(cache.IsLoaded());
}

TEST_F(LWODataCacheTest, LoadFromBytesMalformedDataFails) {
    std::vector<uint8_t> garbage = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};
    LWODataCache cache;
    EXPECT_FALSE(cache.LoadFromBytes(garbage));
    EXPECT_FALSE(cache.IsLoaded());
}
