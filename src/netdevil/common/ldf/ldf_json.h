#pragma once

#include "netdevil/common/ldf/ldf_types.h"
#include <nlohmann/json.hpp>

namespace lu::assets {

// JSON output includes "type" (numeric eLDFType ID) and "type_name" (human-readable label).
// On deserialization only "type" is used; "type_name" is informational and ignored.
void to_json(nlohmann::json& j, const LdfEntry& e);
void from_json(const nlohmann::json& j, LdfEntry& e);

nlohmann::json ldf_config_to_json(const LdfConfig& config);
LdfConfig ldf_config_from_json(const nlohmann::json& j);

} // namespace lu::assets
