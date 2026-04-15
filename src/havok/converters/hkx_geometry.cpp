// hkx_geometry.cpp — Shared HKX geometry extraction.
//
// Extracts world-space triangle meshes from parsed HKX data (rigid body
// collision shapes + scene meshes).

#include "havok/converters/hkx_geometry.h"

#include <algorithm>
#include <string>
#include <array>
#include <cmath>
#include <set>

namespace Hkx {

// ---------------------------------------------------------------------------
// Transform math (same as gl_viewport::transformPoint/combineTransforms
// but self-contained here to avoid depending on the GL viewport library)
// ---------------------------------------------------------------------------

static std::array<float, 3> xformPt(const Transform& t, float x, float y, float z) {
    return {
        t.col0.x*x + t.col1.x*y + t.col2.x*z + t.translation.x,
        t.col0.y*x + t.col1.y*y + t.col2.y*z + t.translation.y,
        t.col0.z*x + t.col1.z*y + t.col2.z*z + t.translation.z
    };
}

static Transform xformCombine(const Transform& parent, const Transform& child) {
    Transform r;
    for (int row = 0; row < 3; row++) {
        float pRow[3];
        if (row==0) { pRow[0]=parent.col0.x; pRow[1]=parent.col1.x; pRow[2]=parent.col2.x; }
        else if (row==1) { pRow[0]=parent.col0.y; pRow[1]=parent.col1.y; pRow[2]=parent.col2.y; }
        else { pRow[0]=parent.col0.z; pRow[1]=parent.col1.z; pRow[2]=parent.col2.z; }
        float c0=pRow[0]*child.col0.x+pRow[1]*child.col0.y+pRow[2]*child.col0.z;
        float c1=pRow[0]*child.col1.x+pRow[1]*child.col1.y+pRow[2]*child.col1.z;
        float c2=pRow[0]*child.col2.x+pRow[1]*child.col2.y+pRow[2]*child.col2.z;
        if (row==0) { r.col0.x=c0; r.col1.x=c1; r.col2.x=c2; }
        else if (row==1) { r.col0.y=c0; r.col1.y=c1; r.col2.y=c2; }
        else { r.col0.z=c0; r.col1.z=c1; r.col2.z=c2; }
    }
    auto ct = xformPt(parent, child.translation.x, child.translation.y, child.translation.z);
    r.translation = {ct[0], ct[1], ct[2], 1.0f};
    return r;
}

static void addVertex(ExtractedMesh& m, const Transform& t, float x, float y, float z) {
    auto p = xformPt(t, x, y, z);
    m.vertices.push_back(p[0]); m.vertices.push_back(p[1]); m.vertices.push_back(p[2]);
}

// ---------------------------------------------------------------------------
// Collision shape extraction (recursive)
// ---------------------------------------------------------------------------

// Tessellate a box primitive into 12 triangles (6 faces x 2 tris)
static void tessellateBox(ExtractedMesh& m, const Transform& xform, const Vector4& halfExtents) {
    float hx = halfExtents.x, hy = halfExtents.y, hz = halfExtents.z;
    // 8 corners
    float corners[8][3] = {
        {-hx, -hy, -hz}, { hx, -hy, -hz}, { hx,  hy, -hz}, {-hx,  hy, -hz},
        {-hx, -hy,  hz}, { hx, -hy,  hz}, { hx,  hy,  hz}, {-hx,  hy,  hz},
    };
    for (auto& c : corners) addVertex(m, xform, c[0], c[1], c[2]);
    // 6 faces, 2 triangles each (CCW winding)
    uint32_t faces[12][3] = {
        {0,2,1},{0,3,2}, {4,5,6},{4,6,7}, // -Z, +Z
        {0,1,5},{0,5,4}, {2,3,7},{2,7,6}, // -Y, +Y
        {0,4,7},{0,7,3}, {1,2,6},{1,6,5}, // -X, +X
    };
    for (auto& f : faces) {
        m.indices.push_back(f[0]); m.indices.push_back(f[1]); m.indices.push_back(f[2]);
    }
}

static void extractShape(std::vector<ExtractedMesh>& out, int& shapeCount,
                          const ShapeInfo& shape, const Transform& parentXform) {
    if (shape.type == ShapeType::Unknown) return;
    shapeCount++;

    // Box primitive: tessellate into triangles
    if (shape.type == ShapeType::Box) {
        ExtractedMesh m;
        m.shapeType = ShapeType::Box;
        tessellateBox(m, parentXform, shape.halfExtents);
        if (!m.indices.empty()) out.push_back(std::move(m));
    }

    // Mesh shapes: planeEquations as vertices + triangles
    if (!shape.triangles.empty() && !shape.planeEquations.empty()) {
        ExtractedMesh m;
        m.shapeType = shape.type;
        m.vertices.reserve(shape.planeEquations.size() * 3);
        for (const auto& v : shape.planeEquations)
            addVertex(m, parentXform, v.x, v.y, v.z);
        for (const auto& tri : shape.triangles) {
            if (tri.a < shape.planeEquations.size() && tri.b < shape.planeEquations.size() &&
                tri.c < shape.planeEquations.size()) {
                m.indices.push_back(tri.a); m.indices.push_back(tri.b); m.indices.push_back(tri.c);
            }
        }
        if (!m.indices.empty()) out.push_back(std::move(m));
    }

    // Compressed mesh
    if (shape.compressedMesh) {
        const auto& cm = *shape.compressedMesh;
        for (const auto& chunk : cm.chunks) {
            Transform cx = parentXform;
            if (chunk.transformIndex != 0xFFFF && chunk.transformIndex < cm.transforms.size())
                cx = xformCombine(parentXform, cm.transforms[chunk.transformIndex]);
            ExtractedMesh m;
            m.shapeType = ShapeType::CompressedMesh;
            int nv = static_cast<int>(chunk.vertices.size()) / 3;
            for (int i = 0; i < nv; i++) {
                float lx = chunk.offset.x + chunk.vertices[i*3] * cm.error;
                float ly = chunk.offset.y + chunk.vertices[i*3+1] * cm.error;
                float lz = chunk.offset.z + chunk.vertices[i*3+2] * cm.error;
                addVertex(m, cx, lx, ly, lz);
            }
            for (auto idx : chunk.indices) m.indices.push_back(static_cast<uint32_t>(idx));
            if (!m.vertices.empty() && !m.indices.empty()) out.push_back(std::move(m));
        }
        if (!cm.bigTriangles.empty() && !cm.bigVertices.empty()) {
            ExtractedMesh m;
            m.shapeType = ShapeType::CompressedMesh;
            for (auto& v : cm.bigVertices) addVertex(m, parentXform, v.x, v.y, v.z);
            for (auto& bt : cm.bigTriangles) {
                m.indices.push_back(bt.a); m.indices.push_back(bt.b); m.indices.push_back(bt.c);
            }
            if (!m.indices.empty()) out.push_back(std::move(m));
        }
    }

    // Recurse into children with appropriate transform
    for (const auto& child : shape.children) {
        Transform childXform = parentXform;
        if (shape.type == ShapeType::Transform || shape.type == ShapeType::ConvexTransform)
            childXform = xformCombine(parentXform, shape.childTransform);
        else if (shape.type == ShapeType::ConvexTranslate) {
            Transform xlate; xlate.translation = shape.translation; xlate.translation.w = 1.0f;
            childXform = xformCombine(parentXform, xlate);
        }
        extractShape(out, shapeCount, child, childXform);
    }
}

// ---------------------------------------------------------------------------
// Scene mesh extraction (node hierarchy walk)
// ---------------------------------------------------------------------------

static void walkNode(std::vector<ExtractedMesh>& out, int& sceneMeshCount,
                      const SceneInfo& scene, int nodeIndex, const Transform& parentXform) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(scene.nodes.size())) return;
    const auto& node = scene.nodes[nodeIndex];
    Transform worldXform = xformCombine(parentXform, node.transform);

    if (node.meshIndex >= 0 && node.meshIndex < static_cast<int>(scene.meshes.size())) {
        const auto& mesh = scene.meshes[node.meshIndex];
        bool hasNonzero = false;
        for (const auto& v : mesh.vertices)
            if (std::abs(v.x)>1e-6f||std::abs(v.y)>1e-6f||std::abs(v.z)>1e-6f) { hasNonzero=true; break; }
        if (hasNonzero && !mesh.vertices.empty() && !mesh.triangles.empty()) {
            ExtractedMesh em;
            em.shapeType = ShapeType::Unknown;
            em.isSceneMesh = true;
            em.label = "Scene node: " + node.name + " (mesh " + std::to_string(node.meshIndex) +
                ", " + std::to_string(mesh.vertices.size()) + " verts)";
            for (const auto& v : mesh.vertices) addVertex(em, worldXform, v.x, v.y, v.z);
            if (!mesh.normals.empty()) {
                for (const auto& n : mesh.normals) {
                    // Transform normal by rotation only (no translation)
                    auto rn = xformPt(worldXform, n.x, n.y, n.z);
                    auto o = xformPt(worldXform, 0, 0, 0);
                    em.normals.push_back(rn[0]-o[0]); em.normals.push_back(rn[1]-o[1]); em.normals.push_back(rn[2]-o[2]);
                }
            }
            for (const auto& tri : mesh.triangles) {
                if (tri.a<mesh.vertices.size()&&tri.b<mesh.vertices.size()&&tri.c<mesh.vertices.size()) {
                    em.indices.push_back(tri.a); em.indices.push_back(tri.b); em.indices.push_back(tri.c);
                }
            }
            sceneMeshCount++;
            out.push_back(std::move(em));
        }
    }
    for (int childIdx : node.childIndices)
        walkNode(out, sceneMeshCount, scene, childIdx, worldXform);
}

