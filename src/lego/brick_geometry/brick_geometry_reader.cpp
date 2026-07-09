#include "lego/brick_geometry/brick_geometry_reader.h"
#include "common/binary_reader/binary_reader.h"
#include <string>

namespace lu::assets {

BrickGeometry brick_geometry_parse(std::span<const uint8_t> data) {
    BinaryReader r(data);

    uint32_t magic = r.read_u32();
    if (magic != BRICK_GEOM_MAGIC) {
        throw BrickGeometryError("BrickGeometry: invalid magic (expected 0x42473031, got 0x" +
                                  std::to_string(magic) + ")");
    }

    uint32_t vertex_count = r.read_u32();
    uint32_t index_count = r.read_u32();
    uint32_t options = r.read_u32();

    BrickGeometry geom;
    geom.options = options;
    geom.has_uvs          = (options & 0x01) != 0;
    geom.has_normals      = (options & 0x02) != 0;
    geom.has_extra        = (options & 0x04) != 0;
    geom.has_tag          = (options & 0x08) != 0;
    geom.has_skin_indices = (options & 0x10) != 0;
    geom.has_skin_weights = (options & 0x20) != 0;

    // Vertex positions (vertex_count * 3 floats) — always present.
    geom.vertices.resize(vertex_count);
    for (uint32_t i = 0; i < vertex_count; ++i) {
        geom.vertices[i].position.x = r.read_f32();
        geom.vertices[i].position.y = r.read_f32();
        geom.vertices[i].position.z = r.read_f32();
    }

    // Vertex normals (vertex_count * 3 floats) — gated by 0x02, independent of UVs.
    if (geom.has_normals) {
        for (uint32_t i = 0; i < vertex_count; ++i) {
            geom.vertices[i].normal.x = r.read_f32();
            geom.vertices[i].normal.y = r.read_f32();
            geom.vertices[i].normal.z = r.read_f32();
        }
    }

    // Texture coordinates (vertex_count * 2 floats) — gated by 0x01, independent of normals.
    if (geom.has_uvs) {
        for (uint32_t i = 0; i < vertex_count; ++i) {
            geom.vertices[i].u = r.read_f32();
            geom.vertices[i].v = r.read_f32();
        }
    }

    // Triangle indices (index_count u32 values) — always present.
    geom.indices.resize(index_count);
    for (uint32_t i = 0; i < index_count; ++i) {
        geom.indices[i] = r.read_u32();
    }

    // Skin weight index block (0x10): weight-group index list + per-index mapping.
    if (geom.has_skin_indices) {
        uint32_t count = r.read_u32();
        geom.skin_indices.weight_group_indices.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            geom.skin_indices.weight_group_indices[i] = r.read_u32();
        }
        geom.skin_indices.index_mapping.resize(index_count);
        for (uint32_t i = 0; i < index_count; ++i) {
            geom.skin_indices.index_mapping[i] = r.read_u32();
        }
    }

    // Skin weight block (0x20): Vec3-valued weight table + per-index mapping.
    if (geom.has_skin_weights) {
        uint32_t count = r.read_u32();
        geom.skin_weights.weights.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            geom.skin_weights.weights[i].x = r.read_f32();
            geom.skin_weights.weights[i].y = r.read_f32();
            geom.skin_weights.weights[i].z = r.read_f32();
        }
        geom.skin_weights.index_mapping.resize(index_count);
        for (uint32_t i = 0; i < index_count; ++i) {
            geom.skin_weights.index_mapping[i] = r.read_u32();
        }
    }

    // Extra vertex data block (0x04): opaque payload + per-vertex remap array.
    if (geom.has_extra) {
        uint32_t size = r.read_u32();
        auto bytes = r.read_bytes(size);
        geom.extra_data.assign(bytes.begin(), bytes.end());
        geom.extra_remap.resize(vertex_count);
        for (uint32_t i = 0; i < vertex_count; ++i) {
            geom.extra_remap[i] = r.read_u32();
        }
    }

    // Trailing opaque tag block (0x08): tag + size-prefixed raw payload.
    if (geom.has_tag) {
        geom.tag = r.read_u32();
        uint32_t size = r.read_u32();
        auto bytes = r.read_bytes(size);
        geom.tag_data.assign(bytes.begin(), bytes.end());
    }

    return geom;
}

} // namespace lu::assets
