#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lu::assets {

// LDF (LnvData / LEGO Data Format) — typed key-value config system used
// throughout the LEGO Universe client.
//
// Text LDF (used in LVL object config strings and particle config_data):
//   Each entry is one line: key=type_id:value
//   Lines are separated by \n (LF). Keys and values are ASCII after UTF-16LE
//   decode. The value field is interpreted according to the type_id integer.
//
// Example:
//   CheckPrecondition=0:          (wstring, empty)
//   create_physics=7:1            (bool, true)
//   modelScale=3:1.5              (float)
//   objectLOT=1:6326              (s32)
//
// RE source: DarkflameServer/dCommon/LDFFormat.h (eLDFType enum)
// and legouniverse.exe object config string parsing.
//
// LUZ binary LDF (u32 count + wstr8 key + wstr8 value pairs) is parsed by
// ldf_parse_binary() in ldf_reader.h. It stores raw string pairs without
// the type_id encoding used in text LDF.

// eLDFType values (from DarkflameServer LDFFormat.h + Ghidra RE).
enum class LdfType : uint8_t {
    WString  = 0,   // UTF-16 string (appears as ASCII after conversion)
    S32      = 1,   // Signed 32-bit integer
    Float    = 3,   // Single-precision float
    Double   = 4,   // Double-precision float
    U32      = 5,   // Unsigned 32-bit integer
    Bool     = 7,   // Boolean (stored as "0" or "1")
    U64      = 8,   // Unsigned 64-bit integer
    ObjId    = 9,   // Signed 64-bit object ID (LWOOBJID)
    Utf8     = 13,  // UTF-8 string
    Unknown  = 255, // Unrecognised type_id — raw_value holds the original value string
};

// Typed value variant — active member corresponds to the LdfType.
// WString/Utf8/Unknown → string
// S32 → int32_t, Float → float, Double → double
// U32 → uint32_t, Bool → bool, U64 → uint64_t, ObjId → int64_t
using LdfValue = std::variant<
    std::string,  // WString(0), Utf8(13), Unknown(255)
    int32_t,      // S32(1)
    float,        // Float(3)
    double,       // Double(4)
    uint32_t,     // U32(5)
    bool,         // Bool(7)
    uint64_t,     // U64(8)
    int64_t       // ObjId(9)
>;

struct LdfEntry {
    std::string key;
    LdfType     type  = LdfType::Unknown;
    LdfValue    value;              // Parsed value, type matches `type`
    std::string raw_value;         // Original value string (before parsing)
};
// Raw string key-value pairs (no type IDs). Used by LUZ binary LDF in
// path waypoint configs.
using LdfConfig = std::vector<std::pair<std::string, std::string>>;

} // namespace lu::assets
