#include "microsoft/fxo/fxo_reader.h"
#include "common/binary_reader/binary_reader.h"
#include <string>

#include <cstring>

namespace lu::assets {

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

// Align a byte offset up to the next 4-byte boundary.
static constexpr size_t align4(size_t v) { return (v + 3) & ~size_t{3}; }

// Read a null-terminated string whose byte count (including null) is given by
// a preceding u32 length field. The string data is padded to a 4-byte boundary.
// Returns the string (without null), advances *pos past the padded data.
static std::string read_len_string(const uint8_t* data, size_t file_size,
                                   size_t& pos) {
    if (pos + 4 > file_size)
        throw FxoError("FXO: truncated reading string length");

    uint32_t len;
    std::memcpy(&len, data + pos, 4);
    pos += 4;

    if (len == 0 || len > 512)
        throw FxoError("FXO: implausible string length " + std::to_string(len));
    if (pos + len > file_size)
        throw FxoError("FXO: truncated reading string data");

    // Last byte must be null terminator
    if (data[pos + len - 1] != 0)
        throw FxoError("FXO: string missing null terminator");

    std::string result(reinterpret_cast<const char*>(data + pos), len - 1);
    pos += align4(len);
    return result;
}

// Peek at a u32 without advancing.
static uint32_t peek_u32(const uint8_t* data, size_t pos) {
    uint32_t v;
    std::memcpy(&v, data + pos, 4);
    return v;
}

// -------------------------------------------------------------------------
// Parameter scanner
//
// Parameters are stored as variable-sized blocks starting at offset 0x64.
// Each block:
//   u32  preamble  (always 0 in LU)
//   u32  name_len  (including null, must be 2..128 for valid params)
//   char name[]    (padded to 4)
//   u32  sem_len
//   char sem[]     (padded to 4)
//   u32  type      (D3DXPARAMETER_TYPE: 0-14)
//   u32  class     (D3DXPARAMETER_CLASS: 0-5)
//   u32  reg_off
//   u32  reg_cnt
//   u32  ann_cnt
//   u32  rows      (1-4)
//   u32  cols      (1-4)
//   u8   tail[]    (variable: default values + annotation records)
//
// We scan forward, stopping when the block no longer matches this layout.
// The tail's end is determined by finding the next valid preamble+name_len pair,
// or by stopping at the technique section (identified by "Technique_" strings).
// -------------------------------------------------------------------------

static bool looks_like_ascii(const uint8_t* data, size_t pos, uint32_t len) {
    for (uint32_t i = 0; i < len - 1; ++i) {
        uint8_t c = data[pos + i];
        if (c < 32 || c >= 127) return false;
    }
    return data[pos + len - 1] == 0;
}

static bool looks_like_param_start(const uint8_t* data, size_t file_size, size_t pos) {
    if (pos + 16 > file_size) return false;
    uint32_t preamble = peek_u32(data, pos);
    if (preamble != 0) return false;
    uint32_t name_len = peek_u32(data, pos + 4);
    if (name_len < 2 || name_len > 128) return false;
    if (pos + 8 + align4(name_len) + 4 > file_size) return false;
    if (!looks_like_ascii(data, pos + 8, name_len)) return false;
    // Also check that a semantic string follows
    size_t sem_pos = pos + 8 + align4(name_len);
    uint32_t sem_len = peek_u32(data, sem_pos);
    if (sem_len < 2 || sem_len > 64) return false;
    if (sem_pos + 4 + align4(sem_len) + 28 > file_size) return false;
    if (!looks_like_ascii(data, sem_pos + 4, sem_len)) return false;
    // Check type/class/rows/cols plausibility
    size_t field_pos = sem_pos + 4 + align4(sem_len);
    uint32_t type = peek_u32(data, field_pos);
    uint32_t cls  = peek_u32(data, field_pos + 4);
    uint32_t rows = peek_u32(data, field_pos + 20);
    uint32_t cols = peek_u32(data, field_pos + 24);
    if (type > 14 || cls > 5) return false;
    if (rows == 0 || rows > 4 || cols == 0 || cols > 4) return false;
    return true;
}

static std::vector<FxoParameter> scan_parameters(const uint8_t* data,
                                                  size_t file_size, size_t start) {
    std::vector<FxoParameter> params;
    size_t pos = start;

    while (pos + 16 < file_size) {
        if (!looks_like_param_start(data, file_size, pos)) break;

        FxoParameter p;
        pos += 4; // skip preamble

        // Name
        size_t name_len = peek_u32(data, pos);
        p.name = std::string(reinterpret_cast<const char*>(data + pos + 4),
                             name_len - 1);
        pos += 4 + align4(name_len);

        // Semantic
        size_t sem_len = peek_u32(data, pos);
        p.semantic = std::string(reinterpret_cast<const char*>(data + pos + 4),
                                 sem_len - 1);
        pos += 4 + align4(sem_len);

        // Fixed fields
        p.type        = peek_u32(data, pos);      pos += 4;
        p.param_class = peek_u32(data, pos);      pos += 4;
        p.reg_offset  = peek_u32(data, pos);      pos += 4;
        p.reg_count   = peek_u32(data, pos);      pos += 4;
        p.ann_count   = peek_u32(data, pos);      pos += 4;
        p.rows        = peek_u32(data, pos);      pos += 4;
        p.cols        = peek_u32(data, pos);      pos += 4;

        params.push_back(std::move(p));

        // Advance past the variable-length tail by scanning for the next
        // valid parameter start. The tail contains default values and per-register
        // data whose size depends on type. We scan forward in 4-byte steps.
        bool found_next = false;
        for (size_t scan = pos; scan + 16 < file_size && scan < pos + 256; scan += 4) {
            if (looks_like_param_start(data, file_size, scan)) {
                pos = scan;
                found_next = true;
                break;
            }
        }
        if (!found_next) break;
    }

    return params;
}

// -------------------------------------------------------------------------
// Technique scanner
//
// Technique names in the LU client all begin with "Technique_".
// We scan the whole file for this prefix and extract the null-terminated names.
// -------------------------------------------------------------------------

static std::vector<FxoTechnique> scan_techniques(const uint8_t* data,
                                                  size_t file_size) {
    std::vector<FxoTechnique> techs;
    const uint8_t prefix[] = "Technique_";
    const size_t prefix_len = sizeof(prefix) - 1; // excludes null

    size_t pos = 0;
    while (pos + prefix_len < file_size) {
        if (std::memcmp(data + pos, prefix, prefix_len) == 0) {
            // Find null terminator
            size_t end = pos + prefix_len;
            while (end < file_size && data[end] != 0 && (data[end] >= 32 && data[end] < 127))
                ++end;
            if (end < file_size && data[end] == 0 && end > pos + prefix_len) {
                FxoTechnique t;
                t.name = std::string(reinterpret_cast<const char*>(data + pos),
                                     end - pos);
                techs.push_back(std::move(t));
            }
            pos = end + 1;
        } else {
            ++pos;
        }
    }
    return techs;
}

// -------------------------------------------------------------------------
// Public entry point
// -------------------------------------------------------------------------

FxoFile fxo_parse(std::span<const uint8_t> data) {
    FxoFile fxo;
    fxo.total_size = data.size();

    // Empty file = FXP pool file (common.fxp, legoppcommon.fxp are 0 bytes in LU)
    if (data.empty()) return fxo;

    if (data.size() < 16)
        throw FxoError("FXO: file too small (" + std::to_string(data.size()) + " bytes)");

    const uint8_t* raw = data.data();
    const size_t sz = data.size();

    // Store full file bytes (no gaps — shader bytecode is included)
    fxo.raw_data.assign(data.begin(), data.end());

    // Header
    fxo.signature = peek_u32(raw, 0x00);
    if (fxo.signature != FXO_SIGNATURE)
        throw FxoError("FXO: bad signature " + std::to_string(fxo.signature));

    fxo.meta_size = peek_u32(raw, 0x04);
    fxo.unk_08    = peek_u32(raw, 0x08);
    fxo.unk_0c    = peek_u32(raw, 0x0C);
    fxo.unk_10    = peek_u32(raw, 0x10);
    fxo.unk_14    = peek_u32(raw, 0x14);
    fxo.unk_18    = peek_u32(raw, 0x18);

    // Parameter table starts at 0x64 (0x60 header + 4-byte table preamble).
    // All LU FXOs have a 0x60-byte header + 4-byte zero at 0x60, so params start at 0x64.
    // Verified: peek at 0x60 = 0 in all 32 client FXOs.
    if (sz > 0x64)
        fxo.parameters = scan_parameters(raw, sz, 0x64);

    // Technique names by prefix scan (all LU techniques start with "Technique_")
    fxo.techniques = scan_techniques(raw, sz);

    return fxo;
}

} // namespace lu::assets
