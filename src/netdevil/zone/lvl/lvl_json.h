#pragma once

#include "netdevil/zone/lvl/lvl_types.h"
#include "netdevil/zone/luz/luz_json.h"
#include "netdevil/common/ldf/ldf_json.h"
#include <nlohmann/json.hpp>

namespace lu::assets {

void to_json(nlohmann::json& j, const LvlCullVal& v);
void from_json(const nlohmann::json& j, LvlCullVal& v);
void to_json(nlohmann::json& j, const LvlDrawDistances& d);
void from_json(const nlohmann::json& j, LvlDrawDistances& d);
void to_json(nlohmann::json& j, const LvlLightingInfo& l);
void from_json(const nlohmann::json& j, LvlLightingInfo& l);
void to_json(nlohmann::json& j, const LvlSkydomeInfo& s);
void from_json(const nlohmann::json& j, LvlSkydomeInfo& s);
void to_json(nlohmann::json& j, const LvlEditorColor& c);
void from_json(const nlohmann::json& j, LvlEditorColor& c);
void to_json(nlohmann::json& j, const LvlEditorSettings& e);
void from_json(const nlohmann::json& j, LvlEditorSettings& e);
void to_json(nlohmann::json& j, const LvlEnvironmentData& e);
void from_json(const nlohmann::json& j, LvlEnvironmentData& e);

void to_json(nlohmann::json& j, const LvlRenderAttr& a);
void from_json(const nlohmann::json& j, LvlRenderAttr& a);
void to_json(nlohmann::json& j, const LvlRenderTechnique& t);
void from_json(const nlohmann::json& j, LvlRenderTechnique& t);

void to_json(nlohmann::json& j, const LvlObject& o);
void from_json(const nlohmann::json& j, LvlObject& o);
void to_json(nlohmann::json& j, const LvlParticle& p);
void from_json(const nlohmann::json& j, LvlParticle& p);
void to_json(nlohmann::json& j, const LvlFile& f);
void from_json(const nlohmann::json& j, LvlFile& f);

} // namespace lu::assets
