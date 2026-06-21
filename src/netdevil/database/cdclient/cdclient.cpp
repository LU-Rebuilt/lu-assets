// cdclient.cpp — CDClient database interface.
// Pure C++20 + SQLite3 — fully cross-platform.

#include "netdevil/database/cdclient/cdclient.h"
#include "netdevil/database/fdb/fdb_reader.h"
#include <sqlite3.h>
#include <fstream>
#include <iostream>

namespace lu::assets {

namespace fs = std::filesystem;

CdClient::~CdClient() {
    close();
}

std::string CdClient::get_text(sqlite3_stmt* stmt, int col) {
    auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
    return text ? text : "";
}

int32_t CdClient::get_int(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_int(stmt, col);
}

float CdClient::get_float(sqlite3_stmt* stmt, int col) {
    return static_cast<float>(sqlite3_column_double(stmt, col));
}

bool CdClient::open(const fs::path& sqlite_path) {
    close();
    int rc = sqlite3_open_v2(sqlite_path.string().c_str(), &db_,
                              SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) {
        if (db_) { sqlite3_close(db_); db_ = nullptr; }
        return false;
    }
    return true;
}

bool CdClient::open_fdb(const fs::path& fdb_path, const fs::path& output_path) {
    close();

    // Read the FDB file
    std::ifstream file(fdb_path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    auto size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    // Determine output path for the SQLite conversion
    fs::path out = output_path;
    if (out.empty()) {
        out = fdb_path;
        out.replace_extension(".sqlite");
    }

    // Convert FDB → SQLite
    try {
        fdb_to_sqlite_direct(data, out.string());
    } catch (const std::exception&) {
        return false;
    }

    // Open the resulting SQLite database
    return open(out);
}

bool CdClient::open_from_client(const fs::path& client_root) {
    // Try SQLite files first (preferred — no conversion needed)
    std::vector<fs::path> sqlite_paths = {
        client_root / "res" / "CDServer.sqlite",
        client_root / "res" / "cdclient.sqlite",
        client_root / "CDServer.sqlite",
        client_root / "cdclient.sqlite",
    };
    for (const auto& p : sqlite_paths) {
        if (fs::exists(p) && open(p)) return true;
    }

    // Try FDB files (auto-convert to SQLite in memory)
    std::vector<fs::path> fdb_paths = {
        client_root / "res" / "cdclient.fdb",
        client_root / "cdclient.fdb",
        client_root / "res" / "ivantest.fdb",
        client_root / "ivantest.fdb",
    };
    for (const auto& p : fdb_paths) {
        if (fs::exists(p)) {
            // Convert to SQLite next to the FDB
            fs::path out = p;
            out.replace_extension(".converted.sqlite");
            if (open_fdb(p, out)) return true;
        }
    }

    return false;
}

void CdClient::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

// --- Generic query interface ---

std::vector<std::string> CdClient::table_columns(const std::string& table) const {
    std::vector<std::string> cols;
    if (!db_) return cols;

    std::string sql = "PRAGMA table_info(" + table + ")";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return cols;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        cols.push_back(get_text(stmt, 1)); // column 1 = name
    }
    sqlite3_finalize(stmt);
    return cols;
}

std::vector<CdRow> CdClient::query(const std::string& table,
                                     const std::string& where,
                                     const std::vector<CdValue>& params) const {
    std::vector<CdRow> rows;
    query_each(table, where, params, [&](const CdRow& row) {
        rows.push_back(row);
    });
    return rows;
}

void CdClient::query_each(const std::string& table,
                            const std::string& where,
                            const std::vector<CdValue>& params,
                            const std::function<void(const CdRow&)>& callback) const {
    if (!db_) return;

    std::string sql = "SELECT * FROM " + table;
    if (!where.empty()) {
        sql += " WHERE " + where;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return;

    // Bind parameters
    for (size_t i = 0; i < params.size(); i++) {
        int idx = static_cast<int>(i + 1);
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                sqlite3_bind_null(stmt, idx);
            } else if constexpr (std::is_same_v<T, int32_t>) {
                sqlite3_bind_int(stmt, idx, val);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                sqlite3_bind_int64(stmt, idx, val);
            } else if constexpr (std::is_same_v<T, float>) {
                sqlite3_bind_double(stmt, idx, val);
            } else if constexpr (std::is_same_v<T, std::string>) {
                sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
            } else if constexpr (std::is_same_v<T, bool>) {
                sqlite3_bind_int(stmt, idx, val ? 1 : 0);
            }
        }, params[i]);
    }

    int col_count = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CdRow row;
        row.reserve(col_count);
        for (int c = 0; c < col_count; c++) {
            int type = sqlite3_column_type(stmt, c);
            switch (type) {
                case SQLITE_INTEGER:
                    row.emplace_back(static_cast<int32_t>(sqlite3_column_int(stmt, c)));
                    break;
                case SQLITE_FLOAT:
                    row.emplace_back(static_cast<float>(sqlite3_column_double(stmt, c)));
                    break;
                case SQLITE_TEXT:
                    row.emplace_back(get_text(stmt, c));
                    break;
                case SQLITE_NULL:
                default:
                    row.emplace_back(std::monostate{});
                    break;
            }
        }
        callback(row);
    }
    sqlite3_finalize(stmt);
}

