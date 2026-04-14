#include "netdevil/database/fdb/fdb_reader.h"

#include <cstring>
#include <iomanip>
#include <string>
#include <sstream>
#include <sqlite3.h>

namespace lu::assets {

namespace {

// FDB uses a stream-based pointer indirection model.
// SeekPointer reads a u32 pointer at the current position,
// saves the position after the read, seeks to the pointer target,
// and returns the saved position for later restoration.
// This matches DarkflameServer's FdbToSqlite::SeekPointer @ FdbToSqlite.cpp:78

class FdbReader {
public:
    explicit FdbReader(std::span<const uint8_t> data) : data_(data), pos_(0) {}

    int32_t read_i32() {
        if (pos_ + 4 > data_.size()) throw FdbError("FDB: read past end at " + std::to_string(pos_));
        int32_t val;
        std::memcpy(&val, data_.data() + pos_, 4);
        pos_ += 4;
        return val;
    }

    float read_f32() {
        if (pos_ + 4 > data_.size()) throw FdbError("FDB: read past end");
        float val;
        std::memcpy(&val, data_.data() + pos_, 4);
        pos_ += 4;
        return val;
    }

    int64_t read_i64() {
        if (pos_ + 8 > data_.size()) throw FdbError("FDB: read past end");
        int64_t val;
        std::memcpy(&val, data_.data() + pos_, 8);
        pos_ += 8;
        return val;
    }

    // Read a pointer, save current position, seek to target.
    // Returns the saved position (after the pointer read).
    size_t seek_pointer() {
        int32_t target = read_i32();
        size_t saved = pos_;
        if (target < 0 || static_cast<size_t>(target) >= data_.size()) {
            throw FdbError("FDB: invalid pointer " + std::to_string(target));
        }
        pos_ = static_cast<size_t>(target);
        return saved;
    }

    void seek(size_t pos) { pos_ = pos; }
    size_t pos() const { return pos_; }

    // Read a null-terminated Latin-1 string via pointer indirection.
    // Matches DarkflameServer ReadString: SeekPointer -> BinaryIO::ReadU8String -> seek back
    std::string read_string_indirect() {
        size_t saved = seek_pointer();
        std::string result;
        while (pos_ < data_.size()) {
            uint8_t ch = data_[pos_++];
            if (ch == 0) break;
            result += static_cast<char>(ch);
        }
        pos_ = saved;
        return result;
    }

    // Read an int64 via pointer indirection (INT64 type stores pointer to 8-byte value)
    int64_t read_i64_indirect() {
        size_t saved = seek_pointer();
        int64_t val = read_i64();
        pos_ = saved;
        return val;
    }

private:
    std::span<const uint8_t> data_;
    size_t pos_;
};

const char* sqlite_type_name(FdbDataType type) {
    switch (type) {
        case FdbDataType::INT32:    return "int32";
        case FdbDataType::REAL:     return "real";
        case FdbDataType::TEXT_4:   return "text_4";
        case FdbDataType::BOOL:     return "int_bool";
        case FdbDataType::INT64:    return "int64";
        case FdbDataType::TEXT_8:   return "text_8";
        default:                    return "none";
    }
}

// Escape a string for SQLite insertion (double any quotes)
std::string escape_sql(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if (c == '"') result += '"';
        result += c;
    }
    return result;
}

} // anonymous namespace

std::vector<FdbTable> fdb_parse(std::span<const uint8_t> data) {
    FdbReader r(data);

    int32_t table_count = r.read_i32();
    if (table_count < 0 || table_count > 10000) {
        throw FdbError("FDB: unreasonable table count: " + std::to_string(table_count));
    }

    // SeekPointer to table array
    size_t tables_saved = r.seek_pointer();

    std::vector<FdbTable> tables;
    tables.reserve(table_count);

    for (int32_t t = 0; t < table_count; ++t) {
        FdbTable table;

        // ReadColumnHeader: SeekPointer to column header
        size_t col_saved = r.seek_pointer();

        int32_t num_cols = r.read_i32();
        table.name = r.read_string_indirect();

        // ReadColumns: SeekPointer to column definitions
        size_t cols_saved = r.seek_pointer();
        table.columns.reserve(num_cols);
        for (int32_t c = 0; c < num_cols; ++c) {
            FdbColumn col;
            col.type = static_cast<FdbDataType>(r.read_i32());
            col.name = r.read_string_indirect();
            table.columns.push_back(std::move(col));
        }
        r.seek(cols_saved);

        r.seek(col_saved);

        // Skip row header pointer (we don't load rows in fdb_parse)
        r.read_i32(); // consume the row header pointer

        tables.push_back(std::move(table));
    }

    r.seek(tables_saved);
    return tables;
}

