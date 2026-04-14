#include "lego/brick_geometry/brick_geometry.h"
#include "common/binary_reader/binary_reader.h"
#include <string>

namespace lu::assets {

BrickGeometry brick_geometry_parse(std::span<const uint8_t> data) {
    BinaryReader r(data);

    uint32_t magic = r.read_u32();
    if (magic != BRICK_GEOM_MAGIC) {
        throw BrickGeometryError("BrickGeometry: invalid magic (expected 0x42420D31, got 0x" +
                                  std::to_string(magic) + ")");
    }

    uint32_t vertex_count = r.read_u32();
    uint32_t index_count = r.read_u32();
    uint32_t options = r.read_u32();

    BrickGeometry geom;
    geom.options = options;
    geom.has_uvs = (options & 3) == 3;
    geom.has_bones = (options & 48) == 48;

    // Vertex positions (vertex_count * 3 floats)
    geom.vertices.resize(vertex_count);
    for (uint32_t i = 0; i < vertex_count; ++i) {
        geom.vertices[i].position.x = r.read_f32();
        geom.vertices[i].position.y = r.read_f32();
        geom.vertices[i].position.z = r.read_f32();
    }

    // Vertex normals (vertex_count * 3 floats)
    for (uint32_t i = 0; i < vertex_count; ++i) {
        geom.vertices[i].normal.x = r.read_f32();
        geom.vertices[i].normal.y = r.read_f32();
        geom.vertices[i].normal.z = r.read_f32();
    }

    // Texture coordinates (optional, vertex_count * 2 floats)
    if (geom.has_uvs) {
        for (uint32_t i = 0; i < vertex_count; ++i) {
            geom.vertices[i].u = r.read_f32();
            geom.vertices[i].v = r.read_f32();
        }
    }

    // Triangle indices (index_count u32 values)
    geom.indices.resize(index_count);
    for (uint32_t i = 0; i < index_count; ++i) {
        geom.indices[i] = r.read_u32();
    }

    // Bone mapping: per-vertex bone index assignment.
    // Format (from community tools): u32 bone_length, then bone_length u32 bone indices.
    if (geom.has_bones && r.remaining() >= 4) {
        uint32_t bone_length = r.read_u32();
        if (bone_length <= vertex_count * 2 && r.remaining() >= bone_length * 4) {
            geom.bone_mapping.resize(bone_length);
            for (uint32_t i = 0; i < bone_length; ++i) {
                geom.bone_mapping[i] = r.read_u32();
            }
        }
    }

    return geom;
}

} // namespace lu::assets
