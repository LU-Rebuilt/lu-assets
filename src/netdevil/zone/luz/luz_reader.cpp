#include "netdevil/zone/luz/luz_reader.h"
#include "netdevil/common/ldf/ldf_reader.h"
#include "common/binary_reader/binary_reader.h"

#include <algorithm>

namespace lu::assets {

// ── Local helpers ─────────────────────────────────────────────────────────────

static Vec3 read_vec3(BinaryReader& r) {
    Vec3 v;
    v.x = r.read_f32();
    v.y = r.read_f32();
    v.z = r.read_f32();
    return v;
}

static Quat read_quat_wxyz(BinaryReader& r) {
    Quat q;
    q.w = r.read_f32();
    q.x = r.read_f32();
    q.y = r.read_f32();
    q.z = r.read_f32();
    return q;
}

// u1_wstr: u8 char_count + UTF-16LE chars. Converts to ASCII (replaces non-ASCII with '?').
// Used in path names, camera next_path, audio GUIDs, LDF keys/values.
// Verified: lu_formats/luz.ksy common::u1_wstr.
static std::string read_wstr8(BinaryReader& r) {
    uint8_t len = r.read_u8();
    std::string result;
    result.reserve(len);
    for (uint8_t i = 0; i < len; ++i) {
        uint16_t wc = r.read_u16();
        result += static_cast<char>(wc < 128 ? wc : '?');
    }
    return result;
}

// u4_wstr: u32 char_count + UTF-16LE chars. Used in property display_desc.
static std::string read_wstr32(BinaryReader& r) {
    uint32_t len = r.read_u32();
    std::string result;
    result.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
        uint16_t wc = r.read_u16();
        result += static_cast<char>(wc < 128 ? wc : '?');
    }
    return result;
}

// LDF config parsing delegated to ldf_parse_binary() in ldf_reader.

// ── Main parser ───────────────────────────────────────────────────────────────

