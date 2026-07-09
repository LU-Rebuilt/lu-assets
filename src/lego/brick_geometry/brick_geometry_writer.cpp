#include "lego/brick_geometry/brick_geometry_writer.h"
#include "common/binary_writer/binary_writer.h"

namespace lu::assets {

std::vector<uint8_t> brick_geometry_write(const BrickGeometry& geom) {
    BinaryWriter w;

    w.write_u32(BRICK_GEOM_MAGIC);
    w.write_u32(static_cast<uint32_t>(geom.vertices.size()));
    w.write_u32(static_cast<uint32_t>(geom.indices.size()));
    w.write_u32(geom.options);

    for (const auto& v : geom.vertices) {
        w.write_f32(v.position.x);
        w.write_f32(v.position.y);
        w.write_f32(v.position.z);
    }
    if (geom.has_normals) {
        for (const auto& v : geom.vertices) {
            w.write_f32(v.normal.x);
            w.write_f32(v.normal.y);
            w.write_f32(v.normal.z);
        }
    }
    if (geom.has_uvs) {
        for (const auto& v : geom.vertices) {
            w.write_f32(v.u);
            w.write_f32(v.v);
        }
    }
    for (uint32_t idx : geom.indices) {
        w.write_u32(idx);
    }

    if (geom.has_skin_indices) {
        w.write_u32(static_cast<uint32_t>(geom.skin_indices.weight_group_indices.size()));
        for (uint32_t idx : geom.skin_indices.weight_group_indices) {
            w.write_u32(idx);
        }
        for (uint32_t idx : geom.skin_indices.index_mapping) {
            w.write_u32(idx);
        }
    }

    if (geom.has_skin_weights) {
        w.write_u32(static_cast<uint32_t>(geom.skin_weights.weights.size()));
        for (const auto& v : geom.skin_weights.weights) {
            w.write_f32(v.x);
            w.write_f32(v.y);
            w.write_f32(v.z);
        }
        for (uint32_t idx : geom.skin_weights.index_mapping) {
            w.write_u32(idx);
        }
    }

    if (geom.has_extra) {
        w.write_u32(static_cast<uint32_t>(geom.extra_data.size()));
        w.write_bytes(geom.extra_data.data(), geom.extra_data.size());
        for (uint32_t idx : geom.extra_remap) {
            w.write_u32(idx);
        }
    }

    if (geom.has_tag) {
        w.write_u32(geom.tag);
        w.write_u32(static_cast<uint32_t>(geom.tag_data.size()));
        w.write_bytes(geom.tag_data.data(), geom.tag_data.size());
    }

    return std::move(w.data());
}

} // namespace lu::assets
