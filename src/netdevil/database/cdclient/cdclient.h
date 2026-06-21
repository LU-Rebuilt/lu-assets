#pragma once
// cdclient.h — CDClient database interface.
//
// Opens the game's CDClient database (either .sqlite or .fdb format).
// When given .fdb, auto-converts to SQLite first using fdb_to_sqlite_direct.
// Provides typed query methods for commonly-used tables, plus a generic
// query interface for any table.
//
// The live client CDClient has 137 tables. This class provides convenience
// methods for the most frequently queried tables and a generic row query
// for everything else.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace lu::assets {

// Generic row value — matches FDB/SQLite column types
using CdValue = std::variant<std::monostate, int32_t, int64_t, float, std::string, bool>;

// A single row from any table
using CdRow = std::vector<CdValue>;

// --- Typed result structs for common tables ---

struct CdComponentEntry {
    int32_t component_type;
    int32_t component_id;
};

struct CdRenderComponent {
    int32_t id;
    std::string render_asset;
    std::string icon_asset;
    int32_t icon_id;
    int32_t shader_id;
    int32_t effect1;
    int32_t effect2;
    int32_t effect3;
    int32_t effect4;
    int32_t effect5;
    int32_t effect6;
    int32_t animation_group_ids;
    std::string fade_asset;
    bool use_fade_in;
    bool preload_animations;
    float anim_speed;
    float fade_in_time;
};

struct CdPhysicsComponent {
    int32_t id;
    std::string physics_asset;
    float speed;
    float rot_speed;
    float jump_speed;
    float gravity_scale;
    int32_t pcshape_type;
    float player_height;
    float player_radius;
};

struct CdDestructibleComponent {
    int32_t id;
    int32_t faction;
    std::string faction_list;     // comma-separated, e.g. "1,4"
    int32_t life;
    int32_t imagination;
    int32_t loot_matrix_index;
    int32_t currency_index;
    int32_t level;
    float armor;
    int32_t death_behavior;
    bool is_npc;
    int32_t attack_priority;
    bool is_smashable;
    int32_t difficulty_level;
};

struct CdObjectInfo {
    int32_t id;
    std::string name;
    std::string type;
    std::string description;
    int32_t display_name;
    int32_t interactable;
    int32_t nametag;
    int32_t placeable;
    int32_t npc_template_id;
    float gate_version;
    int32_t hf_alternate_id;
};

struct CdZoneTableEntry {
    int32_t zone_id;
    int32_t zone_name;
    int32_t scene_id;
    int32_t ghost_distance_min;
    int32_t ghost_distance;
    int32_t population_soft_cap;
    int32_t population_hard_cap;
    std::string display_description;
    std::string map_folder;
    float smash_reward_min;
    float smash_reward_max;
    std::string gate_version;
    int32_t player_lose_coins_on_death;
    bool disable_saving_in_zone;
    int32_t world_type;
    bool mount_speed_limit;
    bool pets_not_allowed;
};

struct CdSceneTableEntry {
    int32_t scene_id;
    std::string scene_name;
};

struct CdSkillBehavior {
    int32_t skill_id;
    int32_t locked_skill_id;
    int32_t behavior_id;
    int32_t imagination_cost;
    float cooldown;
    int32_t cooldown_group;
    float charge_up_time;
    int32_t in_npc_editor;
    int32_t skill_icon;
    std::string oom_skill_id;
    int32_t oom_behavior_effect_id;
    float cast_type_desc;
    float im_bonus_ui;
    int32_t life_bonus_ui;
    int32_t armor_bonus_ui;
    float damage_ui;
    bool hide_icon;
    bool localize;
    std::string gate_version;
    int32_t cancel_type;
};

struct CdBehaviorTemplate {
    int32_t behavior_id;
    int32_t template_id;
    int32_t effect_id;
    std::string effect_handle;
};

struct CdMission {
    int32_t id;
    std::string defined_type;
    std::string defined_subtype;
    int32_t uid;
    int32_t offer_object_id;
    int32_t target_object_id;
    int32_t reward_currency;
    int32_t lego_score;
    int32_t reward_reputation;
    bool is_choice_reward;
    std::string reward_item1;
    int32_t reward_item1_count;
    int32_t reward_maxhealth;
    int32_t reward_maximagination;
    int32_t reward_maxinventory;
    bool is_mission;
    int32_t prerequisite_mission_id;
};

// --- Main CDClient class ---

class CdClient {
public:
    CdClient() = default;
    ~CdClient();
    CdClient(const CdClient&) = delete;
    CdClient& operator=(const CdClient&) = delete;

    // Open a .sqlite database directly.
    bool open(const std::filesystem::path& sqlite_path);

    // Open a .fdb file — auto-converts to SQLite at the given output path.
    // If output_path is empty, uses a temp file in the same directory.
    bool open_fdb(const std::filesystem::path& fdb_path,
                  const std::filesystem::path& output_path = {});

    // Open from a client root — tries CDServer.sqlite, cdclient.sqlite, cdclient.fdb
    bool open_from_client(const std::filesystem::path& client_root);

    void close();
    bool is_open() const { return db_ != nullptr; }

    // --- Generic query interface ---

    // Get all column names for a table
    std::vector<std::string> table_columns(const std::string& table) const;

    // Query rows with a WHERE clause. Returns all matching rows.
    // Example: query("Objects", "id = ?", {CdValue{int32_t(1)}})
    std::vector<CdRow> query(const std::string& table,
                             const std::string& where = "",
                             const std::vector<CdValue>& params = {}) const;

    // Iterate rows with a callback (avoids building the full result vector)
    void query_each(const std::string& table,
                    const std::string& where,
                    const std::vector<CdValue>& params,
                    const std::function<void(const CdRow&)>& callback) const;

    // Count rows in a table
    int64_t count(const std::string& table, const std::string& where = "") const;

    // --- Typed convenience queries ---

    // ComponentsRegistry: get all components for a LOT
    std::vector<CdComponentEntry> get_components(int32_t lot) const;

    // RenderComponent by component ID
    std::optional<CdRenderComponent> get_render_component(int32_t id) const;

    // PhysicsComponent by component ID
    std::optional<CdPhysicsComponent> get_physics_component(int32_t id) const;

    // DestructibleComponent by component ID (health/armor/imagination
    // baseline, faction, loot, NPC/smashable flags)
    std::optional<CdDestructibleComponent> get_destructible_component(int32_t id) const;

    // Objects table by LOT
    std::optional<CdObjectInfo> get_object(int32_t lot) const;

    // Get the render asset path for a LOT (resolves through ComponentsRegistry)
    std::optional<std::string> get_render_asset(int32_t lot) const;

    // ZoneTable by zone ID
    std::optional<CdZoneTableEntry> get_zone(int32_t zone_id) const;

    // SceneTable by scene ID
    std::optional<CdSceneTableEntry> get_scene(int32_t scene_id) const;

    // SkillBehavior by skill ID
    std::optional<CdSkillBehavior> get_skill(int32_t skill_id) const;

    // BehaviorTemplate by behavior ID
    std::optional<CdBehaviorTemplate> get_behavior_template(int32_t behavior_id) const;

    // Missions by ID
    std::optional<CdMission> get_mission(int32_t mission_id) const;

private:
    // Helper to read a text column safely
    static std::string get_text(sqlite3_stmt* stmt, int col);
    static int32_t get_int(sqlite3_stmt* stmt, int col);
    static float get_float(sqlite3_stmt* stmt, int col);

    sqlite3* db_ = nullptr;
};

} // namespace lu::assets