int64_t CdClient::count(const std::string& table, const std::string& where) const {
    if (!db_) return 0;

    std::string sql = "SELECT COUNT(*) FROM " + table;
    if (!where.empty()) sql += " WHERE " + where;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return 0;

    int64_t result = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return result;
}

// --- Typed convenience queries ---

std::vector<CdComponentEntry> CdClient::get_components(int32_t lot) const {
    std::vector<CdComponentEntry> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT component_type, component_id FROM ComponentsRegistry WHERE id = ?",
        -1, &stmt, nullptr) != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, lot);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        result.push_back({get_int(stmt, 0), get_int(stmt, 1)});
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<CdRenderComponent> CdClient::get_render_component(int32_t id) const {
    if (!db_) return std::nullopt;

    // Column names match the actual cdclient.sqlite schema (FDB-converted)
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT id, render_asset, icon_asset, IconID, shader_id, "
        "effect1, effect2, effect3, effect4, effect5, effect6, "
        "animationGroupIDs, fade, usedropshadow, preloadAnimations, "
        "fadeInTime FROM RenderComponent WHERE id = ?",
        -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);
    std::optional<CdRenderComponent> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        CdRenderComponent rc;
        rc.id = get_int(stmt, 0);
        rc.render_asset = get_text(stmt, 1);
        rc.icon_asset = get_text(stmt, 2);
        rc.icon_id = get_int(stmt, 3);
        rc.shader_id = get_int(stmt, 4);
        rc.effect1 = get_int(stmt, 5);
        rc.effect2 = get_int(stmt, 6);
        rc.effect3 = get_int(stmt, 7);
        rc.effect4 = get_int(stmt, 8);
        rc.effect5 = get_int(stmt, 9);
        rc.effect6 = get_int(stmt, 10);
        rc.animation_group_ids = get_int(stmt, 11);
        rc.fade_asset = "";
        rc.use_fade_in = get_int(stmt, 12) != 0;
        rc.preload_animations = get_int(stmt, 14) != 0;
        rc.anim_speed = 1.0f;
        rc.fade_in_time = get_float(stmt, 15);
        result = rc;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<CdPhysicsComponent> CdClient::get_physics_component(int32_t id) const {
    if (!db_) return std::nullopt;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT id, physics_asset, speed, rotSpeed, jumpSpeed, gravityScale, "
        "pcShapeType, playerHeight, playerRadius FROM PhysicsComponent WHERE id = ?",
        -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);
    std::optional<CdPhysicsComponent> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        CdPhysicsComponent pc;
        pc.id = get_int(stmt, 0);
        pc.physics_asset = get_text(stmt, 1);
        pc.speed = get_float(stmt, 2);
        pc.rot_speed = get_float(stmt, 3);
        pc.jump_speed = get_float(stmt, 4);
        pc.gravity_scale = get_float(stmt, 5);
        pc.pcshape_type = get_int(stmt, 6);
        pc.player_height = get_float(stmt, 7);
        pc.player_radius = get_float(stmt, 8);
        result = pc;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<CdDestructibleComponent> CdClient::get_destructible_component(int32_t id) const {
    if (!db_) return std::nullopt;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT id, faction, factionList, life, imagination, "
        "LootMatrixIndex, CurrencyIndex, level, armor, death_behavior, "
        "isnpc, attack_priority, isSmashable, difficultyLevel "
        "FROM DestructibleComponent WHERE id = ?",
        -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, id);
    std::optional<CdDestructibleComponent> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        CdDestructibleComponent dc;
        dc.id                 = get_int(stmt, 0);
        dc.faction            = get_int(stmt, 1);
        dc.faction_list       = get_text(stmt, 2);
        dc.life               = get_int(stmt, 3);
        dc.imagination        = get_int(stmt, 4);
        dc.loot_matrix_index  = get_int(stmt, 5);
        dc.currency_index     = get_int(stmt, 6);
        dc.level              = get_int(stmt, 7);
        dc.armor              = get_float(stmt, 8);
        dc.death_behavior     = get_int(stmt, 9);
        dc.is_npc             = get_int(stmt, 10) != 0;
        dc.attack_priority    = get_int(stmt, 11);
        dc.is_smashable       = get_int(stmt, 12) != 0;
        dc.difficulty_level   = get_int(stmt, 13);
        result = dc;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<CdObjectInfo> CdClient::get_object(int32_t lot) const {
    if (!db_) return std::nullopt;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT id, name, type, description, displayName, interactable, "
        "nametag, _internalNotes, npcTemplateID, gate_version, HF_Alternate_NIF_ID "
        "FROM Objects WHERE id = ?",
        -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, lot);
    std::optional<CdObjectInfo> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        CdObjectInfo obj;
        obj.id = get_int(stmt, 0);
        obj.name = get_text(stmt, 1);
        obj.type = get_text(stmt, 2);
        obj.description = get_text(stmt, 3);
        obj.display_name = get_int(stmt, 4);
        obj.interactable = get_int(stmt, 5);
        obj.nametag = get_int(stmt, 6);
        obj.placeable = 0; // column may not exist in all versions
        obj.npc_template_id = get_int(stmt, 8);
        obj.gate_version = get_float(stmt, 9);
        obj.hf_alternate_id = get_int(stmt, 10);
        result = obj;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<std::string> CdClient::get_render_asset(int32_t lot) const {
    // ComponentsRegistry component_type 2 = RenderComponent
    auto comps = get_components(lot);
    for (const auto& c : comps) {
        if (c.component_type == 2) {
            auto rc = get_render_component(c.component_id);
            if (rc && !rc->render_asset.empty()) return rc->render_asset;
        }
    }
    return std::nullopt;
}

