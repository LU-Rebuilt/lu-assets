#pragma once

#include "netdevil/zone/luz/luz_types.h"
#include <nlohmann/json.hpp>

namespace lu::assets {

void to_json(nlohmann::json& j, const Vec3& v);
void from_json(const nlohmann::json& j, Vec3& v);
void to_json(nlohmann::json& j, const Quat& q);
void from_json(const nlohmann::json& j, Quat& q);

void to_json(nlohmann::json& j, const LuzScene& s);
void from_json(const nlohmann::json& j, LuzScene& s);
void to_json(nlohmann::json& j, const LuzBoundary& b);
void from_json(const nlohmann::json& j, LuzBoundary& b);
void to_json(nlohmann::json& j, const LuzTransitionPoint& p);
void from_json(const nlohmann::json& j, LuzTransitionPoint& p);
void to_json(nlohmann::json& j, const LuzTransition& t);
void from_json(const nlohmann::json& j, LuzTransition& t);

void to_json(nlohmann::json& j, const LuzPlatformPathData& d);
void from_json(const nlohmann::json& j, LuzPlatformPathData& d);
void to_json(nlohmann::json& j, const LuzPropertyPathData& d);
void from_json(const nlohmann::json& j, LuzPropertyPathData& d);
void to_json(nlohmann::json& j, const LuzCameraPathData& d);
void from_json(const nlohmann::json& j, LuzCameraPathData& d);
void to_json(nlohmann::json& j, const LuzSpawnerPathData& d);
void from_json(const nlohmann::json& j, LuzSpawnerPathData& d);

void to_json(nlohmann::json& j, const LuzWaypoint& wp);
void from_json(const nlohmann::json& j, LuzWaypoint& wp);
void to_json(nlohmann::json& j, const LuzPath& p);
void from_json(const nlohmann::json& j, LuzPath& p);

void to_json(nlohmann::json& j, const LuzFile& f);
void from_json(const nlohmann::json& j, LuzFile& f);

} // namespace lu::assets
