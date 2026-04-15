#include "netdevil/database/fdb/fdb_writer.h"
#include "netdevil/database/fdb/fdb_types.h"

#include <sqlite3.h>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace lu::assets {

namespace {

// Binary buffer with pointer fixup support.
// Deferred writes collect data (strings, int64s) to be appended after
// all structural pointer slots are laid out, then fixed up.
class FdbBuffer {
public:
    size_t pos() const { return buf_.size(); }

    void write_u32(uint32_t v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        buf_.insert(buf_.end(), p, p + 4);
    }

    void write_i32(int32_t v) { write_u32(static_cast<uint32_t>(v)); }

    void write_f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        write_u32(bits);
    }

    void write_i64(int64_t v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        buf_.insert(buf_.end(), p, p + 8);
    }

    size_t reserve() {
        size_t off = pos();
        write_u32(0);
        return off;
    }

    void fixup(size_t offset, uint32_t value) {
        std::memcpy(buf_.data() + offset, &value, 4);
    }

    size_t write_string(const std::string& s) {
        size_t off = pos();
        buf_.insert(buf_.end(), s.begin(), s.end());
        buf_.push_back(0);
        return off;
    }

    // Write a pointer slot that will point to a string written later.
    // Returns the fixup offset. Call flush_deferred() to write the string.
    size_t write_string_ptr(const std::string& s) {
        size_t fixup_off = reserve();
        deferred_.push_back({fixup_off, s, 0, false});
        return fixup_off;
    }

    // Write a pointer slot that will point to an int64 written later.
    size_t write_i64_ptr(int64_t v) {
        size_t fixup_off = reserve();
        deferred_.push_back({fixup_off, {}, v, true});
        return fixup_off;
    }

    // Flush all deferred data: write strings/int64s and fix up their pointers.
    void flush_deferred() {
        for (auto& d : deferred_) {
            if (d.is_i64) {
                fixup(d.ptr_offset, static_cast<uint32_t>(pos()));
                write_i64(d.i64_val);
            } else {
                fixup(d.ptr_offset, static_cast<uint32_t>(pos()));
                write_string(d.str_val);
            }
        }
        deferred_.clear();
    }

    const std::vector<uint8_t>& data() const { return buf_; }

private:
    struct Deferred {
        size_t ptr_offset;
        std::string str_val;
        int64_t i64_val;
        bool is_i64;
    };
    std::vector<uint8_t> buf_;
    std::vector<Deferred> deferred_;
};

FdbDataType sqlite_type_to_fdb(const std::string& type_name) {
    if (type_name == "int32" || type_name == "INTEGER" || type_name == "INT")
        return FdbDataType::INT32;
    if (type_name == "real" || type_name == "REAL" || type_name == "FLOAT")
        return FdbDataType::REAL;
    if (type_name == "text_4" || type_name == "TEXT" || type_name == "VARCHAR")
        return FdbDataType::TEXT_4;
    if (type_name == "text_8")
        return FdbDataType::TEXT_8;
    if (type_name == "int_bool" || type_name == "BOOLEAN")
        return FdbDataType::BOOL;
    if (type_name == "int64" || type_name == "BIGINT")
        return FdbDataType::INT64;
    if (type_name == "none")
        return FdbDataType::NOTHING;
    return FdbDataType::TEXT_4;
}

uint32_t next_pow2(uint32_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1; n |= n >> 2; n |= n >> 4; n |= n >> 8; n |= n >> 16;
    return n + 1;
}

uint32_t row_hash(sqlite3_stmt* stmt, FdbDataType first_col_type, uint32_t bucket_count) {
    if (bucket_count == 0) return 0;
    if (sqlite3_column_type(stmt, 0) == SQLITE_NULL) return 0;
    uint32_t h = 0;
    switch (first_col_type) {
        case FdbDataType::INT32:
        case FdbDataType::BOOL:
            h = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
            break;
        case FdbDataType::INT64:
            h = static_cast<uint32_t>(sqlite3_column_int64(stmt, 0));
            break;
        default: {
            auto* text = sqlite3_column_text(stmt, 0);
            if (text) {
                h = 5381;
                for (auto* s = reinterpret_cast<const char*>(text); *s; ++s)
                    h = h * 33 + static_cast<uint32_t>(*s);
            }
            break;
        }
    }
    return h % bucket_count;
}