std::optional<CdZoneTableEntry> CdClient::get_zone(int32_t zone_id) const {
    if (!db_) return std::nullopt;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT zoneID, zoneName, scriptID, ghostdistance_min, ghostdistance, "
        "population_soft_cap, population_hard_cap, DisplayDescription, mapFolder, "
        "smashableMinDistance, smashableMaxDistance, gate_version, "
        "playerLoseCoinsOnDeath, disableSaveLoc, worldType, mountsAllowed, petsAllowed "
        "FROM ZoneTable WHERE zoneID = ?",
        -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, zone_id);
    std::optional<CdZoneTableEntry> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        CdZoneTableEntry z;
        z.zone_id = get_int(stmt, 0);
        z.zone_name = get_int(stmt, 1);
        z.scene_id = get_int(stmt, 2);
        z.ghost_distance_min = get_int(stmt, 3);
        z.ghost_distance = get_int(stmt, 4);
        z.population_soft_cap = get_int(stmt, 5);
        z.population_hard_cap = get_int(stmt, 6);
        z.display_description = get_text(stmt, 7);
        z.map_folder = get_text(stmt, 8);
        z.smash_reward_min = get_float(stmt, 9);
        z.smash_reward_max = get_float(stmt, 10);
        z.gate_version = get_text(stmt, 11);
        z.player_lose_coins_on_death = get_int(stmt, 12);
        z.disable_saving_in_zone = get_int(stmt, 13) != 0;
        z.world_type = get_int(stmt, 14);
        z.mount_speed_limit = get_int(stmt, 15) != 0;
        z.pets_not_allowed = get_int(stmt, 16) != 0;
        result = z;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<CdSceneTableEntry> CdClient::get_scene(int32_t scene_id) const {
    if (!db_) return std::nullopt;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT sceneID, sceneName FROM SceneTable WHERE sceneID = ?",
        -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, scene_id);
    std::optional<CdSceneTableEntry> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = CdSceneTableEntry{get_int(stmt, 0), get_text(stmt, 1)};
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<CdSkillBehavior> CdClient::get_skill(int32_t skill_id) const {
    if (!db_) return std::nullopt;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT skillID, locStatus, behaviorID, imaginationcost, cooldowntime, "
        "cooldowngroup, inNpcEditor, skillIcon, oomSkillID, oomBehaviorEffectID, "
        "castTypeDesc, imBonusUI, lifeBonusUI, armorBonusUI, damageUI, "
        "hideIcon, localize, gate_version, cancelType "
        "FROM SkillBehavior WHERE skillID = ?",
        -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, skill_id);
    std::optional<CdSkillBehavior> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        CdSkillBehavior sb;
        sb.skill_id = get_int(stmt, 0);
        sb.locked_skill_id = get_int(stmt, 1);
        sb.behavior_id = get_int(stmt, 2);
        sb.imagination_cost = get_int(stmt, 3);
        sb.cooldown = get_float(stmt, 4);
        sb.cooldown_group = get_int(stmt, 5);
        sb.in_npc_editor = get_int(stmt, 6);
        sb.skill_icon = get_int(stmt, 7);
        sb.oom_skill_id = get_text(stmt, 8);
        sb.oom_behavior_effect_id = get_int(stmt, 9);
        sb.cast_type_desc = get_float(stmt, 10);
        sb.im_bonus_ui = get_float(stmt, 11);
        sb.life_bonus_ui = get_int(stmt, 12);
        sb.armor_bonus_ui = get_int(stmt, 13);
        sb.damage_ui = get_float(stmt, 14);
        sb.hide_icon = get_int(stmt, 15) != 0;
        sb.localize = get_int(stmt, 16) != 0;
        sb.gate_version = get_text(stmt, 17);
        sb.cancel_type = get_int(stmt, 18);
        result = sb;
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<CdBehaviorTemplate> CdClient::get_behavior_template(int32_t behavior_id) const {
    if (!db_) return std::nullopt;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT behaviorID, templateID, effectID, effectHandle "
        "FROM BehaviorTemplateName WHERE behaviorID = ?",
        -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, behavior_id);
    std::optional<CdBehaviorTemplate> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = CdBehaviorTemplate{
            get_int(stmt, 0), get_int(stmt, 1), get_int(stmt, 2), get_text(stmt, 3)
        };
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<CdMission> CdClient::get_mission(int32_t mission_id) const {
    if (!db_) return std::nullopt;

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_,
        "SELECT id, defined_type, defined_subtype, UISortOrder, "
        "offer_objectID, target_objectID, reward_currency, LegoScore, "
        "reward_reputation, isChoiceReward, reward_item1, reward_item1_count, "
        "reward_maxhealth, reward_maximagination, reward_maxinventory, "
        "isMission, prereqMissionID "
        "FROM Missions WHERE id = ?",
        -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;

    sqlite3_bind_int(stmt, 1, mission_id);
    std::optional<CdMission> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        CdMission m;
        m.id = get_int(stmt, 0);
        m.defined_type = get_text(stmt, 1);
        m.defined_subtype = get_text(stmt, 2);
        m.uid = get_int(stmt, 3);
        m.offer_object_id = get_int(stmt, 4);
        m.target_object_id = get_int(stmt, 5);
        m.reward_currency = get_int(stmt, 6);
        m.lego_score = get_int(stmt, 7);
        m.reward_reputation = get_int(stmt, 8);
        m.is_choice_reward = get_int(stmt, 9) != 0;
        m.reward_item1 = get_text(stmt, 10);
        m.reward_item1_count = get_int(stmt, 11);
        m.reward_maxhealth = get_int(stmt, 12);
        m.reward_maximagination = get_int(stmt, 13);
        m.reward_maxinventory = get_int(stmt, 14);
        m.is_mission = get_int(stmt, 15) != 0;
        m.prerequisite_mission_id = get_int(stmt, 16);
        result = m;
    }
    sqlite3_finalize(stmt);
    return result;
}

} // namespace lu::assets
