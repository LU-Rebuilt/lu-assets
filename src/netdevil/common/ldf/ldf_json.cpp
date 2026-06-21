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
        arr.push_back(json{{"key", key}, {"value", val}});
    }
    return arr;
}

LdfConfig ldf_config_from_json(const json& j) {
    LdfConfig config;
    for (auto& item : j) {
        config.emplace_back(item.at("key").get<std::string>(),
                            item.at("value").get<std::string>());
    }
    return config;
}

} // namespace lu::assets