LuzFile luz_parse(std::span<const uint8_t> data) {
    BinaryReader r(data);
    LuzFile luz;

    // ReadLUZFile @ 010438a0
    luz.version = r.read_u32();

    uint32_t revision = luz.version;
    if (revision < 20) revision = 20;

    if (revision > 35) {
        luz.file_revision = r.read_u32();
    }

    luz.world_id = r.read_u32();

    // Spawn point (revision > 37)
    if (revision > 37) {
        luz.spawn_position = read_vec3(r);
        // ReadQuaternionW: W first, then X, Y, Z
        luz.spawn_rotation = read_quat_wxyz(r);
    }

    // Scene count: < 37 uses u8, >= 37 uses u32
    uint32_t scene_count;
    if (revision < 37) {
        scene_count = r.read_u8();
    } else {
        scene_count = r.read_u32();
    }

    // ReadScenes @ 01043400, verified against DarkflameServer Zone::LoadScene.
    //
    // Field order for ALL versions (per DLS Zone::LoadScene):
    //   filename (u8 str, always)
    //   scene_id (u32, if version >= 33 OR version < 30)
    //   sceneType/layerId (u32, if version >= 33)
    //   name (u8 str, if version >= 33)
    //   unknown1 vec3 + unknown2 f32 (if version == 33 only)
    //   color_r, color_b, color_g (u8 each, if version >= 33)
    //
    // Note: the original C++ had id+layerId before filename for v30-40, which was
    // WRONG. The field order is identical for all versions >= 33 (including v41).
    luz.scenes.reserve(scene_count);
    for (uint32_t i = 0; i < scene_count; ++i) {
        if (r.remaining() < 2) break;
        LuzScene scene;

        scene.filename = r.read_string8();
        std::replace(scene.filename.begin(), scene.filename.end(), '\\', '/');

        if (revision >= 33 || revision < 30) {
            scene.id = r.read_u32();
        }

        if (revision >= 33) {
            scene.type = r.read_u32();
            scene.name = r.read_string8();

            if (revision == 33) {
                // Revision 33 only: editor-only scene bounding data, removed in revision 34+.
                // These fields are read and discarded by all known client builds (legouniverse.exe
                // @ 010437af-010437d3, older client build @ 0085ef3d).
                // They are not stored in the runtime LUZScene/astruct_24 struct (56 bytes: sceneId,
                // layerId, sceneFileName, sceneDisplayName — no Vec3/float members).
                //
                // Identification based on:
                //   - community RE'd SceneData struct has m_point (NiPoint3) at the corresponding
                //     position, used for editor scene visualization/navigation
                //   - Vec3 + f32 is the classic bounding sphere pattern (center + radius)
                //   - These sit between scene display name and scene color (all editor UI fields)
                //   - No shipped LUZ file uses revision 33; all v41 client files skip this path
                scene.scene_position.x = r.read_f32();
                scene.scene_position.y = r.read_f32();
                scene.scene_position.z = r.read_f32();
                scene.scene_radius = r.read_f32();
            }

            scene.color_r = r.read_u8();
            scene.color_b = r.read_u8();  // Note: byte order in file is R,B,G (not R,G,B)
            scene.color_g = r.read_u8();
        }

        luz.scenes.push_back(std::move(scene));
    }

    // ReadZoneBoundaryLines @ 01018490
    if (r.remaining() >= 1) {
        uint8_t num_boundaries = r.read_u8();
        luz.boundaries.reserve(num_boundaries);
        for (uint8_t b = 0; b < num_boundaries; ++b) {
            if (r.remaining() < 44) break;
            LuzBoundary boundary;
            boundary.normal        = read_vec3(r);
            boundary.point         = read_vec3(r);
            uint32_t packed        = r.read_u32();
            boundary.dest_map_id      = static_cast<uint16_t>(packed & 0xFFFF);
            boundary.dest_instance_id = static_cast<uint16_t>((packed >> 16) & 0xFFFF);
            boundary.dest_clone_id    = 0;
            boundary.dest_scene_id = r.read_u32();
            boundary.spawn_location = read_vec3(r);
            luz.boundaries.push_back(boundary);
        }
    }

    // Zone info strings — always present after boundary lines
    if (r.remaining() >= 1) {
        luz.raw_path = r.read_string8();
    }
    if (revision > 30 && r.remaining() >= 1) {
        luz.zone_name = r.read_string8();
        if (r.remaining() >= 1) {
            luz.zone_description = r.read_string8();
        }
    }

    // ── Transition Data (version >= 32) ────────────────────────────────────────
    //
    // ReadTransitionData @ 0102e0f0.
    // Each transition connects two zones. Point count per transition:
    //   2 points if version <= 33 or version >= 39 (all live client files: version 41 → 2 points)
    //   5 points if version 34–38
    // For version < 40: each transition is preceded by a u1_str + f32 (purpose unclear).
    if (revision >= 32 && r.remaining() >= 4) {
        uint32_t num_trans = r.read_u32();
        luz.transitions.reserve(num_trans);

        uint32_t pts_per_trans = (revision <= 33 || revision >= 39) ? 2u : 5u;

        for (uint32_t i = 0; i < num_trans; ++i) {
            LuzTransition trans;

            if (revision < 40) {
                r.read_string8();  // unknown_string (u1_str)
                r.read_f32();      // unknown_float (width or similar)
            }

            trans.points.resize(pts_per_trans);
            for (uint32_t j = 0; j < pts_per_trans; ++j) {
                trans.points[j].scene_id = r.read_u32();
                trans.points[j].layer_id = r.read_u32();
                trans.points[j].position = read_vec3(r);
            }

            luz.transitions.push_back(std::move(trans));
        }
    }

    // ── Path Chunk (version >= 35) ─────────────────────────────────────────────
    //
    // ReadLUZPaths @ 0108caa0, LevelPath::FromBuffer @ 0108b150.
    // Prefixed by u32 chunk_size (byte count of everything that follows).
    // Contains: u32 chunk_version (rejected if >= 2) + u32 num_paths + paths[].
    if (revision >= 35 && r.remaining() >= 4) {
        uint32_t chunk_size  = r.read_u32();
        size_t   chunk_start = r.pos();

        if (r.remaining() >= chunk_size && chunk_size >= 8) {
            uint32_t chunk_version = r.read_u32();
            uint32_t num_paths     = r.read_u32();
            luz.paths.reserve(num_paths);

            for (uint32_t i = 0; i < num_paths; ++i) {
                LuzPath path;
                path.path_version = r.read_u32();
                path.name         = read_wstr8(r);

                // Legacy: version <= 2 has a type_name string that overrides the type enum
                if (path.path_version <= 2) {
                    read_wstr8(r); // type_name (ignored — all live files use path_version >= 12)
                }

                path.path_type = static_cast<LuzPathType>(r.read_u32());
                path.flags     = r.read_u32();
                path.behavior  = static_cast<LuzPathBehavior>(r.read_u32());

                // ── Path-level type-specific data ─────────────────────────────
                switch (path.path_type) {

                case LuzPathType::MovingPlatform:
                    // Verified: LevelPath::FromBuffer, condition (pathVersion >= 13 and < 18)
                    if (path.path_version >= 18) {
                        path.platform.time_based_movement = r.read_bool();
                    } else if (path.path_version >= 13) {
                        path.platform.traveling_audio_guid = read_wstr8(r);
                    }
                    break;

                case LuzPathType::Property:
                    path.property.property_path_type = r.read_u32();
                    path.property.price              = r.read_u32();
                    path.property.rental_time        = r.read_u32();
                    path.property.associated_zone    = r.read_u64();
                    if (path.path_version >= 5) {
                        path.property.display_name = read_wstr8(r);  // u1_wstr
                        path.property.display_desc = read_wstr32(r); // u4_wstr
                    }
                    if (path.path_version >= 6) path.property.property_type        = r.read_u32();
                    if (path.path_version >= 7) {
                        path.property.clone_limit            = r.read_u32();
                        path.property.reputation_multiplier  = r.read_f32();
                        path.property.period_type            = r.read_u32();
                    }
                    if (path.path_version >= 8) {
                        path.property.achievement_required = r.read_u32();
                        path.property.zone_position        = read_vec3(r);
                        path.property.max_build_height     = r.read_f32();
                    }
                    break;

                case LuzPathType::Camera:
                    path.camera.next_path = read_wstr8(r);
                    if (path.path_version >= 14) {
                        path.camera.rotate_player = r.read_bool();
                    }
                    break;

                case LuzPathType::Spawner:
                    path.spawner.spawned_lot        = r.read_u32();
                    path.spawner.respawn_time       = r.read_u32();
                    path.spawner.max_to_spawn       = r.read_s32();
                    path.spawner.num_to_maintain    = r.read_u32();
                    path.spawner.spawner_object_id  = r.read_u64();
                    if (path.path_version >= 9) {
                        path.spawner.activate_on_load = r.read_bool();
                    }
                    break;

                default: // NPC, Showcase, Racing, Rail: no path-level extra data
                    break;
                }

                // ── Waypoints ─────────────────────────────────────────────────
                uint32_t num_waypoints = r.read_u32();
                path.waypoints.reserve(num_waypoints);

                for (uint32_t j = 0; j < num_waypoints; ++j) {
                    LuzWaypoint wp;
                    wp.position = read_vec3(r);

                    switch (path.path_type) {

                    case LuzPathType::NPC:
                        wp.config = ldf_parse_binary(r);
                        break;

                    case LuzPathType::MovingPlatform:
                        wp.rotation      = read_quat_wxyz(r);
                        wp.lock_player   = r.read_bool();
                        wp.platform_speed = r.read_f32();
                        wp.platform_wait  = r.read_f32();
                        if (path.path_version >= 13) {
                            wp.depart_audio_guid = read_wstr8(r);
                            wp.arrive_audio_guid = read_wstr8(r);
                        }
                        break;

                    case LuzPathType::Camera:
                        wp.rotation          = read_quat_wxyz(r);
                        wp.camera_time       = r.read_f32();
                        wp.camera_fov        = r.read_f32();
                        wp.camera_tension    = r.read_f32();
                        wp.camera_continuity = r.read_f32();
                        wp.camera_bias       = r.read_f32();
                        break;

                    case LuzPathType::Spawner:
                        wp.rotation = read_quat_wxyz(r);
                        wp.config   = ldf_parse_binary(r);
                        break;

                    case LuzPathType::Racing:
                        wp.rotation                    = read_quat_wxyz(r);
                        wp.is_reset_node               = r.read_bool();
                        wp.is_non_horizontal_camera    = r.read_bool();
                        wp.plane_width                 = r.read_f32();
                        wp.plane_height                = r.read_f32();
                        wp.shortest_distance_to_end    = r.read_f32();
                        break;

                    case LuzPathType::Rail:
                        wp.rotation = read_quat_wxyz(r);
                        if (path.path_version > 16) {
                            wp.rail_speed = r.read_f32();
                        }
                        wp.config = ldf_parse_binary(r);
                        break;

                    default: // Property, Showcase: position only
                        break;
                    }

                    path.waypoints.push_back(std::move(wp));
                }

                luz.paths.push_back(std::move(path));
            }
        }
    }

    return luz;
}

} // namespace lu::assets