struct SqliteClose { void operator()(sqlite3* db) { sqlite3_close(db); } };
struct StmtClose { void operator()(sqlite3_stmt* s) { sqlite3_finalize(s); } };
using DbPtr = std::unique_ptr<sqlite3, SqliteClose>;
using StmtPtr = std::unique_ptr<sqlite3_stmt, StmtClose>;

StmtPtr sql_prepare(sqlite3* db, const std::string& sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK)
        throw FdbError(std::string("SQL: ") + sqlite3_errmsg(db));
    return StmtPtr(raw);
}

struct ColInfo { std::string name; FdbDataType type; };
struct TableDef { std::string name; std::vector<ColInfo> columns; };

} // anonymous namespace

void sqlite_to_fdb(const std::string& sqlite_path, const std::string& fdb_path) {
    sqlite3* raw = nullptr;
    if (sqlite3_open_v2(sqlite_path.c_str(), &raw, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        std::string err = sqlite3_errmsg(raw);
        sqlite3_close(raw);
        throw FdbError("Cannot open SQLite: " + err);
    }
    DbPtr db(raw);

    // Collect table definitions
    std::vector<TableDef> tables;
    {
        auto stmt = sql_prepare(db.get(),
            "SELECT name FROM sqlite_master WHERE type='table' "
            "AND name NOT LIKE 'sqlite_%' ORDER BY rowid");
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            auto* n = sqlite3_column_text(stmt.get(), 0);
            if (!n) continue;
            TableDef td;
            td.name = reinterpret_cast<const char*>(n);
            auto info = sql_prepare(db.get(), "PRAGMA table_info('" + td.name + "')");
            while (sqlite3_step(info.get()) == SQLITE_ROW) {
                ColInfo ci;
                auto* cn = sqlite3_column_text(info.get(), 1);
                auto* ct = sqlite3_column_text(info.get(), 2);
                ci.name = cn ? reinterpret_cast<const char*>(cn) : "";
                ci.type = sqlite_type_to_fdb(ct ? reinterpret_cast<const char*>(ct) : "");
                td.columns.push_back(std::move(ci));
            }
            tables.push_back(std::move(td));
        }
    }

    FdbBuffer buf;
    uint32_t table_count = static_cast<uint32_t>(tables.size());

    // === File header ===
    // [table_count] [ptr -> table_array]
    buf.write_u32(table_count);
    size_t table_array_fixup = buf.reserve();

    // === Table array ===
    // Per table: [ptr -> col_header] [ptr -> row_header]
    buf.fixup(table_array_fixup, static_cast<uint32_t>(buf.pos()));
    std::vector<size_t> col_hdr_fixups(table_count);
    std::vector<size_t> row_hdr_fixups(table_count);
    for (uint32_t t = 0; t < table_count; ++t) {
        col_hdr_fixups[t] = buf.reserve();
        row_hdr_fixups[t] = buf.reserve();
    }

    for (uint32_t t = 0; t < table_count; ++t) {
        const auto& td = tables[t];
        uint32_t ncols = static_cast<uint32_t>(td.columns.size());

        // === Column header ===
        // Layout the reader expects (contiguous pointer slots):
        //   [u32 column_count]
        //   [u32 ptr -> table_name_string]   <- read_string_indirect
        //   [u32 ptr -> col_defs]            <- seek_pointer
        buf.fixup(col_hdr_fixups[t], static_cast<uint32_t>(buf.pos()));
        buf.write_u32(ncols);
        buf.write_string_ptr(td.name);     // deferred string
        size_t coldefs_fixup = buf.reserve();

        // === Column definitions ===
        // Per col: [u32 data_type] [u32 ptr -> col_name_string]
        buf.fixup(coldefs_fixup, static_cast<uint32_t>(buf.pos()));
        for (uint32_t c = 0; c < ncols; ++c) {
            buf.write_u32(static_cast<uint32_t>(td.columns[c].type));
            buf.write_string_ptr(td.columns[c].name);  // deferred string
        }

        // Flush deferred strings for this table's schema
        buf.flush_deferred();

        // === Row header ===
        // [u32 bucket_count] [u32 ptr -> bucket_array]
        uint32_t row_count = 0;
        {
            auto stmt = sql_prepare(db.get(),
                "SELECT COUNT(*) FROM '" + td.name + "'");
            if (sqlite3_step(stmt.get()) == SQLITE_ROW)
                row_count = static_cast<uint32_t>(sqlite3_column_int(stmt.get(), 0));
        }
        uint32_t bucket_count = next_pow2(row_count > 0 ? row_count : 1);

        buf.fixup(row_hdr_fixups[t], static_cast<uint32_t>(buf.pos()));
        buf.write_u32(bucket_count);
        size_t buckets_fixup = buf.reserve();

        // Bucket array: [bucket_count * u32] initialized to -1
        buf.fixup(buckets_fixup, static_cast<uint32_t>(buf.pos()));
        std::vector<size_t> bucket_offsets(bucket_count);
        for (uint32_t b = 0; b < bucket_count; ++b) {
            bucket_offsets[b] = buf.pos();
            buf.write_u32(static_cast<uint32_t>(-1));
        }

        if (row_count == 0) continue;

        // Write rows, building hash chains.
        std::vector<size_t> bucket_tail_next(bucket_count, 0);

        auto stmt = sql_prepare(db.get(), "SELECT * FROM '" + td.name + "'");
        FdbDataType first_type = ncols > 0 ? td.columns[0].type : FdbDataType::NOTHING;

        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            uint32_t bucket = row_hash(stmt.get(), first_type, bucket_count);

            // Row chain entry: [ptr -> row_values] [next_ptr = -1]
            size_t chain_off = buf.pos();
            if (bucket_tail_next[bucket] != 0)
                buf.fixup(bucket_tail_next[bucket], static_cast<uint32_t>(chain_off));
            else
                buf.fixup(bucket_offsets[bucket], static_cast<uint32_t>(chain_off));

            size_t rv_fixup = buf.reserve();  // ptr -> row_values
            size_t next_fixup = buf.reserve(); // next_ptr
            buf.fixup(next_fixup, static_cast<uint32_t>(-1));
            bucket_tail_next[bucket] = next_fixup;

            // Row values: [column_count] [ptr -> field_data]
            buf.fixup(rv_fixup, static_cast<uint32_t>(buf.pos()));
            buf.write_u32(ncols);
            size_t fd_fixup = buf.reserve();

            // Field data: per col [data_type] [value (4 bytes)]
            // TEXT and INT64 use pointer indirection (deferred).
            buf.fixup(fd_fixup, static_cast<uint32_t>(buf.pos()));

            for (uint32_t c = 0; c < ncols; ++c) {
                int col = static_cast<int>(c);
                if (sqlite3_column_type(stmt.get(), col) == SQLITE_NULL) {
                    buf.write_u32(static_cast<uint32_t>(FdbDataType::NOTHING));
                    buf.write_u32(0);
                    continue;
                }

                FdbDataType ftype = td.columns[c].type;
                buf.write_u32(static_cast<uint32_t>(ftype));

                switch (ftype) {
                    case FdbDataType::INT32:
                        buf.write_i32(sqlite3_column_int(stmt.get(), col));
                        break;
                    case FdbDataType::REAL:
                        buf.write_f32(static_cast<float>(
                            sqlite3_column_double(stmt.get(), col)));
                        break;
                    case FdbDataType::BOOL:
                        buf.write_i32(sqlite3_column_int(stmt.get(), col) ? 1 : 0);
                        break;
                    case FdbDataType::TEXT_4:
                    case FdbDataType::TEXT_8: {
                        auto* text = sqlite3_column_text(stmt.get(), col);
                        std::string s = text ? reinterpret_cast<const char*>(text) : "";
                        buf.write_string_ptr(s);
                        break;
                    }
                    case FdbDataType::INT64:
                        buf.write_i64_ptr(sqlite3_column_int64(stmt.get(), col));
                        break;
                    default:
                        buf.write_u32(0);
                        break;
                }
            }

            // Flush deferred data for this row (strings, int64s)
            buf.flush_deferred();
        }
    }

    std::ofstream out(fdb_path, std::ios::binary);
    if (!out) throw FdbError("Cannot write: " + fdb_path);
    out.write(reinterpret_cast<const char*>(buf.data().data()),
              static_cast<std::streamsize>(buf.data().size()));
}

} // namespace lu::assets
