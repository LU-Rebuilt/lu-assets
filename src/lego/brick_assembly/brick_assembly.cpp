#include "lego/brick_assembly/brick_assembly.h"

#include <cmath>
#include <string>
#include <sstream>

namespace lu::assets {

namespace {

// Parse "21" or "26,1" into vector of ints.
std::vector<int> parse_material_ids(const std::string& s) {
    std::vector<int> ids;
    std::istringstream ss(s);
    int v;
    while (ss >> v) {
        ids.push_back(v);
        if (ss.peek() == ',') ss.ignore();
    }
    return ids;
}

// Apply a 4x3 transform matrix [n11,n12,n13, n21,n22,n23, n31,n32,n33, n41,n42,n43]
// to a point (x,y,z). Uses column-major multiply matching lu-toolbox's Point3D.transform():
//   x_new = n11*x + n21*y + n31*z + n41
//   y_new = n12*x + n22*y + n32*z + n42
//   z_new = n13*x + n23*y + n33*z + n43
void apply_transform(const float m[12], float& x, float& y, float& z) {
    float ox = m[0]*x + m[3]*y + m[6]*z + m[9];
    float oy = m[1]*x + m[4]*y + m[7]*z + m[10];
    float oz = m[2]*x + m[5]*y + m[8]*z + m[11];
    x = ox; y = oy; z = oz;
}

// Apply rotation part only (no translation) for normals.
void apply_rotation(const float m[12], float& nx, float& ny, float& nz) {
    float ox = m[0]*nx + m[3]*ny + m[6]*nz;
    float oy = m[1]*nx + m[4]*ny + m[7]*nz;
    float oz = m[2]*nx + m[5]*ny + m[8]*nz;
    // Normalize
    float len = std::sqrt(ox*ox + oy*oy + oz*oz);
    if (len > 1e-8f) { ox /= len; oy /= len; oz /= len; }
    nx = ox; ny = oy; nz = oz;
}

// Assemble one part: load all .g sub-files, apply bone transforms, merge.
void assemble_part(const LxfmlPart& part,
                   const BrickGeometryLoader& loader,
                   AssembledBrick& out) {
    // Load all geometry sub-parts (.g, .g1, .g2, ...)
    for (int pi = 0; ; ++pi) {
        BrickGeometry geom;
        try {
            geom = loader(part.design_id, pi);
        } catch (...) {
            break;
        }
        if (geom.vertices.empty()) break;

        // Per-vertex bone selection for multi-bone parts, derived from the real on-disk
        // skin-index block (options & 0x10) rather than the old (wrong) assumption of a
        // simple per-vertex bone-index array. Ghidra RE (LEGO::BrickGeometry::vfunc3,
        // legouniverse.exe 1.10.64) confirmed the block shape — weight_group_indices[]
        // (one entry per weight group) + index_mapping[] (one entry per TRIANGLE INDEX,
        // not per vertex) — but no consumer function could be found to confirm exactly
        // how weight_group_indices maps to a bone/transform slot. Best-supported reading
        // from the data shapes alone: for triangle-index position i, index_mapping[i]
        // selects into weight_group_indices to give the bone index for vertex
        // indices[i]. Builds a per-vertex bone-index table from that before the main
        // vertex loop; falls back to the first bone when unavailable (no skin-index
        // block, or a vertex never referenced by any triangle index).
        //
        // The 0x20 skin-weight block (Vec3-valued, likely multi-bone blend weights per
        // the standard BlendWeights/BlendIndices shader pattern seen elsewhere in the
        // client) is deliberately NOT applied here — blending math would be guesswork
        // without a confirmed consumer, so parts using it currently render with only
        // the single bone from the 0x10 block (or the first bone, if 0x10 is absent).
        std::vector<int> vertex_bone(geom.vertices.size(), 0);
        if (geom.has_skin_indices && part.bones.size() > 1 &&
            geom.skin_indices.index_mapping.size() == geom.indices.size()) {
            const auto& groups = geom.skin_indices.weight_group_indices;
            for (size_t ii = 0; ii < geom.indices.size(); ++ii) {
                uint32_t group = geom.skin_indices.index_mapping[ii];
                if (group >= groups.size()) continue;
                uint32_t vidx = geom.indices[ii];
                if (vidx >= vertex_bone.size()) continue;
                uint32_t bone = groups[group];
                if (bone < part.bones.size()) {
                    vertex_bone[vidx] = static_cast<int>(bone);
                }
            }
        }

        uint32_t base_vertex = static_cast<uint32_t>(out.vertices.size() / 3);

        for (size_t vi = 0; vi < geom.vertices.size(); ++vi) {
            float px = geom.vertices[vi].position.x;
            float py = geom.vertices[vi].position.y;
            float pz = geom.vertices[vi].position.z;
            float nx = geom.vertices[vi].normal.x;
            float ny = geom.vertices[vi].normal.y;
            float nz = geom.vertices[vi].normal.z;

            int bone_idx = vertex_bone[vi];

            // Apply pre-flex bone transform (for multi-bone parts, position
            // each vertex according to its bone). Then apply the first bone
            // as the global placement transform.
            if (bone_idx != 0 && bone_idx < static_cast<int>(part.bones.size())) {
                apply_transform(part.bones[bone_idx].transform, px, py, pz);
                apply_rotation(part.bones[bone_idx].transform, nx, ny, nz);
            } else if (!part.bones.empty()) {
                apply_transform(part.bones[0].transform, px, py, pz);
                apply_rotation(part.bones[0].transform, nx, ny, nz);
            }

            out.vertices.push_back(px);
            out.vertices.push_back(py);
            out.vertices.push_back(pz);
            out.normals.push_back(nx);
            out.normals.push_back(ny);
            out.normals.push_back(nz);
        }

        for (uint32_t idx : geom.indices) {
            out.indices.push_back(base_vertex + idx);
        }
    }
}

} // anonymous namespace

AssemblyResult assemble_lxfml(const LxfmlFile& lxfml, const BrickGeometryLoader& loader) {
    AssemblyResult result;

    for (const auto& brick : lxfml.bricks) {
        AssembledBrick ab;
        ab.brick_ref_id = brick.ref_id;
        ab.design_id = brick.design_id;
        ab.label = "Brick " + std::to_string(brick.ref_id) +
                   " (design " + std::to_string(brick.design_id) + ")";

        // Get primary material color from the first part
        BrickColor primary_color = {0.6f, 0.6f, 0.6f, 1.0f};
        if (!brick.parts.empty()) {
            auto mats = parse_material_ids(brick.parts[0].materials);
            if (!mats.empty()) {
                primary_color = brick_color_lookup(mats[0]);
            }
        }
        ab.color = primary_color;

        // Assemble all parts of this brick
        for (const auto& part : brick.parts) {
            assemble_part(part, loader, ab);
        }

        if (ab.vertices.empty()) {
            result.bricks_missing++;
            continue;
        }

        result.total_vertices += static_cast<uint32_t>(ab.vertices.size() / 3);
        result.total_triangles += static_cast<uint32_t>(ab.indices.size() / 3);
        result.bricks_loaded++;
        result.bricks.push_back(std::move(ab));
    }

    return result;
}

} // namespace lu::assets
