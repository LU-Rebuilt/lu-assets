#include "netdevil/common/ldf/ldf_json.h"
#include "netdevil/common/ldf/ldf_reader.h"

using json = nlohmann::json;

namespace lu::assets {

static const char* ldf_type_name(LdfType t) {
    switch (t) {
    case LdfType::WString: return "WString";
    case LdfType::S32:     return "S32";
    case LdfType::Float:   return "Float";
    case LdfType::Double:  return "Double";
    case LdfType::U32:     return "U32";
    case LdfType::Bool:    return "Bool";
    case LdfType::U64:     return "U64";
    case LdfType::ObjId:   return "ObjId";
    case LdfType::Utf8:    return "Utf8";
    case LdfType::Unknown: return "Unknown";
    }
    return "Unknown";
}

void to_json(json& j, const LdfEntry& e) {
    j = json{
        {"key", e.key},
        {"type", static_cast<uint8_t>(e.type)},
        {"type_name", ldf_type_name(e.type)},
        {"raw_value", e.raw_value},
    };
}

void from_json(const json& j, LdfEntry& e) {
    j.at("key").get_to(e.key);
    e.type = static_cast<LdfType>(j.value("type", uint8_t(255)));
    e.raw_value = j.value("raw_value", "");
    std::string line = e.key + "=" + std::to_string(static_cast<int>(e.type)) + ":" + e.raw_value;
    auto entries = ldf_parse(line);
    if (!entries.empty()) {
        e.value = entries[0].value;
    } else {
        e.value = e.raw_value;
    }
}

json ldf_config_to_json(const LdfConfig& config) {
    json arr = json::array();
    for (auto& [key, val] : config) {
        auto colon = val.find(':');
        if (colon != std::string::npos) {
            int type_id = std::atoi(val.substr(0, colon).c_str());
            std::string raw = val.substr(colon + 1);
            arr.push_back(json{
                {"key", key},
                {"type", type_id},
                {"type_name", ldf_type_name(static_cast<LdfType>(type_id))},
                {"raw_value", raw},
            });
        } else {
            arr.push_back(json{
                {"key", key},
                {"type", 255},
                {"type_name", "Unknown"},
                {"raw_value", val},
            });
        }
    }
    return arr;
}

LdfConfig ldf_config_from_json(const json& j) {
    LdfConfig config;
    for (auto& item : j) {
        std::string key = item.at("key").get<std::string>();
        if (item.contains("type") && item.contains("raw_value")) {
            int type_id = item.at("type").get<int>();
            std::string raw = item.at("raw_value").get<std::string>();
            if (type_id == 255) {
                config.emplace_back(std::move(key), std::move(raw));
            } else {
                config.emplace_back(std::move(key),
                                    std::to_string(type_id) + ":" + raw);
            }
        } else {
            config.emplace_back(std::move(key),
                                item.at("value").get<std::string>());
        }
    }
    return config;
}

} // namespace lu::assets
