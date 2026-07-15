#include "usd/hkx_to_usd.h"

#include "havok/reader/hkx_reader.h"
#include "havok/converters/hkx_geometry.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/scope.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>

#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_set>

PXR_NAMESPACE_USING_DIRECTIVE

namespace lu::assets {

namespace {

const char* shape_type_name(Hkx::ShapeType t) {
    switch (t) {
        case Hkx::ShapeType::Box: return "Box";
        case Hkx::ShapeType::Sphere: return "Sphere";
        case Hkx::ShapeType::Capsule: return "Capsule";
        case Hkx::ShapeType::Cylinder: return "Cylinder";
        case Hkx::ShapeType::ConvexVertices: return "ConvexVertices";
        case Hkx::ShapeType::ConvexTransform: return "ConvexTransform";
        case Hkx::ShapeType::ConvexTranslate: return "ConvexTranslate";
        case Hkx::ShapeType::Mopp: return "Mopp";
        case Hkx::ShapeType::List: return "List";
        case Hkx::ShapeType::Transform: return "Transform";
        case Hkx::ShapeType::Triangle: return "Triangle";
        default: return "Unknown";
    }
}

std::string sanitize(const std::string& raw, const std::string& fallback) {
    std::string out;
    for (char c : raw) {
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_');
    }
    if (out.empty()) out = fallback;
    if (std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    return out;
}

} // namespace

// Author the HKX collision meshes onto an existing stage under parent_path.
// Shared by the standalone entry point below and the object-composition layer.
// Returns the number of meshes authored.
int author_hkx_collision(const UsdStagePtr& stage, const SdfPath& parent_path,
                         const std::string& hkx_path, const HkxToUsdOptions& options) {
    Hkx::HkxFile file;
    Hkx::ParseResult result = file.Parse(std::filesystem::path(hkx_path));
    if (!result.success) {
        throw std::runtime_error("hkx_to_usd: parse failed: " + result.error);
    }

    Hkx::ExtractionResult geo = Hkx::extractGeometry(result);

    UsdGeomScope::Define(stage, parent_path);

    std::unordered_set<std::string> used;
    int authored = 0;
    for (size_t i = 0; i < geo.meshes.size(); ++i) {
        const Hkx::ExtractedMesh& m = geo.meshes[i];
        if (m.vertices.empty() || m.indices.empty()) continue;

        std::string base = sanitize(m.label.empty() ? ("shape_" + std::to_string(i)) : m.label,
                                    "shape_" + std::to_string(i));
        std::string name = base;
        int suffix = 1;
        while (!used.insert(name).second) name = base + "_" + std::to_string(suffix++);

        UsdGeomMesh mesh = UsdGeomMesh::Define(stage, parent_path.AppendChild(TfToken(name)));

        VtArray<GfVec3f> points;
        points.reserve(m.vertices.size() / 3);
        for (size_t v = 0; v + 2 < m.vertices.size(); v += 3) {
            points.push_back(GfVec3f(m.vertices[v], m.vertices[v + 1], m.vertices[v + 2]));
        }
        mesh.CreatePointsAttr(VtValue(points));

        VtArray<int> counts(m.indices.size() / 3, 3);
        VtArray<int> indices;
        indices.reserve(m.indices.size());
        for (uint32_t idx : m.indices) indices.push_back(static_cast<int>(idx));
        mesh.CreateFaceVertexCountsAttr(VtValue(counts));
        mesh.CreateFaceVertexIndicesAttr(VtValue(indices));
        mesh.CreateSubdivisionSchemeAttr(VtValue(UsdGeomTokens->none));

        // Collision geometry is a helper, not render geometry: purpose = guide.
        if (options.as_guide) {
            mesh.CreatePurposeAttr(VtValue(UsdGeomTokens->guide));
        }

        // Preserve the original Havok shape classification for a future importer.
        mesh.GetPrim().CreateAttribute(TfToken("lu:hkxShapeType"),
            SdfValueTypeNames->String, true).Set(std::string(shape_type_name(m.shapeType)));

        ++authored;
    }
    return authored;
}

void hkx_to_usd(const std::string& hkx_path,
                const std::string& out_path,
                const std::string& source_name,
                const HkxToUsdOptions& options) {
    UsdStageRefPtr stage = UsdStage::CreateNew(out_path);
    if (!stage) throw std::runtime_error("hkx_to_usd: failed to create USD stage at " + out_path);

    UsdGeomSetStageUpAxis(stage, UsdGeomTokens->z); // Havok/NIF Z-up
    UsdGeomSetStageMetersPerUnit(stage, 1.0);
    stage->SetMetadata(TfToken("comment"), VtValue("Exported from LU HKX: " + source_name));

    const SdfPath root("/collision");
    int n = author_hkx_collision(stage, root, hkx_path, options);
    if (n == 0) {
        throw std::runtime_error("hkx_to_usd: no collision geometry in " + hkx_path);
    }
    stage->SetDefaultPrim(stage->GetPrimAtPath(root));
    stage->GetRootLayer()->Save();
}

} // namespace lu::assets
