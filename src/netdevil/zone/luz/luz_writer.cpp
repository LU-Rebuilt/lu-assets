#include "netdevil/zone/luz/luz_writer.h"
#include "netdevil/common/ldf/ldf_writer.h"
#include "common/binary_writer/binary_writer.h"

#include <algorithm>

namespace lu::assets {

static void write_vec3(BinaryWriter& w, const Vec3& v) {
    w.write_f32(v.x);
    w.write_f32(v.y);
    w.write_f32(v.z);
}

static void write_quat_wxyz(BinaryWriter& w, const Quat& q) {
    w.write_f32(q.w);
    w.write_f32(q.x);
    w.write_f32(q.y);
    w.write_f32(q.z);
}

std::vector<uint8_t> luz_write(const LuzFile& luz) {
    BinaryWriter w;

    uint32_t revision = luz.version;
    if (revision < 20) revision = 20;

    w.write_u32(luz.version);

    if (revision > 35) {
        w.write_u32(luz.file_revision);
    }

    w.write_u32(luz.world_id);

    if (revision > 37) {
        write_vec3(w, luz.spawn_position);
        write_quat_wxyz(w, luz.spawn_rotation);
    }

    if (revision < 37) {
        w.write_u8(static_cast<uint8_t>(luz.scenes.size()));
    } else {
        w.write_u32(static_cast<uint32_t>(luz.scenes.size()));
    }

    for (auto& scene : luz.scenes) {
        std::string fn = scene.filename;
        std::replace(fn.begin(), fn.end(), '/', '\\');
        w.write_string8(fn);

        if (revision >= 33 || revision < 30) {
            w.write_u32(scene.id);
        }

        if (revision >= 33) {
            w.write_u32(scene.type);
            w.write_string8(scene.name);

            if (revision == 33) {
                w.write_f32(scene.scene_position.x);
                w.write_f32(scene.scene_position.y);
                w.write_f32(scene.scene_position.z);
                w.write_f32(scene.scene_radius);
            }

            w.write_u8(scene.color_r);
            w.write_u8(scene.color_b);
            w.write_u8(scene.color_g);
        }
    }

    w.write_u8(static_cast<uint8_t>(luz.boundaries.size()));
    for (auto& b : luz.boundaries) {
        write_vec3(w, b.normal);
        write_vec3(w, b.point);
        uint32_t packed = static_cast<uint32_t>(b.dest_map_id) |
                          (static_cast<uint32_t>(b.dest_instance_id) << 16);
        w.write_u32(packed);
        w.write_u32(b.dest_scene_id);
        write_vec3(w, b.spawn_location);
    }

    w.write_string8(luz.raw_path);
    if (revision > 30) {
        w.write_string8(luz.zone_name);
        w.write_string8(luz.zone_description);
    } else if (luz.has_zone_name) {
        // version == 30 exception (see LuzFile::has_zone_name).
        w.write_string8(luz.zone_name);
    }

    if (revision >= 32) {
        w.write_u32(static_cast<uint32_t>(luz.transitions.size()));
        for (auto& trans : luz.transitions) {
            if (revision < 40) {
                w.write_string8(trans.name);
                w.write_f32(trans.width);
            }
            for (auto& pt : trans.points) {
                w.write_u32(pt.scene_id);
                w.write_u32(pt.layer_id);
                write_vec3(w, pt.position);
            }
        }
    }

    if (revision >= 35) {
        size_t chunk_size_pos = w.pos();
        w.write_u32(0);

        size_t chunk_start = w.pos();
        w.write_u32(luz.path_chunk_version);
        w.write_u32(static_cast<uint32_t>(luz.paths.size()));

        for (auto& path : luz.paths) {
            w.write_u32(path.path_version);
            w.write_wstr8(path.name);

            if (path.path_version <= 2) {
                w.write_wstr8(path.type_name);
                w.write_u32(static_cast<uint32_t>(path.path_type));
                w.write_u32(path.flags);

                w.write_u32(static_cast<uint32_t>(path.waypoints.size()));
                for (auto& wp : path.waypoints) {
                    write_vec3(w, wp.position);
                    write_quat_wxyz(w, wp.rotation);
                    w.write_bool(wp.lock_player);
                    w.write_f32(wp.platform_speed);
                    w.write_f32(wp.platform_wait);
                    ldf_write_binary(w, wp.config);
                }
                continue;
            }

            w.write_u32(static_cast<uint32_t>(path.path_type));
            w.write_u32(path.flags);
            w.write_u32(static_cast<uint32_t>(path.behavior));

            switch (path.path_type) {
            case LuzPathType::MovingPlatform:
                if (path.path_version >= 18) {
                    w.write_bool(path.platform.time_based_movement);
                } else if (path.path_version >= 13) {
                    w.write_wstr8(path.platform.traveling_audio_guid);
                }
                break;

            case LuzPathType::Property:
                w.write_u32(path.property.property_path_type);
                w.write_u32(path.property.price);
                w.write_u32(path.property.rental_time);
                w.write_u64(path.property.associated_zone);
                if (path.path_version >= 5) {
                    w.write_wstr8(path.property.display_name);
                    w.write_wstr32(path.property.display_desc);
                }
                if (path.path_version >= 6) w.write_u32(path.property.property_type);
                if (path.path_version >= 7) {
                    w.write_u32(path.property.clone_limit);
                    w.write_f32(path.property.reputation_multiplier);
                    w.write_u32(path.property.period_type);
                }
                if (path.path_version >= 8) {
                    w.write_u32(path.property.achievement_required);
                    write_vec3(w, path.property.zone_position);
                    w.write_f32(path.property.max_build_height);
                }
                break;

            case LuzPathType::Camera:
                w.write_wstr8(path.camera.next_path);
                if (path.path_version >= 14) {
                    w.write_bool(path.camera.rotate_player);
                }
                break;

            case LuzPathType::Spawner:
                w.write_u32(path.spawner.spawned_lot);
                w.write_u32(path.spawner.respawn_time);
                w.write_s32(path.spawner.max_to_spawn);
                w.write_u32(path.spawner.num_to_maintain);
                w.write_u64(path.spawner.spawner_object_id);
                if (path.path_version >= 9) {
                    w.write_bool(path.spawner.activate_on_load);
                }
                break;

            default:
                break;
            }

            w.write_u32(static_cast<uint32_t>(path.waypoints.size()));
            for (auto& wp : path.waypoints) {
                write_vec3(w, wp.position);

                switch (path.path_type) {
                case LuzPathType::NPC:
                    ldf_write_binary(w, wp.config);
                    break;

                case LuzPathType::MovingPlatform:
                    write_quat_wxyz(w, wp.rotation);
                    w.write_bool(wp.lock_player);
                    w.write_f32(wp.platform_speed);
                    w.write_f32(wp.platform_wait);
                    if (path.path_version >= 13) {
                        w.write_wstr8(wp.depart_audio_guid);
                        w.write_wstr8(wp.arrive_audio_guid);
                    }
                    break;

                case LuzPathType::Camera:
                    write_quat_wxyz(w, wp.rotation);
                    w.write_f32(wp.camera_time);
                    w.write_f32(wp.camera_fov);
                    w.write_f32(wp.camera_tension);
                    w.write_f32(wp.camera_continuity);
                    w.write_f32(wp.camera_bias);
                    break;

                case LuzPathType::Spawner:
                    write_quat_wxyz(w, wp.rotation);
                    ldf_write_binary(w, wp.config);
                    break;

                case LuzPathType::Racing:
                    write_quat_wxyz(w, wp.rotation);
                    w.write_bool(wp.is_reset_node);
                    w.write_bool(wp.is_non_horizontal_camera);
                    w.write_f32(wp.plane_width);
                    w.write_f32(wp.plane_height);
                    w.write_f32(wp.shortest_distance_to_end);
                    break;

                case LuzPathType::Rail:
                    write_quat_wxyz(w, wp.rotation);
                    if (path.path_version > 16) {
                        w.write_f32(wp.rail_speed);
                    }
                    ldf_write_binary(w, wp.config);
                    break;

                default:
                    break;
                }
            }
        }

        uint32_t chunk_size = static_cast<uint32_t>(w.pos() - chunk_start);
        w.patch_u32(chunk_size_pos, chunk_size);
    }

    return w.data();
}

} // namespace lu::assets
