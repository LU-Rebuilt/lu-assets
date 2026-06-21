#include "netdevil/zone/lvl/lvl_json.h"
#include "netdevil/common/ldf/ldf_json.h"
#include "netdevil/common/ldf/ldf_reader.h"

using json = nlohmann::json;

namespace lu::assets {

// ── Cull / Draw ──────────────────────────────────────────────────────────────

void to_json(json& j, const LvlCullVal& v) {
    j = json{{"group_id", v.group_id}, {"min", v.min}, {"max", v.max}};
}
void from_json(const json& j, LvlCullVal& v) {
    j.at("group_id").get_to(v.group_id);
    j.at("min").get_to(v.min);
    j.at("max").get_to(v.max);
}

void to_json(json& j, const LvlDrawDistances& d) {
    j = json{
        {"fog_near", d.fog_near}, {"fog_far", d.fog_far},
        {"post_fog_solid", d.post_fog_solid}, {"post_fog_fade", d.post_fog_fade},
        {"static_obj_distance", d.static_obj_distance},
        {"dynamic_obj_distance", d.dynamic_obj_distance},
    };
}
void from_json(const json& j, LvlDrawDistances& d) {
    d.fog_near = j.value("fog_near", 0.0f);
    d.fog_far = j.value("fog_far", 0.0f);
    d.post_fog_solid = j.value("post_fog_solid", 0.0f);
    d.post_fog_fade = j.value("post_fog_fade", 0.0f);
    d.static_obj_distance = j.value("static_obj_distance", 0.0f);
    d.dynamic_obj_distance = j.value("dynamic_obj_distance", 0.0f);
}

// ── Lighting ─────────────────────────────────────────────────────────────────

void to_json(json& j, const LvlLightingInfo& l) {
    j = json{
        {"blend_time", l.blend_time},
        {"ambient", json::array({l.ambient[0], l.ambient[1], l.ambient[2]})},
        {"specular", json::array({l.specular[0], l.specular[1], l.specular[2]})},
        {"upper_hemi", json::array({l.upper_hemi[0], l.upper_hemi[1], l.upper_hemi[2]})},
        {"position", l.position},
        {"has_draw_distances", l.has_draw_distances},
        {"min_draw", l.min_draw},
        {"max_draw", l.max_draw},
        {"cull_vals", l.cull_vals},
        {"fog_near", l.fog_near},
        {"fog_far", l.fog_far},
        {"fog_color", json::array({l.fog_color[0], l.fog_color[1], l.fog_color[2]})},
        {"dir_light", json::array({l.dir_light[0], l.dir_light[1], l.dir_light[2]})},
        {"start_position", l.start_position},
        {"start_rotation", l.start_rotation},
        {"has_spawn", l.has_spawn},
    };
}

static void get_float3(const json& j, const char* key, float out[3]) {
    if (j.contains(key) && j.at(key).is_array() && j.at(key).size() >= 3) {
        out[0] = j.at(key)[0].get<float>();
        out[1] = j.at(key)[1].get<float>();
        out[2] = j.at(key)[2].get<float>();
    }
}

void from_json(const json& j, LvlLightingInfo& l) {
    l.blend_time = j.value("blend_time", 0.0f);
    get_float3(j, "ambient", l.ambient);
    get_float3(j, "specular", l.specular);
    get_float3(j, "upper_hemi", l.upper_hemi);
    l.position = j.value("position", Vec3{});
    l.has_draw_distances = j.value("has_draw_distances", false);
    if (j.contains("min_draw")) j.at("min_draw").get_to(l.min_draw);
    if (j.contains("max_draw")) j.at("max_draw").get_to(l.max_draw);
    l.cull_vals = j.value("cull_vals", std::vector<LvlCullVal>{});
    l.fog_near = j.value("fog_near", 0.0f);
    l.fog_far = j.value("fog_far", 0.0f);
    get_float3(j, "fog_color", l.fog_color);
    get_float3(j, "dir_light", l.dir_light);
    l.start_position = j.value("start_position", Vec3{});
    l.start_rotation = j.value("start_rotation", Quat{});
    l.has_spawn = j.value("has_spawn", false);
}

// ── Skydome ──────────────────────────────────────────────────────────────────

void to_json(json& j, const LvlSkydomeInfo& s) {
    j = json{
        {"filename", s.filename},
        {"sky_layer_filename", s.sky_layer_filename},
        {"ring_layer", json::array({s.ring_layer[0], s.ring_layer[1],
                                    s.ring_layer[2], s.ring_layer[3]})},
    };
}
void from_json(const json& j, LvlSkydomeInfo& s) {
    s.filename = j.value("filename", "");
    s.sky_layer_filename = j.value("sky_layer_filename", "");
    if (j.contains("ring_layer") && j.at("ring_layer").is_array()) {
        auto& arr = j.at("ring_layer");
        for (size_t i = 0; i < 4 && i < arr.size(); ++i)
            s.ring_layer[i] = arr[i].get<std::string>();
    }
}

// ── Editor ───────────────────────────────────────────────────────────────────

void to_json(json& j, const LvlEditorColor& c) {
    j = json{{"r", c.r}, {"g", c.g}, {"b", c.b}};
}
void from_json(const json& j, LvlEditorColor& c) {
    j.at("r").get_to(c.r);
    j.at("g").get_to(c.g);
    j.at("b").get_to(c.b);
}

void to_json(json& j, const LvlEditorSettings& e) {
    j = json{{"saved_colors", e.saved_colors}};
}
void from_json(const json& j, LvlEditorSettings& e) {
    e.saved_colors = j.value("saved_colors", std::vector<LvlEditorColor>{});
}

// ── Environment ──────────────────────────────────────────────────────────────

void to_json(json& j, const LvlEnvironmentData& e) {
    j = json{
        {"lighting", e.lighting},
        {"skydome", e.skydome},
        {"editor", e.editor},
        {"has_editor", e.has_editor},
    };
}
void from_json(const json& j, LvlEnvironmentData& e) {
    if (j.contains("lighting")) j.at("lighting").get_to(e.lighting);
    if (j.contains("skydome"))  j.at("skydome").get_to(e.skydome);
    if (j.contains("editor"))   j.at("editor").get_to(e.editor);
    e.has_editor = j.value("has_editor", false);
}

// ── Render ───────────────────────────────────────────────────────────────────

void to_json(json& j, const LvlRenderAttr& a) {
    j = json{
        {"name", a.name},
        {"num_floats", a.num_floats},
        {"is_color", a.is_color},
        {"values", json::array({a.values[0], a.values[1], a.values[2], a.values[3]})},
    };
}
void from_json(const json& j, LvlRenderAttr& a) {
    j.at("name").get_to(a.name);
    a.num_floats = j.value("num_floats", 4u);
    a.is_color = j.value("is_color", false);
    if (j.contains("values") && j.at("values").is_array()) {
        auto& arr = j.at("values");
        for (size_t i = 0; i < 4 && i < arr.size(); ++i)
            a.values[i] = arr[i].get<float>();
    }
}

void to_json(json& j, const LvlRenderTechnique& t) {
    j = json{{"name", t.name}, {"attrs", t.attrs}};
}
void from_json(const json& j, LvlRenderTechnique& t) {
    t.name = j.value("name", "");
    t.attrs = j.value("attrs", std::vector<LvlRenderAttr>{});
}

// LdfEntry to_json/from_json provided by netdevil/common/ldf/ldf_json.h

// ── Object ───────────────────────────────────────────────────────────────────

static const char* node_type_str(LvlNodeType t) {
    switch (t) {
    case LvlNodeType::EnvironmentObj:     return "EnvironmentObj";
    case LvlNodeType::Building:           return "Building";
    case LvlNodeType::Enemy:              return "Enemy";
    case LvlNodeType::NPC:                return "NPC";
    case LvlNodeType::Rebuilder:          return "Rebuilder";
    case LvlNodeType::Spawned:            return "Spawned";
    case LvlNodeType::Cannon:             return "Cannon";
    case LvlNodeType::Bouncer:            return "Bouncer";
    case LvlNodeType::Exhibit:            return "Exhibit";
    case LvlNodeType::MovingPlatform:     return "MovingPlatform";
    case LvlNodeType::Springpad:          return "Springpad";
    case LvlNodeType::Sound:              return "Sound";
    case LvlNodeType::Particle:           return "Particle";
    case LvlNodeType::GenericPlaceholder: return "GenericPlaceholder";
    case LvlNodeType::ErrorMarker:        return "ErrorMarker";
    case LvlNodeType::PlayerStart:        return "PlayerStart";
    }
    return "Building";
}

static LvlNodeType node_type_from_str(const std::string& s) {
    if (s == "EnvironmentObj")     return LvlNodeType::EnvironmentObj;
    if (s == "Building")           return LvlNodeType::Building;
    if (s == "Enemy")              return LvlNodeType::Enemy;
    if (s == "NPC")                return LvlNodeType::NPC;
    if (s == "Rebuilder")          return LvlNodeType::Rebuilder;
    if (s == "Spawned")            return LvlNodeType::Spawned;
    if (s == "Cannon")             return LvlNodeType::Cannon;
    if (s == "Bouncer")            return LvlNodeType::Bouncer;
    if (s == "Exhibit")            return LvlNodeType::Exhibit;
    if (s == "MovingPlatform")     return LvlNodeType::MovingPlatform;
    if (s == "Springpad")          return LvlNodeType::Springpad;
    if (s == "Sound")              return LvlNodeType::Sound;
    if (s == "Particle")           return LvlNodeType::Particle;
    if (s == "GenericPlaceholder") return LvlNodeType::GenericPlaceholder;
    if (s == "ErrorMarker")        return LvlNodeType::ErrorMarker;
    if (s == "PlayerStart")        return LvlNodeType::PlayerStart;
    return LvlNodeType::Building;
}

void to_json(json& j, const LvlObject& o) {
    j = json{
        {"object_id", o.object_id},
        {"lot", o.lot},
        {"node_type", node_type_str(o.node_type)},
        {"glom_id", o.glom_id},
        {"position", o.position},
        {"rotation", o.rotation},
        {"scale", o.scale},
        {"config", o.config},
        {"render_technique", o.render_technique},
    };
}
void from_json(const json& j, LvlObject& o) {
    j.at("object_id").get_to(o.object_id);
    j.at("lot").get_to(o.lot);
    o.node_type = node_type_from_str(j.value("node_type", "Building"));
    o.glom_id = j.value("glom_id", 0u);
    j.at("position").get_to(o.position);
    o.rotation = j.value("rotation", Quat{});
    o.scale = j.value("scale", 1.0f);
    o.config = j.value("config", std::vector<LdfEntry>{});
    if (j.contains("render_technique")) j.at("render_technique").get_to(o.render_technique);
}

// ── Particle ─────────────────────────────────────────────────────────────────

void to_json(json& j, const LvlParticle& p) {
    j = json{
        {"priority", p.priority},
        {"position", p.position},
        {"rotation", p.rotation},
        {"effect_names", p.effect_names},
        {"config", p.config},
    };
}
void from_json(const json& j, LvlParticle& p) {
    p.priority = j.value("priority", uint16_t(0));
    j.at("position").get_to(p.position);
    p.rotation = j.value("rotation", Quat{});
    p.effect_names = j.value("effect_names", "");
    p.config = j.value("config", std::vector<LdfEntry>{});
}

// ── Top-level LvlFile ────────────────────────────────────────────────────────

void to_json(json& j, const LvlFile& f) {
    j = json{
        {"version", f.version},
        {"revision", f.revision},
        {"has_environment", f.has_environment},
        {"environment", f.environment},
        {"objects", f.objects},
        {"particles", f.particles},
    };
}
void from_json(const json& j, LvlFile& f) {
    j.at("version").get_to(f.version);
    f.revision = j.value("revision", 0u);
    f.has_environment = j.value("has_environment", false);
    if (j.contains("environment")) j.at("environment").get_to(f.environment);
    f.objects = j.value("objects", std::vector<LvlObject>{});
    f.particles = j.value("particles", std::vector<LvlParticle>{});
}

} // namespace lu::assets