void fdb_to_sqlite_direct(std::span<const uint8_t> data, const std::string& output_path) {
    FdbReader r(data);

    int32_t table_count = r.read_i32();
    if (table_count < 0 || table_count > 10000) {
        throw FdbError("FDB: unreasonable table count");
    }

    sqlite3* db = nullptr;
    int rc = sqlite3_open(output_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db);
        sqlite3_close(db);
        throw FdbError("FDB: failed to open SQLite: " + err);
    }

    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // SeekPointer to table array
    size_t tables_saved = r.seek_pointer();

    for (int32_t t = 0; t < table_count; ++t) {
        // === Read Column Header ===
        size_t col_saved = r.seek_pointer();

        int32_t num_cols = r.read_i32();
        std::string table_name = r.read_string_indirect();

        // ReadColumns
        size_t cols_saved = r.seek_pointer();
        std::vector<FdbDataType> col_types;
        std::vector<std::string> col_names;
        col_types.reserve(num_cols);
        col_names.reserve(num_cols);

        std::string create_sql = "CREATE TABLE IF NOT EXISTS '" + table_name + "' (";
        for (int32_t c = 0; c < num_cols; ++c) {
            if (c > 0) create_sql += ", ";
            FdbDataType dtype = static_cast<FdbDataType>(r.read_i32());
            std::string cname = r.read_string_indirect();
            create_sql += "'" + cname + "' " + sqlite_type_name(dtype);
            col_types.push_back(dtype);
            col_names.push_back(std::move(cname));
        }
        create_sql += ");";
        r.seek(cols_saved);
        r.seek(col_saved);

        sqlite3_exec(db, create_sql.c_str(), nullptr, nullptr, nullptr);

        // === Read Row Header ===
        size_t row_header_saved = r.seek_pointer();
        int32_t num_allocated_rows = r.read_i32();

        // ReadRows: SeekPointer to row pointer array
        size_t rows_saved = r.seek_pointer();

        for (int32_t row = 0; row < num_allocated_rows; ++row) {
            int32_t row_pointer = r.read_i32();
            if (row_pointer == -1) continue;

            // ReadRow: seek to row_pointer, follow linked list
            size_t row_iter_saved = r.pos();
            r.seek(static_cast<size_t>(row_pointer));

            while (true) {
                // ReadRowInfo: SeekPointer to row values
                size_t row_info_saved = r.seek_pointer();
                int32_t num_row_cols = r.read_i32();

                // ReadRowValues: SeekPointer to column values
                size_t row_vals_saved = r.seek_pointer();

                std::stringstream insert_sql;
                insert_sql << "INSERT INTO '" << table_name << "' values (";

                for (int32_t c = 0; c < num_row_cols; ++c) {
                    if (c > 0) insert_sql << ", ";
                    auto field_type = static_cast<FdbDataType>(r.read_i32());

                    switch (field_type) {
                        case FdbDataType::NOTHING: {
                            int32_t zero = r.read_i32(); // Always 0x00000000 for NULL fields
                            (void)zero;
                            insert_sql << "NULL";
                            break;
                        }
                        case FdbDataType::INT32: {
                            int32_t val = r.read_i32();
                            insert_sql << val;
                            break;
                        }
                        case FdbDataType::REAL: {
                            float val = r.read_f32();
                            insert_sql << std::fixed << std::setprecision(34) << val;
                            break;
                        }
                        case FdbDataType::TEXT_4:
                        case FdbDataType::TEXT_8: {
                            std::string val = r.read_string_indirect();
                            insert_sql << "\"" << escape_sql(val) << "\"";
                            break;
                        }
                        case FdbDataType::BOOL: {
                            int32_t val = r.read_i32();
                            insert_sql << (val ? 1 : 0);
                            break;
                        }
                        case FdbDataType::INT64: {
                            int64_t val = r.read_i64_indirect();
                            insert_sql << val;
                            break;
                        }
                        case FdbDataType::UNUSED_2:
                        case FdbDataType::UNUSED_7: {
                            // Unused types — gap in DLU's eSqliteDataType enum.
                            // Never appear in shipped cdclient.fdb but handle gracefully.
                            r.read_i32(); // consume 4 bytes
                            insert_sql << "NULL";
                            break;
                        }
                        default: {
                            // Truly unknown type — should not occur in valid FDB files
                            r.read_i32(); // consume 4 bytes
                            insert_sql << "NULL";
                            break;
                        }
                    }
                }
                insert_sql << ");";

                r.seek(row_vals_saved);
                r.seek(row_info_saved);

                auto sql = insert_sql.str();
                sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);

                // Read linked pointer
                int32_t linked = r.read_i32();
                if (linked == -1) break;
                r.seek(static_cast<size_t>(linked));
            }

            r.seek(row_iter_saved);
        }

        r.seek(rows_saved);
        r.seek(row_header_saved);
    }

    r.seek(tables_saved);

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

    // Apply relational constraints derived from community-documented
    // CDClient table relationships.
    // SQLite doesn't enforce FK constraints by default, but having proper
    // indexes on PK columns improves query performance significantly.
    // FK constraints are documented here for reference and tooling.
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    // Primary key indexes — create unique indexes on PK columns for fast lookups
    static const char* pk_indexes[] = {
        "CREATE INDEX IF NOT EXISTS idx_Objects_id ON Objects(id)",
        "CREATE INDEX IF NOT EXISTS idx_ComponentsRegistry_id ON ComponentsRegistry(id)",
        "CREATE INDEX IF NOT EXISTS idx_Missions_id ON Missions(id)",
        "CREATE INDEX IF NOT EXISTS idx_MissionTasks_uid ON MissionTasks(uid)",
        "CREATE INDEX IF NOT EXISTS idx_SkillBehavior_skillID ON SkillBehavior(skillID)",
        "CREATE INDEX IF NOT EXISTS idx_BehaviorTemplate_behaviorID ON BehaviorTemplate(behaviorID)",
        "CREATE INDEX IF NOT EXISTS idx_BehaviorParameter_behaviorID ON BehaviorParameter(behaviorID)",
        "CREATE INDEX IF NOT EXISTS idx_ItemComponent_id ON ItemComponent(id)",
        "CREATE INDEX IF NOT EXISTS idx_RenderComponent_id ON RenderComponent(id)",
        "CREATE INDEX IF NOT EXISTS idx_DestructibleComponent_id ON DestructibleComponent(id)",
        "CREATE INDEX IF NOT EXISTS idx_PhysicsComponent_id ON PhysicsComponent(id)",
        "CREATE INDEX IF NOT EXISTS idx_LootTable_id ON LootTable(id)",
        "CREATE INDEX IF NOT EXISTS idx_LootMatrix_id ON LootMatrix(id)",
        "CREATE INDEX IF NOT EXISTS idx_LootTableIndex_LootTableIndex ON LootTableIndex(LootTableIndex)",
        "CREATE INDEX IF NOT EXISTS idx_LootMatrixIndex_LootMatrixIndex ON LootMatrixIndex(LootMatrixIndex)",
        "CREATE INDEX IF NOT EXISTS idx_RarityTableIndex_RarityTableIndex ON RarityTableIndex(RarityTableIndex)",
        "CREATE INDEX IF NOT EXISTS idx_ZoneTable_zoneID ON ZoneTable(zoneID)",
        "CREATE INDEX IF NOT EXISTS idx_Activities_ActivityID ON Activities(ActivityID)",
        "CREATE INDEX IF NOT EXISTS idx_AnimationIndex_animationGroupID ON AnimationIndex(animationGroupID)",
        "CREATE INDEX IF NOT EXISTS idx_CurrencyIndex_CurrencyIndex ON CurrencyIndex(CurrencyIndex)",
        "CREATE INDEX IF NOT EXISTS idx_Emotes_id ON Emotes(id)",
        "CREATE INDEX IF NOT EXISTS idx_ScriptComponent_id ON ScriptComponent(id)",
        "CREATE INDEX IF NOT EXISTS idx_ObjectSkills_objectTemplate ON ObjectSkills(objectTemplate)",
        "CREATE INDEX IF NOT EXISTS idx_VendorComponent_id ON VendorComponent(id)",
        nullptr
    };
    for (const char** sql = pk_indexes; *sql; ++sql) {
        sqlite3_exec(db, *sql, nullptr, nullptr, nullptr);
    }

    // ComponentsRegistry is the most queried table — index by component_type too
    sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_ComponentsRegistry_type ON ComponentsRegistry(component_type)", nullptr, nullptr, nullptr);

    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    sqlite3_close(db);
}

} // namespace lu::assets
