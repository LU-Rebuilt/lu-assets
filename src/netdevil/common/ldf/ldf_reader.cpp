#include "netdevil/common/ldf/ldf_reader.h"

#include <charconv>
#include <string>
#include <cstring>
#include <sstream>

namespace lu::assets {

namespace {

// Split a string_view by a delimiter, returning the part before the first
// occurrence and updating `sv` to the remainder (excluding the delimiter).
// Returns false if the delimiter is not found.
bool split_once(std::string_view& sv, char delim, std::string_view& before) {
    auto pos = sv.find(delim);
    if (pos == std::string_view::npos) return false;
    before = sv.substr(0, pos);
    sv = sv.substr(pos + 1);
    return true;
}

LdfType type_from_id(uint8_t id) {
    switch (id) {
    case 0:  return LdfType::WString;
    case 1:  return LdfType::S32;
    case 3:  return LdfType::Float;
    case 4:  return LdfType::Double;
    case 5:  return LdfType::U32;
    case 7:  return LdfType::Bool;
    case 8:  return LdfType::U64;
    case 9:  return LdfType::ObjId;
    case 13: return LdfType::Utf8;
    default: return LdfType::Unknown;
    }
}

LdfValue parse_value(LdfType type, std::string_view raw) {
    switch (type) {
    case LdfType::WString:
    case LdfType::Utf8:
    case LdfType::Unknown:
        return std::string(raw);

    case LdfType::S32: {
        int32_t v = 0;
        std::from_chars(raw.data(), raw.data() + raw.size(), v);
        return v;
    }
    case LdfType::Float: {
        // std::from_chars for float not available everywhere; use stof fallback
        float v = 0.0f;
        try { v = std::stof(std::string(raw)); } catch (...) {}
        return v;
    }
    case LdfType::Double: {
        double v = 0.0;
        try { v = std::stod(std::string(raw)); } catch (...) {}
        return v;
    }
    case LdfType::U32: {
        uint32_t v = 0;
        std::from_chars(raw.data(), raw.data() + raw.size(), v);
        return v;
    }
    case LdfType::Bool: {
        // "0" → false, "1" → true
        uint32_t v = 0;
        std::from_chars(raw.data(), raw.data() + raw.size(), v);
        return v != 0;
    }
    case LdfType::U64: {
        uint64_t v = 0;
        std::from_chars(raw.data(), raw.data() + raw.size(), v);
        return v;
    }
    case LdfType::ObjId: {
        int64_t v = 0;
        std::from_chars(raw.data(), raw.data() + raw.size(), v);
        return v;
    }
    }
    return std::string(raw);
}

} // anonymous namespace

std::vector<LdfEntry> ldf_parse(std::string_view text) {
    std::vector<LdfEntry> result;

    while (!text.empty()) {
        // Find end of line (skip CR if present)
        auto nl = text.find('\n');
        std::string_view line = (nl == std::string_view::npos) ? text : text.substr(0, nl);
        text = (nl == std::string_view::npos) ? std::string_view{} : text.substr(nl + 1);

        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;

        // Split key=type:value
        std::string_view key_part, type_value;
        if (!split_once(line, '=', key_part)) continue;
        type_value = line;  // remainder after '='

        std::string_view type_str, value_str;
        if (!split_once(type_value, ':', type_str)) continue;
        value_str = type_value;  // remainder after ':'

        // Parse type_id
        uint8_t type_id = 255;
        std::from_chars(type_str.data(), type_str.data() + type_str.size(), type_id);
        LdfType ltype = type_from_id(type_id);

        LdfEntry e;
        e.key       = std::string(key_part);
        e.type      = ltype;
        e.raw_value = std::string(value_str);
        e.value     = parse_value(ltype, value_str);
        result.push_back(std::move(e));
    }

    return result;
}

std::string ldf_entry_to_string(const LdfEntry& e, bool include_key) {
    std::string result;
    if (include_key) {
        result = e.key + '=';
    }

    // Append type_id
    result += std::to_string(static_cast<int>(e.type)) + ':';

    // Append value
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
            result += v;
        } else if constexpr (std::is_same_v<T, bool>) {
            result += v ? "1" : "0";
        } else {
            result += std::to_string(v);
        }
    }, e.value);

    return result;
}

// ── Binary LDF (LUZ waypoint config format) ─────────────────────────────────
// u32 count, then per entry: u1_wstr key + u1_wstr value.
// u1_wstr = u8 char_count + UTF-16LE chars. Converts to ASCII.
// Verified: ReadLUZPaths @ 0108caa0, lu_formats/luz.ksy lnv_entry.

LdfConfig ldf_parse_binary(BinaryReader& r) {
    LdfConfig config;
    uint32_t n = r.read_u32();
    config.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        // Read u1_wstr (u8 len + UTF-16LE chars)
        auto read_wstr8 = [&]() {
            uint8_t len = r.read_u8();
            std::string s;
            s.reserve(len);
            for (uint8_t j = 0; j < len; ++j) {
                uint16_t wc = r.read_u16();
                s += static_cast<char>(wc < 128 ? wc : '?');
            }
            return s;
        };
        std::string key = read_wstr8();
        std::string val = read_wstr8();
        config.emplace_back(std::move(key), std::move(val));
    }
    return config;
}

} // namespace lu::assets
