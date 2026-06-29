#include "netdevil/zone/luz/luz_json.h"
#include "netdevil/common/ldf/ldf_json.h"

#include <cmath>
#include <cstring>
#include <limits>

using json = nlohmann::json;

static json float_to_json(float f) {
    if (std::isinf(f)) return f > 0 ? "inf" : "-inf";
    if (std::isnan(f)) {
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return "nan:0x" + ([](uint32_t v) {
            char buf[9];
            std::snprintf(buf, sizeof(buf), "%08x", v);
            return std::string(buf);
        })(bits);
    }
    return f;
}

static float float_from_json(const json& j) {
    if (j.is_string()) {
        auto s = j.get<std::string>();
        if (s == "inf")  return std::numeric_limits<float>::infinity();
        if (s == "-inf") return -std::numeric_limits<float>::infinity();
        if (s.substr(0, 6) == "nan:0x") {
            uint32_t bits = std::stoul(s.substr(4), nullptr, 16);
            float f;
            std::memcpy(&f, &bits, 4);
            return f;
        }
        if (s == "nan") return std::numeric_limits<float>::quiet_NaN();
    }
    if (j.is_null()) return std::numeric_limits<float>::quiet_NaN();
    return j.get<float>();
}

namespace lu::assets {

// ── Vec3 / Quat ──────────────────────────────────────────────────────────────

void to_json(json& j, const Vec3& v) {
    j = json{{"x", float_to_json(v.x)}, {"y", float_to_json(v.y)}, {"z", float_to_json(v.z)}};
}
void from_json(const json& j, Vec3& v) {
    v.x = float_from_json(j.at("x"));
    v.y = float_from_json(j.at("y"));
    v.z = float_from_json(j.at("z"));
}

void to_json(json& j, const Quat& q) {
    j = json{{"x", float_to_json(q.x)}, {"y", float_to_json(q.y)}, {"z", float_to_json(q.z)}, {"w", float_to_json(q.w)}};
}
void from_json(const json& j, Quat& q) {
    q.x = float_from_json(j.at("x"));
    q.y = float_from_json(j.at("y"));
    q.z = float_from_json(j.at("z"));
    q.w = float_from_json(j.at("w"));
}

// ── Scene ────────────────────────────────────────────────────────────────────

void to_json(json& j, const LuzScene& s) {
    j = json{
        {"filename", s.filename},
        {"id", s.id},
        {"type", s.type},
        {"name", s.name},
        {"scene_position", s.scene_position},
        {"scene_radius", float_to_json(s.scene_radius)},
        {"color_r", s.color_r},
        {"color_g", s.color_g},
        {"color_b", s.color_b},
    };
}
void from_json(const json& j, LuzScene& s) {
    j.at("filename").get_to(s.filename);
    s.id = j.value("id", 0u);
    s.type = j.value("type", 0u);
    s.name = j.value("name", "");
    s.scene_position = j.value("scene_position", Vec3{});
    s.scene_radius = j.contains("scene_radius") ? float_from_json(j.at("scene_radius")) : 0.0f;
    s.color_r = j.value("color_r", uint8_t(0));
    s.color_g = j.value("color_g", uint8_t(0));
    s.color_b = j.value("color_b", uint8_t(0));
}

// ── Boundary ─────────────────────────────────────────────────────────────────

void to_json(json& j, const LuzBoundary& b) {
    j = json{
        {"normal", b.normal},
        {"point", b.point},
        {"dest_map_id", b.dest_map_id},
        {"dest_instance_id", b.dest_instance_id},
        {"dest_clone_id", b.dest_clone_id},
        {"dest_scene_id", b.dest_scene_id},
        {"spawn_location", b.spawn_location},
    };
}
void from_json(const json& j, LuzBoundary& b) {
    j.at("normal").get_to(b.normal);
    j.at("point").get_to(b.point);
    j.at("dest_map_id").get_to(b.dest_map_id);
    j.at("dest_instance_id").get_to(b.dest_instance_id);
    b.dest_clone_id = j.value("dest_clone_id", 0u);
    j.at("dest_scene_id").get_to(b.dest_scene_id);
    j.at("spawn_location").get_to(b.spawn_location);
}

// ── Transitions ──────────────────────────────────────────────────────────────

void to_json(json& j, const LuzTransitionPoint& p) {
    j = json{
        {"scene_id", p.scene_id},
        {"layer_id", p.layer_id},
        {"position", p.position},
    };
}
void from_json(const json& j, LuzTransitionPoint& p) {
    j.at("scene_id").get_to(p.scene_id);
    j.at("layer_id").get_to(p.layer_id);
    j.at("position").get_to(p.position);
}

void to_json(json& j, const LuzTransition& t) {
    j = json{{"name", t.name}, {"width", t.width}, {"points", t.points}};
}
void from_json(const json& j, LuzTransition& t) {
    t.name = j.value("name", "");
    t.width = j.value("width", 0.0f);
    j.at("points").get_to(t.points);
}

// ── Path data ────────────────────────────────────────────────────────────────

void to_json(json& j, const LuzPlatformPathData& d) {
    j = json{
        {"traveling_audio_guid", d.traveling_audio_guid},
        {"time_based_movement", d.time_based_movement},
    };
}
void from_json(const json& j, LuzPlatformPathData& d) {
    d.traveling_audio_guid = j.value("traveling_audio_guid", "");
    d.time_based_movement = j.value("time_based_movement", false);
}

void to_json(json& j, const LuzPropertyPathData& d) {
    j = json{
        {"property_path_type", d.property_path_type},
        {"price", d.price},
        {"rental_time", d.rental_time},
        {"associated_zone", d.associated_zone},
        {"display_name", d.display_name},
        {"display_desc", d.display_desc},
        {"property_type", d.property_type},
        {"clone_limit", d.clone_limit},
        {"reputation_multiplier", d.reputation_multiplier},
        {"period_type", d.period_type},
        {"achievement_required", d.achievement_required},
        {"zone_position", d.zone_position},
        {"max_build_height", d.max_build_height},
    };
}
void from_json(const json& j, LuzPropertyPathData& d) {
    d.property_path_type = j.value("property_path_type", 0u);
    d.price = j.value("price", 0u);
    d.rental_time = j.value("rental_time", 0u);
    d.associated_zone = j.value("associated_zone", uint64_t(0));
    d.display_name = j.value("display_name", "");
    d.display_desc = j.value("display_desc", "");
    d.property_type = j.value("property_type", 0u);
    d.clone_limit = j.value("clone_limit", 0u);
    d.reputation_multiplier = j.value("reputation_multiplier", 0.0f);
    d.period_type = j.value("period_type", 0u);
    d.achievement_required = j.value("achievement_required", 0u);
    d.zone_position = j.value("zone_position", Vec3{});
    d.max_build_height = j.value("max_build_height", 0.0f);
}

void to_json(json& j, const LuzCameraPathData& d) {
    j = json{
        {"next_path", d.next_path},
        {"rotate_player", d.rotate_player},
    };
}
void from_json(const json& j, LuzCameraPathData& d) {
    d.next_path = j.value("next_path", "");
    d.rotate_player = j.value("rotate_player", false);
}

void to_json(json& j, const LuzSpawnerPathData& d) {
    j = json{
        {"spawned_lot", d.spawned_lot},
        {"respawn_time", d.respawn_time},
        {"max_to_spawn", d.max_to_spawn},
        {"num_to_maintain", d.num_to_maintain},
        {"spawner_object_id", d.spawner_object_id},
        {"activate_on_load", d.activate_on_load},
    };
}
void from_json(const json& j, LuzSpawnerPathData& d) {
    d.spawned_lot = j.value("spawned_lot", 0u);
    d.respawn_time = j.value("respawn_time", 0u);
    d.max_to_spawn = j.value("max_to_spawn", -1);
    d.num_to_maintain = j.value("num_to_maintain", 0u);
    d.spawner_object_id = j.value("spawner_object_id", uint64_t(0));
    d.activate_on_load = j.value("activate_on_load", false);
}

// ── Waypoint ─────────────────────────────────────────────────────────────────

void to_json(json& j, const LuzWaypoint& wp) {
    j = json{
        {"position", wp.position},
        {"rotation", wp.rotation},
        {"lock_player", wp.lock_player},
        {"platform_speed", wp.platform_speed},
        {"platform_wait", wp.platform_wait},
        {"depart_audio_guid", wp.depart_audio_guid},
        {"arrive_audio_guid", wp.arrive_audio_guid},
        {"camera_time", wp.camera_time},
        {"camera_fov", wp.camera_fov},
        {"camera_tension", wp.camera_tension},
        {"camera_continuity", wp.camera_continuity},
        {"camera_bias", wp.camera_bias},
        {"is_reset_node", wp.is_reset_node},
        {"is_non_horizontal_camera", wp.is_non_horizontal_camera},
        {"plane_width", wp.plane_width},
        {"plane_height", wp.plane_height},
        {"shortest_distance_to_end", wp.shortest_distance_to_end},
        {"rail_speed", wp.rail_speed},
        {"config", ldf_config_to_json(wp.config)},
    };
}

void from_json(const json& j, LuzWaypoint& wp) {
    j.at("position").get_to(wp.position);
    wp.rotation = j.value("rotation", Quat{});
    wp.lock_player = j.value("lock_player", false);
    wp.platform_speed = j.value("platform_speed", 0.0f);
    wp.platform_wait = j.value("platform_wait", 0.0f);
    wp.depart_audio_guid = j.value("depart_audio_guid", "");
    wp.arrive_audio_guid = j.value("arrive_audio_guid", "");
    wp.camera_time = j.value("camera_time", 0.0f);
    wp.camera_fov = j.value("camera_fov", 0.0f);
    wp.camera_tension = j.value("camera_tension", 0.0f);
    wp.camera_continuity = j.value("camera_continuity", 0.0f);
    wp.camera_bias = j.value("camera_bias", 0.0f);
    wp.is_reset_node = j.value("is_reset_node", false);
    wp.is_non_horizontal_camera = j.value("is_non_horizontal_camera", false);
    wp.plane_width = j.value("plane_width", 0.0f);
    wp.plane_height = j.value("plane_height", 0.0f);
    wp.shortest_distance_to_end = j.value("shortest_distance_to_end", 0.0f);
    wp.rail_speed = j.value("rail_speed", 0.0f);
    if (j.contains("config")) wp.config = ldf_config_from_json(j.at("config"));
}

// ── Path ─────────────────────────────────────────────────────────────────────

static const char* path_type_str(LuzPathType t) {
    switch (t) {
    case LuzPathType::NPC:            return "NPC";
    case LuzPathType::MovingPlatform: return "MovingPlatform";
    case LuzPathType::Property:       return "Property";
    case LuzPathType::Camera:         return "Camera";
    case LuzPathType::Spawner:        return "Spawner";
    case LuzPathType::Showcase:       return "Showcase";
    case LuzPathType::Racing:         return "Racing";
    case LuzPathType::Rail:           return "Rail";
    }
    return "NPC";
}

static LuzPathType path_type_from_str(const std::string& s) {
    if (s == "NPC")            return LuzPathType::NPC;
    if (s == "MovingPlatform") return LuzPathType::MovingPlatform;
    if (s == "Property")       return LuzPathType::Property;
    if (s == "Camera")         return LuzPathType::Camera;
    if (s == "Spawner")        return LuzPathType::Spawner;
    if (s == "Showcase")       return LuzPathType::Showcase;
    if (s == "Racing")         return LuzPathType::Racing;
    if (s == "Rail")           return LuzPathType::Rail;
    return LuzPathType::NPC;
}

static const char* path_behavior_str(LuzPathBehavior b) {
    switch (b) {
    case LuzPathBehavior::Loop:   return "Loop";
    case LuzPathBehavior::Bounce: return "Bounce";
    case LuzPathBehavior::Once:   return "Once";
    }
    return "Loop";
}

static LuzPathBehavior path_behavior_from_str(const std::string& s) {
    if (s == "Loop")   return LuzPathBehavior::Loop;
    if (s == "Bounce") return LuzPathBehavior::Bounce;
    if (s == "Once")   return LuzPathBehavior::Once;
    return LuzPathBehavior::Loop;
}

void to_json(json& j, const LuzPath& p) {
    j = json{
        {"path_version", p.path_version},
        {"name", p.name},
        {"type_name", p.type_name},
        {"path_type", path_type_str(p.path_type)},
        {"flags", p.flags},
        {"behavior", path_behavior_str(p.behavior)},
        {"platform", p.platform},
        {"property", p.property},
        {"camera", p.camera},
        {"spawner", p.spawner},
        {"waypoints", p.waypoints},
    };
}

void from_json(const json& j, LuzPath& p) {
    j.at("path_version").get_to(p.path_version);
    j.at("name").get_to(p.name);
    p.type_name = j.value("type_name", "");
    p.path_type = path_type_from_str(j.at("path_type").get<std::string>());
    p.flags = j.value("flags", 0u);
    p.behavior = path_behavior_from_str(j.value("behavior", "Loop"));
    if (j.contains("platform")) j.at("platform").get_to(p.platform);
    if (j.contains("property")) j.at("property").get_to(p.property);
    if (j.contains("camera"))   j.at("camera").get_to(p.camera);
    if (j.contains("spawner"))  j.at("spawner").get_to(p.spawner);
    j.at("waypoints").get_to(p.waypoints);
}

// ── Top-level LuzFile ────────────────────────────────────────────────────────

void to_json(json& j, const LuzFile& f) {
    j = json{
        {"version", f.version},
        {"file_revision", f.file_revision},
        {"world_id", f.world_id},
        {"spawn_position", f.spawn_position},
        {"spawn_rotation", f.spawn_rotation},
        {"scenes", f.scenes},
        {"boundaries", f.boundaries},
        {"raw_path", f.raw_path},
        {"zone_name", f.zone_name},
        {"zone_description", f.zone_description},
        {"transitions", f.transitions},
        {"path_chunk_version", f.path_chunk_version},
        {"paths", f.paths},
    };
}

void from_json(const json& j, LuzFile& f) {
    j.at("version").get_to(f.version);
    f.file_revision = j.value("file_revision", 0u);
    j.at("world_id").get_to(f.world_id);
    f.spawn_position = j.value("spawn_position", Vec3{});
    f.spawn_rotation = j.value("spawn_rotation", Quat{});
    j.at("scenes").get_to(f.scenes);
    f.boundaries = j.value("boundaries", std::vector<LuzBoundary>{});
    f.raw_path = j.value("raw_path", "");
    f.zone_name = j.value("zone_name", "");
    f.zone_description = j.value("zone_description", "");
    f.transitions = j.value("transitions", std::vector<LuzTransition>{});
    f.path_chunk_version = j.value("path_chunk_version", 0u);
    f.paths = j.value("paths", std::vector<LuzPath>{});
}

} // namespace lu::assets
