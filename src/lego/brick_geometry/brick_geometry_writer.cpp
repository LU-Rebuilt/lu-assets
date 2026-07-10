#include "lego/brick_geometry/brick_geometry_writer.h"
#include "common/binary_writer/binary_writer.h"

#include <cmath>

namespace lu::assets {

namespace {

Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }

Vec3 normalized(const Vec3& v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-12f) return {0.0f, 0.0f, 0.0f};
    return {v.x / len, v.y / len, v.z / len};
}

} // namespace

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

BrickGeometry brick_geometry_from_mesh(const std::vector<Vec3>& positions,
                                        const std::vector<Vec3>& normals,
                                        const std::vector<std::pair<float, float>>& uvs,
                                        const std::vector<uint32_t>& indices) {
    BrickGeometry geom;
    geom.has_uvs = true;
    geom.has_normals = true;
    geom.options = 0x01 | 0x02;
    geom.indices = indices;

    std::vector<Vec3> computed_normals;
    const std::vector<Vec3>* final_normals = &normals;
    if (normals.empty()) {
        computed_normals.assign(positions.size(), Vec3{0.0f, 0.0f, 0.0f});
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            uint32_t ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
            Vec3 face_normal = cross(sub(positions[ib], positions[ia]),
                                      sub(positions[ic], positions[ia]));
            computed_normals[ia] = add(computed_normals[ia], face_normal);
            computed_normals[ib] = add(computed_normals[ib], face_normal);
            computed_normals[ic] = add(computed_normals[ic], face_normal);
        }
        for (auto& n : computed_normals) n = normalized(n);
        final_normals = &computed_normals;
    }

    geom.vertices.resize(positions.size());
    for (size_t i = 0; i < positions.size(); i++) {
        geom.vertices[i].position = positions[i];
        geom.vertices[i].normal = (*final_normals)[i];
        if (i < uvs.size()) {
            geom.vertices[i].u = uvs[i].first;
            geom.vertices[i].v = uvs[i].second;
        }
    }

    return geom;
}

} // namespace lu::assets
