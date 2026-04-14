#pragma once

#include "netdevil/common/ldf/ldf_types.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>

namespace lu::assets {

// LUZ (Zone) file parser — complete format including transition data and path chunks.
//
// All client files are version 41. The format is:
//   version u32  →  [file_revision u32]  →  world_id u32  →  spawn_pos/rot
//   →  scene_count  →  scenes[]  →  boundaries[]
//   →  raw_path / zone_name / zone_description strings
//   →  transition_data (version >= 32)
//   →  path_chunk_size u32 + path_chunk (version >= 35)
//
// RE sources (legouniverse.exe):
//   ReadLUZFile          @ 010438a0
//   ReadScenes           @ 01043400
//   ReadSceneAndLayer    @ 004b6d00
//   ReadZoneBoundaryLines@ 01018490
//   ReadTransitionData   @ 0102e0f0
//   ReadLUZPaths         @ 0108caa0
//   LevelPath::FromBuffer@ 0108b150
//
// References:
//   - lcdr/lu_formats luz.ksy (github.com/lcdr/lu_formats) — authoritative Kaitai Struct spec
//   - DarkflameServer (github.com/DarkflameUniverse/DarkflameServer) — Zone.cpp LoadPath()

struct LuzError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};

// ── Scenes ────────────────────────────────────────────────────────────────────

struct LuzScene {
    std::string filename;  // Relative path to .lvl file (backslashes normalized to /)
    uint32_t id = 0;
    uint32_t type = 0;     // layerId: 0=General, 1=Audio
    std::string name;
    // Revision 33 only: editor bounding sphere (removed in 34+, never in shipped files).
    // Identified via community RE'd SceneData::m_point + Ghidra RE of all 3 client builds.
    Vec3 scene_position = {0, 0, 0};
    float scene_radius = 0.0f;
    uint8_t color_r = 0, color_g = 0, color_b = 0;
};

// ── Boundaries ────────────────────────────────────────────────────────────────

// Zone boundary line for transitions between zones.
// Verified from Ghidra RE: ReadZoneBoundaryLines @ 01018490
struct LuzBoundary {
    Vec3 normal;
    Vec3 point;
    uint16_t dest_map_id = 0;
    uint16_t dest_instance_id = 0;
    uint32_t dest_clone_id = 0;
    uint32_t dest_scene_id = 0;
    Vec3 spawn_location;
};

// ── Transition Data (version >= 32) ───────────────────────────────────────────

// One end of a zone-to-zone transition portal.
// Verified from ReadTransitionData @ 0102e0f0.
struct LuzTransitionPoint {
    uint32_t scene_id = 0;
    uint32_t layer_id = 0;
    Vec3 position;
};

// A transition between two zones. Each transition has exactly:
//   2 points if version <= 33 or version >= 39  (all live client files: version 41 → 2 points)
//   5 points if version 34-38
struct LuzTransition {
    std::vector<LuzTransitionPoint> points;
};

// ── Path Chunk (version >= 35) ────────────────────────────────────────────────

// Key-value config entries attached to NPC/spawner/rail waypoints.
// LdfConfig is defined in ldf_types.h — raw string key-value pairs.

// Path type enum (ePathType). Values from lu_formats/luz.ksy and community RE'd.
enum class LuzPathType : uint32_t {
    NPC            = 0,  // NPC/creature movement paths
    MovingPlatform = 1,  // Moving platform paths (elevators, swinging bridges)
    Property       = 2,  // Player property zone boundaries
    Camera         = 3,  // Cinematic camera spline paths
    Spawner        = 4,  // Object spawner nodes
    Showcase       = 5,  // Buildarea/showcase zones
    Racing         = 6,  // Racing track waypoints
    Rail           = 7,  // Rail (Ninjago) paths
};

// Path behavior: what happens when the last waypoint is reached.
enum class LuzPathBehavior : uint32_t {
    Loop   = 0,
    Bounce = 1,
    Once   = 2,
};

