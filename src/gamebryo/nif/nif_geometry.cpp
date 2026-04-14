#include "gamebryo/nif/nif_geometry.h"

namespace lu::assets {

NifExtractionResult extractNifGeometry(const NifFile& nif,
                                        float pos_x, float pos_y, float pos_z,
                                        float scale) {
    NifExtractionResult result;

    for (const auto& mesh : nif.meshes) {
        if (mesh.vertices.empty() || mesh.triangles.empty()) continue;

        NifExtractedMesh em;
        em.vertices.reserve(mesh.vertices.size() * 3);
        em.normals.reserve(mesh.vertices.size() * 3);

        for (const auto& v : mesh.vertices) {
            em.vertices.push_back(v.position.x * scale + pos_x);
            em.vertices.push_back(v.position.y * scale + pos_y);
            em.vertices.push_back(v.position.z * scale + pos_z);
            em.normals.push_back(v.normal.x);
            em.normals.push_back(v.normal.y);
            em.normals.push_back(v.normal.z);
        }

        em.indices.reserve(mesh.triangles.size() * 3);
        for (const auto& tri : mesh.triangles) {
            if (tri.a < mesh.vertices.size() && tri.b < mesh.vertices.size() &&
                tri.c < mesh.vertices.size()) {
                em.indices.push_back(tri.a);
                em.indices.push_back(tri.b);
                em.indices.push_back(tri.c);
            }
        }

        result.total_vertices += static_cast<uint32_t>(mesh.vertices.size());
        result.total_triangles += static_cast<uint32_t>(mesh.triangles.size());
        result.meshes.push_back(std::move(em));
    }

    return result;
}

} // namespace lu::assets