static void extractSceneMeshes(std::vector<ExtractedMesh>& out, int& sceneMeshCount,
                                 const SceneInfo& scene, const Transform& rootXform) {
    if (scene.meshes.empty()) return;
    if (scene.rootNodeIndex >= 0 && scene.rootNodeIndex < static_cast<int>(scene.nodes.size())) {
        walkNode(out, sceneMeshCount, scene, scene.rootNodeIndex, rootXform);
    } else {
        // No node hierarchy — apply root transform to all meshes
        for (size_t mi = 0; mi < scene.meshes.size(); mi++) {
            const auto& mesh = scene.meshes[mi];
            if (mesh.vertices.empty() || mesh.triangles.empty()) continue;
            ExtractedMesh em;
            em.shapeType = ShapeType::Unknown;
            em.isSceneMesh = true;
            char buf[128];
            snprintf(buf, sizeof(buf), "Scene mesh %zu (%zu verts, %zu tris)",
                mi, mesh.vertices.size(), mesh.triangles.size());
            em.label = buf;
            for (const auto& v : mesh.vertices) addVertex(em, rootXform, v.x, v.y, v.z);
            for (const auto& tri : mesh.triangles) {
                if (tri.a<mesh.vertices.size()&&tri.b<mesh.vertices.size()&&tri.c<mesh.vertices.size())
                    { em.indices.push_back(tri.a); em.indices.push_back(tri.b); em.indices.push_back(tri.c); }
            }
            sceneMeshCount++;
            out.push_back(std::move(em));
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static void collectOffsets(const ShapeInfo& shape, std::set<uint32_t>& offsets) {
    if (shape.dataOffset != 0) offsets.insert(shape.dataOffset);
    for (const auto& child : shape.children) collectOffsets(child, offsets);
}

ExtractionResult extractGeometry(const ParseResult& result) {
    ExtractionResult out;

    // Collect all rigid bodies
    std::vector<const RigidBodyInfo*> bodies;
    for (auto& sys : result.physicsSystems) for (auto& rb : sys.rigidBodies) bodies.push_back(&rb);
    for (auto& pd : result.physicsData) for (auto& sys : pd.systems) for (auto& rb : sys.rigidBodies) bodies.push_back(&rb);
    for (auto& rb : result.rigidBodies) bodies.push_back(&rb);
    // Dedup by dataOffset — but only for binary-parsed data where offsets are meaningful.
    // XML-parsed data has dataOffset=0 for all objects, so skip dedup in that case.
    bool hasOffsets = false;
    for (auto* b : bodies) if (b->dataOffset != 0) { hasOffsets = true; break; }
    if (hasOffsets) {
        std::sort(bodies.begin(), bodies.end(), [](auto*a,auto*b){return a->dataOffset<b->dataOffset;});
        bodies.erase(std::unique(bodies.begin(), bodies.end(), [](auto*a,auto*b){return a->dataOffset==b->dataOffset;}), bodies.end());
    }
    out.rigidBodyCount = static_cast<int>(bodies.size());

    std::set<uint32_t> renderedOffsets;
    for (auto* rb : bodies) {
        if (rb->shape.type != ShapeType::Unknown) {
            size_t before = out.meshes.size();
            extractShape(out.meshes, out.shapeCount, rb->shape, rb->motion.motionState.transform);
            collectOffsets(rb->shape, renderedOffsets);
            for (size_t i = before; i < out.meshes.size(); i++) {
                char buf[256];
                snprintf(buf, sizeof(buf), "RB shape: %s (pos %.1f,%.1f,%.1f)",
                    rb->shape.className.c_str(), rb->position.x, rb->position.y, rb->position.z);
                out.meshes[i].label = buf;
            }
        }
    }

    // Standalone shapes
    Transform standalone;
    if (result.rigidBodies.size() == 1) {
        auto& t = result.rigidBodies[0].motion.motionState.transform;
        if (std::abs(t.translation.x)>0.001f||std::abs(t.translation.y)>0.001f||std::abs(t.translation.z)>0.001f)
            standalone = t;
    }
    for (auto& shape : result.shapes) {
        if (renderedOffsets.count(shape.dataOffset)) continue;
        bool skip = false;
        for (auto* rb : bodies) {
            if (rb->shape.type==ShapeType::Unknown) continue;
            if (shape.className==rb->shape.className){skip=true;break;}
            for(auto&c:rb->shape.children){if(shape.className==c.className&&shape.numVertices==c.numVertices&&shape.numTriangles==c.numTriangles){skip=true;break;}}
            if(skip)break;
        }
        if (skip) continue;
        size_t before = out.meshes.size();
        extractShape(out.meshes, out.shapeCount, shape, standalone);
        for (size_t i = before; i < out.meshes.size(); i++)
            out.meshes[i].label = "Standalone: " + shape.className;
    }

    // Scene meshes
    Transform sceneRoot; // identity
    for (const auto& scene : result.scenes)
        extractSceneMeshes(out.meshes, out.sceneMeshCount, scene, sceneRoot);

    return out;
}

CollisionMesh extractCollision(const ParseResult& result, const Transform& worldTransform) {
    CollisionMesh out;

    auto addShapeCollision = [&](const ShapeInfo& shape, const Transform& xform) {
        int dummy = 0;
        std::vector<ExtractedMesh> meshes;
        extractShape(meshes, dummy, shape, xform);
        for (auto& m : meshes) {
            uint32_t base = static_cast<uint32_t>(out.vertices.size() / 3);
            out.vertices.insert(out.vertices.end(), m.vertices.begin(), m.vertices.end());
            for (auto idx : m.indices) out.indices.push_back(base + idx);
        }
    };

    // Extract from all rigid bodies
    for (auto& rb : result.rigidBodies) {
        Transform t = xformCombine(worldTransform, rb.motion.motionState.transform);
        addShapeCollision(rb.shape, t);
    }
    for (auto& sys : result.physicsSystems)
        for (auto& rb : sys.rigidBodies) {
            Transform t = xformCombine(worldTransform, rb.motion.motionState.transform);
            addShapeCollision(rb.shape, t);
        }
    for (auto& pd : result.physicsData)
        for (auto& sys : pd.systems)
            for (auto& rb : sys.rigidBodies) {
                Transform t = xformCombine(worldTransform, rb.motion.motionState.transform);
                addShapeCollision(rb.shape, t);
            }

    // Also extract scene meshes (for files that store collision as scene data)
    Transform sceneRoot = worldTransform;
    for (const auto& scene : result.scenes) {
        int dummy = 0;
        std::vector<ExtractedMesh> meshes;
        extractSceneMeshes(meshes, dummy, scene, sceneRoot);
        for (auto& m : meshes) {
            uint32_t base = static_cast<uint32_t>(out.vertices.size() / 3);
            out.vertices.insert(out.vertices.end(), m.vertices.begin(), m.vertices.end());
            for (auto idx : m.indices) out.indices.push_back(base + idx);
        }
    }

    return out;
}

} // namespace Hkx