// Per-path extra data for MovingPlatform paths.
// Verified from LevelPath::FromBuffer @ 0108b150.
struct LuzPlatformPathData {
    std::string traveling_audio_guid;  // path_version >= 13 and < 18 (GUID string)
    bool time_based_movement = false;  // path_version >= 18
};

// Per-path extra data for Property paths.
struct LuzPropertyPathData {
    uint32_t property_path_type = 0;    // 0=bounded, 1=entire_zone, 2=generated_rectangle
    uint32_t price = 0;
    uint32_t rental_time = 0;
    uint64_t associated_zone = 0;       // packed LWOOBJID: mapId(u16)|instanceId(u16)|cloneId
    std::string display_name;           // path_version >= 5, u1_wstr
    std::string display_desc;           // path_version >= 5, u4_wstr
    uint32_t property_type = 0;         // path_version >= 6: 0=premiere,1=prize,2=lup,3=headspace
    uint32_t clone_limit = 0;           // path_version >= 7
    float reputation_multiplier = 0;    // path_version >= 7
    uint32_t period_type = 0;           // path_version >= 7
    uint32_t achievement_required = 0;  // path_version >= 8
    Vec3 zone_position;                 // path_version >= 8
    float max_build_height = 0;         // path_version >= 8
};

// Per-path extra data for Camera paths.
struct LuzCameraPathData {
    std::string next_path;         // next camera path name (u1_wstr)
    bool rotate_player = false;    // path_version >= 14
};

// Per-path extra data for Spawner paths.
struct LuzSpawnerPathData {
    uint32_t spawned_lot = 0;
    uint32_t respawn_time = 0;
    int32_t  max_to_spawn = -1;    // -1 = infinite
    uint32_t num_to_maintain = 0;
    uint64_t spawner_object_id = 0;
    bool activate_on_load = false; // path_version >= 9
};

// A single waypoint on a path. Fields are populated based on the parent path's type.
// Check path_type to know which fields are valid.
struct LuzWaypoint {
    Vec3 position;

    // Platform, Camera, Spawner, Racing, Rail
    Quat rotation;

    // -- MovingPlatform only --
    bool  lock_player = false;
    float platform_speed = 0;
    float platform_wait = 0;
    std::string depart_audio_guid;   // path_version >= 13
    std::string arrive_audio_guid;   // path_version >= 13

    // -- Camera only --
    float camera_time = 0;
    float camera_fov = 0;
    float camera_tension = 0;
    float camera_continuity = 0;
    float camera_bias = 0;

    // -- Racing only --
    bool  is_reset_node = false;
    bool  is_non_horizontal_camera = false;
    float plane_width = 0;
    float plane_height = 0;
    float shortest_distance_to_end = 0;

    // -- Rail only --
    float rail_speed = 0;           // path_version > 16

    // -- NPC, Spawner, Rail: LDF key-value config --
    LdfConfig config;
};

// A single path in the path chunk.
struct LuzPath {
    uint32_t       path_version = 0;
    std::string    name;
    LuzPathType    path_type = LuzPathType::NPC;
    uint32_t       flags = 0;
    LuzPathBehavior behavior = LuzPathBehavior::Loop;

    // Type-specific path-level data (only the matching member is populated)
    LuzPlatformPathData  platform;
    LuzPropertyPathData  property;
    LuzCameraPathData    camera;
    LuzSpawnerPathData   spawner;

    std::vector<LuzWaypoint> waypoints;
};

// ── Top-level file ─────────────────────────────────────────────────────────────

struct LuzFile {
    uint32_t version = 0;
    uint32_t file_revision = 0;  // present when version > 35
    uint32_t world_id = 0;
    Vec3 spawn_position;
    Quat spawn_rotation;

    std::vector<LuzScene>    scenes;
    std::vector<LuzBoundary> boundaries;

    std::string raw_path;          // terrain heightmap file path (.raw)
    std::string zone_name;         // display name (version > 30)
    std::string zone_description;  // (version > 30)

    std::vector<LuzTransition> transitions;  // version >= 32
    std::vector<LuzPath>       paths;        // version >= 35
};
} // namespace lu::assets
