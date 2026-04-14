#include <gtest/gtest.h>
#include "netdevil/zone/luz/luz_reader.h"

#include <cstring>
#include <vector>
#include <string>
#include <cstdint>

using namespace lu::assets;

namespace {

// Helper to write binary data
struct LuzBuilder {
    std::vector<uint8_t> data;

    void u8(uint8_t v) { data.push_back(v); }
    void u32(uint32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void u64(uint64_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 8);
    }
    void s32(int32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void f32(float v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
    // u8-prefixed ASCII string
    void str8(const std::string& s) {
        u8(static_cast<uint8_t>(s.size()));
        data.insert(data.end(), s.begin(), s.end());
    }
    // u8-prefixed UTF-16LE wide string (ASCII subset only)
    void wstr8(const std::string& s) {
        u8(static_cast<uint8_t>(s.size()));
        for (char c : s) { u8(static_cast<uint8_t>(c)); u8(0); }
    }
    // u32-prefixed UTF-16LE wide string
    void wstr32(const std::string& s) {
        u32(static_cast<uint32_t>(s.size()));
        for (char c : s) { u8(static_cast<uint8_t>(c)); u8(0); }
    }
    // LDF entry: wstr8 key + wstr8 value
    void ldf_entry(const std::string& key, const std::string& val) {
        wstr8(key); wstr8(val);
    }
    // Full LDF block: count + entries
    void ldf(std::initializer_list<std::pair<std::string,std::string>> entries) {
        u32(static_cast<uint32_t>(entries.size()));
        for (auto& [k,v] : entries) ldf_entry(k, v);
    }
    // Quaternion WXYZ
    void quat(float w, float x, float y, float z) {
        f32(w); f32(x); f32(y); f32(z);
    }
    // Vec3
    void vec3(float x, float y, float z) {
        f32(x); f32(y); f32(z);
    }
};

// Build a minimal valid v41 LUZ with two scenes, no boundaries, no transitions,
// and a single NPC path with two waypoints.
std::vector<uint8_t> build_v41_luz_with_paths() {
    LuzBuilder b;

    // Header
    b.u32(41);           // version
    b.u32(4884);         // fileRevision (> 35 → read separately)
    b.u32(1100);         // world_id

    // Spawn (revision > 37)
    b.vec3(10.0f, 20.0f, 30.0f);
    b.quat(1.0f, 0.0f, 0.0f, 0.0f);

    // Scenes (u32 count, revision >= 37)
    b.u32(2);
    // Scene 0: filename, sceneId, layerId, name, color
    b.str8("global.lvl"); b.u32(0); b.u32(0); b.str8("Global"); b.u8(255); b.u8(0); b.u8(0);
    // Scene 1
    b.str8("audio.lvl");  b.u32(1); b.u32(1); b.str8("Audio");  b.u8(0); b.u8(0); b.u8(0);

    // Boundaries: 0
    b.u8(0);

    // Zone strings
    b.str8("test.raw");
    b.str8("TestZone");
    b.str8("A zone for testing");

    // Transition data (v >= 32): 1 transition, 2 points each (v41 >= 39)
    b.u32(1);
    // point 0: sceneId(u32) + layerId(u32) + pos
    b.u32(0); b.u32(0); b.vec3(100.0f, 50.0f, 200.0f);
    // point 1
    b.u32(1); b.u32(0); b.vec3(-100.0f, 50.0f, -200.0f);

    // Path chunk (v >= 35)
    // We'll fill chunk_size after writing the chunk contents
    size_t chunk_size_pos = b.data.size();
    b.u32(0);     // chunk_size placeholder
    size_t chunk_start = b.data.size();

    b.u32(1);     // chunk_version
    b.u32(1);     // num_paths = 1

    // NPC path (path_type=0): path_version=12, name="TestPath", behavior=loop
    b.u32(12);                    // path_version
    b.wstr8("TestPath");          // path name (u8 wstr)
    b.u32(0);                     // path_type = NPC
    b.u32(0);                     // flags
    b.u32(0);                     // behavior = loop

    // No path-level data for NPC
    b.u32(2);                     // num_waypoints

    // Waypoint 0
    b.vec3(1.0f, 2.0f, 3.0f);
    b.ldf({{"speed", "3:5.5"}}); // 1 LDF entry

    // Waypoint 1
    b.vec3(4.0f, 5.0f, 6.0f);
    b.ldf({});                    // 0 LDF entries

    // Patch chunk_size
    uint32_t chunk_size = static_cast<uint32_t>(b.data.size() - chunk_start);
    std::memcpy(b.data.data() + chunk_size_pos, &chunk_size, 4);

    return b.data;
}

// Build a v41 LUZ with a Spawner path (to test spawner path-level data and waypoints)
std::vector<uint8_t> build_v41_luz_spawner_path() {
    LuzBuilder b;

    b.u32(41); b.u32(1); b.u32(1000);
    b.vec3(0,0,0); b.quat(1,0,0,0);
    b.u32(1);
    b.str8("s.lvl"); b.u32(0); b.u32(0); b.str8("S"); b.u8(0); b.u8(0); b.u8(0);
    b.u8(0);         // boundaries
    b.str8("s.raw"); b.str8("S"); b.str8("SD");

    // 0 transitions
    b.u32(0);

    size_t csp = b.data.size(); b.u32(0);
    size_t cs = b.data.size();

    b.u32(1);  // chunk_version
    b.u32(1);  // num_paths

    // Spawner path: path_version=12
    b.u32(12);
    b.wstr8("SpawnPath");
    b.u32(4);    // path_type = Spawner
    b.u32(0);    // flags
    b.u32(0);    // behavior

    // Spawner path-level data (path_version >= 4: spawned_lot, respawn_time, max, maintain, object_id, [activate_on_load v>=9])
    b.u32(1234);        // spawned_lot
    b.u32(10);          // respawn_time
    b.s32(-1);          // max_to_spawn = infinite
    b.u32(3);           // num_to_maintain
    b.u64(0xDEADBEEFCAFEBABEull); // spawner_object_id
    b.u8(1);            // activate_on_load (path_version >= 9)

    // 1 spawner waypoint
    b.u32(1);
    b.vec3(10.0f, 0.0f, -5.0f);
    b.quat(1.0f, 0.0f, 0.0f, 0.0f);
    b.ldf({{"spawner_node_id", "1:0"}}); // 1 LDF config entry

    uint32_t chunk_size = static_cast<uint32_t>(b.data.size() - cs);
    std::memcpy(b.data.data() + csp, &chunk_size, 4);

    return b.data;
}

// Build v40 LUZ (same scene format as v41 since both >= 33, but 2-pt transitions, no extra str/float)
std::vector<uint8_t> build_v40_luz() {
    LuzBuilder b;

    b.u32(40);           // version
    b.u32(1);            // fileRevision (> 35)
    b.u32(1200);         // world_id
    b.vec3(5,10,15);     // spawn pos (revision > 37)
    b.quat(1,0,0,0);     // spawn rot

    b.u32(1);            // scene_count (u32, revision >= 37)
    // Scene (v>=33: filename first)
    b.str8("level.lvl"); b.u32(42); b.u32(0); b.str8("TestScene"); b.u8(255); b.u8(128); b.u8(64);

    b.u8(0);             // boundaries

    b.str8("test.raw"); b.str8("Old Zone"); b.str8("Old description");

    // Transitions (v>=32): 0 transitions (v40 >= 39 → 2 pts per trans, no extra str/float)
    b.u32(0);

    // Path chunk (v>=35): 0 paths
    b.u32(8); b.u32(1); b.u32(0);  // chunk_size=8, version=1, paths=0

    return b.data;
}

} // anonymous namespace

// ── Header / scene / zone string tests ────────────────────────────────────────

TEST(LUZ, ParseVersion41Header) {
    auto data = build_v41_luz_with_paths();
    auto luz = luz_parse(data);

    EXPECT_EQ(luz.version, 41u);
    EXPECT_EQ(luz.file_revision, 4884u);
    EXPECT_EQ(luz.world_id, 1100u);
    EXPECT_FLOAT_EQ(luz.spawn_position.x, 10.0f);
    EXPECT_FLOAT_EQ(luz.spawn_position.y, 20.0f);
    EXPECT_FLOAT_EQ(luz.spawn_position.z, 30.0f);
    EXPECT_FLOAT_EQ(luz.spawn_rotation.w, 1.0f);
}

TEST(LUZ, ParseVersion41Scenes) {
    auto data = build_v41_luz_with_paths();
    auto luz = luz_parse(data);

    ASSERT_EQ(luz.scenes.size(), 2u);
    EXPECT_EQ(luz.scenes[0].filename, "global.lvl");
    EXPECT_EQ(luz.scenes[0].id, 0u);
    EXPECT_EQ(luz.scenes[0].type, 0u);
    EXPECT_EQ(luz.scenes[0].name, "Global");
    EXPECT_EQ(luz.scenes[1].filename, "audio.lvl");
    EXPECT_EQ(luz.scenes[1].id, 1u);
    EXPECT_EQ(luz.scenes[1].type, 1u);
    EXPECT_EQ(luz.scenes[1].name, "Audio");
}

TEST(LUZ, ParseVersion41ZoneStrings) {
    auto data = build_v41_luz_with_paths();
    auto luz = luz_parse(data);

    EXPECT_EQ(luz.raw_path, "test.raw");
    EXPECT_EQ(luz.zone_name, "TestZone");
    EXPECT_EQ(luz.zone_description, "A zone for testing");
}

// ── Transition data tests ──────────────────────────────────────────────────────

TEST(LUZ, ParseTransitions) {
    auto data = build_v41_luz_with_paths();
    auto luz = luz_parse(data);

    ASSERT_EQ(luz.transitions.size(), 1u);
    ASSERT_EQ(luz.transitions[0].points.size(), 2u);
    EXPECT_EQ(luz.transitions[0].points[0].scene_id, 0u);
    EXPECT_EQ(luz.transitions[0].points[0].layer_id, 0u);
    EXPECT_FLOAT_EQ(luz.transitions[0].points[0].position.x, 100.0f);
    EXPECT_FLOAT_EQ(luz.transitions[0].points[0].position.y, 50.0f);
    EXPECT_FLOAT_EQ(luz.transitions[0].points[1].scene_id, 1u);
    EXPECT_FLOAT_EQ(luz.transitions[0].points[1].position.x, -100.0f);
}

TEST(LUZ, ZeroTransitions) {
    auto data = build_v40_luz();
    auto luz = luz_parse(data);
    EXPECT_TRUE(luz.transitions.empty());
}

// ── NPC path tests ─────────────────────────────────────────────────────────────

TEST(LUZ, ParseNpcPath) {
    auto data = build_v41_luz_with_paths();
    auto luz = luz_parse(data);

    ASSERT_EQ(luz.paths.size(), 1u);
    const LuzPath& path = luz.paths[0];
    EXPECT_EQ(path.name, "TestPath");
    EXPECT_EQ(path.path_type, LuzPathType::NPC);
    EXPECT_EQ(path.behavior, LuzPathBehavior::Loop);
    EXPECT_EQ(path.path_version, 12u);
}

TEST(LUZ, ParseNpcWaypoints) {
    auto data = build_v41_luz_with_paths();
    auto luz = luz_parse(data);

    ASSERT_EQ(luz.paths.size(), 1u);
    const LuzPath& path = luz.paths[0];
    ASSERT_EQ(path.waypoints.size(), 2u);

    EXPECT_FLOAT_EQ(path.waypoints[0].position.x, 1.0f);
    EXPECT_FLOAT_EQ(path.waypoints[0].position.y, 2.0f);
    EXPECT_FLOAT_EQ(path.waypoints[0].position.z, 3.0f);
    ASSERT_EQ(path.waypoints[0].config.size(), 1u);
    EXPECT_EQ(path.waypoints[0].config[0].first, "speed");
    EXPECT_EQ(path.waypoints[0].config[0].second, "3:5.5");

    EXPECT_FLOAT_EQ(path.waypoints[1].position.x, 4.0f);
    EXPECT_TRUE(path.waypoints[1].config.empty());
}

// ── Spawner path tests ─────────────────────────────────────────────────────────

TEST(LUZ, ParseSpawnerPath) {
    auto data = build_v41_luz_spawner_path();
    auto luz = luz_parse(data);

    ASSERT_EQ(luz.paths.size(), 1u);
    const LuzPath& path = luz.paths[0];
    EXPECT_EQ(path.name, "SpawnPath");
    EXPECT_EQ(path.path_type, LuzPathType::Spawner);

    EXPECT_EQ(path.spawner.spawned_lot, 1234u);
    EXPECT_EQ(path.spawner.respawn_time, 10u);
    EXPECT_EQ(path.spawner.max_to_spawn, -1);
    EXPECT_EQ(path.spawner.num_to_maintain, 3u);
    EXPECT_EQ(path.spawner.spawner_object_id, 0xDEADBEEFCAFEBABEull);
    EXPECT_TRUE(path.spawner.activate_on_load);
}

TEST(LUZ, ParseSpawnerWaypoint) {
    auto data = build_v41_luz_spawner_path();
    auto luz = luz_parse(data);

    ASSERT_EQ(luz.paths.size(), 1u);
    ASSERT_EQ(luz.paths[0].waypoints.size(), 1u);
    const LuzWaypoint& wp = luz.paths[0].waypoints[0];
    EXPECT_FLOAT_EQ(wp.position.x, 10.0f);
    EXPECT_FLOAT_EQ(wp.position.z, -5.0f);
    EXPECT_FLOAT_EQ(wp.rotation.w, 1.0f);
    ASSERT_EQ(wp.config.size(), 1u);
    EXPECT_EQ(wp.config[0].first, "spawner_node_id");
}

// ── v40 parsing ────────────────────────────────────────────────────────────────

TEST(LUZ, ParseVersion40) {
    auto data = build_v40_luz();
    auto luz = luz_parse(data);

    EXPECT_EQ(luz.version, 40u);
    EXPECT_EQ(luz.world_id, 1200u);
    ASSERT_EQ(luz.scenes.size(), 1u);
    EXPECT_EQ(luz.scenes[0].filename, "level.lvl");
    EXPECT_EQ(luz.scenes[0].id, 42u);
    EXPECT_EQ(luz.scenes[0].name, "TestScene");
    EXPECT_EQ(luz.raw_path, "test.raw");
    EXPECT_EQ(luz.zone_name, "Old Zone");
    EXPECT_TRUE(luz.transitions.empty());
    EXPECT_TRUE(luz.paths.empty());
}

// ── Error cases ────────────────────────────────────────────────────────────────

TEST(LUZ, EmptyDataThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(luz_parse(empty), std::out_of_range);
}
