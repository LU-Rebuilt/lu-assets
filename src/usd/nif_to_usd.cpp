#include "usd/nif_to_usd.h"

#include "gamebryo/nif/nif_geometry.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdGeom/primvar.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/assetPath.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_set>

PXR_NAMESPACE_USING_DIRECTIVE

namespace lu::assets {

namespace {

// Sanitize an arbitrary NIF node/mesh name into a valid USD prim name segment.
// USD prim names must be C-identifier-like (alnum + underscore, non-digit lead).
std::string sanitize_prim_name(const std::string& raw, const std::string& fallback) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_');
    }
    if (out.empty()) out = fallback;
    if (std::isdigit(static_cast<unsigned char>(out[0]))) out.insert(out.begin(), '_');
    return out;
}

// Ensure the chosen prim name is unique among siblings under the same parent.
std::string unique_name(std::unordered_set<std::string>& used, std::string base) {
    std::string name = base;
    int suffix = 1;
    while (!used.insert(name).second) {
        name = base + "_" + std::to_string(suffix++);
    }
    return name;
}

// Author a UsdPreviewSurface material for one mesh's NifRenderMaterial. Returns
// the created material so the caller can bind it to the mesh.
UsdShadeMaterial author_material(const UsdStagePtr& stage,
                                 const SdfPath& parent_path,
                                 const std::string& mesh_prim_name,
                                 const NifRenderMaterial& mat) {
    const SdfPath mat_path = parent_path.AppendChild(
        TfToken("mat_" + mesh_prim_name));
    UsdShadeMaterial material = UsdShadeMaterial::Define(stage, mat_path);

    UsdShadeShader surface = UsdShadeShader::Define(
        stage, mat_path.AppendChild(TfToken("surface")));
    surface.CreateIdAttr(VtValue(TfToken("UsdPreviewSurface")));

    surface.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f)
        .Set(GfVec3f(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]));
    surface.CreateInput(TfToken("emissiveColor"), SdfValueTypeNames->Color3f)
        .Set(GfVec3f(mat.emissive[0], mat.emissive[1], mat.emissive[2]));
    // NiMaterialProperty stores specular as a color plus a shininess exponent in
    // the 4th component; UsdPreviewSurface uses roughness/metallic instead, so we
    // approximate: any specular color -> lower roughness, and never metallic.
    const float spec_lum = (mat.specular[0] + mat.specular[1] + mat.specular[2]) / 3.0f;
    surface.CreateInput(TfToken("roughness"), SdfValueTypeNames->Float)
        .Set(std::clamp(1.0f - spec_lum, 0.05f, 1.0f));
    surface.CreateInput(TfToken("metallic"), SdfValueTypeNames->Float).Set(0.0f);
    surface.CreateInput(TfToken("opacity"), SdfValueTypeNames->Float)
        .Set(mat.diffuse[3]);

    UsdShadeOutput surface_out = surface.CreateOutput(
        TfToken("surface"), SdfValueTypeNames->Token);
    material.CreateSurfaceOutput().ConnectToSource(surface_out);

    // Wire the diffuse texture through a UsdUVTexture + a primvar reader for st.
    if (!mat.diffuse_texture.empty()) {
        UsdShadeShader st_reader = UsdShadeShader::Define(
            stage, mat_path.AppendChild(TfToken("stReader")));
        st_reader.CreateIdAttr(VtValue(TfToken("UsdPrimvarReader_float2")));
        // UsdPreviewSurface's PrimvarReader spec types varname as string, not
        // token — usdchecker's SdrCompliance validator rejects token here.
        UsdShadeInput st_input = st_reader.CreateInput(
            TfToken("varname"), SdfValueTypeNames->String);
        st_input.Set(std::string("st"));
        UsdShadeOutput st_out = st_reader.CreateOutput(
            TfToken("result"), SdfValueTypeNames->Float2);

        UsdShadeShader tex = UsdShadeShader::Define(
            stage, mat_path.AppendChild(TfToken("diffuseTexture")));
        tex.CreateIdAttr(VtValue(TfToken("UsdUVTexture")));
        // Preserve the NIF's texture path verbatim as an asset reference. It is
        // a relative client path (e.g. "textures/foo.dds"); resolution is left
        // to whatever opens the stage, matching how the client resolves it.
        tex.CreateInput(TfToken("file"), SdfValueTypeNames->Asset)
            .Set(SdfAssetPath(mat.diffuse_texture));
        tex.CreateInput(TfToken("st"), SdfValueTypeNames->Float2)
            .ConnectToSource(st_out);
        UsdShadeOutput rgb_out = tex.CreateOutput(
            TfToken("rgb"), SdfValueTypeNames->Float3);
        surface.GetInput(TfToken("diffuseColor")).ConnectToSource(rgb_out);
    }

    return material;
}

} // namespace

void nif_to_usd(const NifFile& nif,
                const std::string& out_path,
                const std::string& source_name,
                const NifToUsdOptions& options) {
    NifRenderExtractionResult extracted = extractNifRenderGeometry(nif);

    UsdStageRefPtr stage = UsdStage::CreateNew(out_path);
    if (!stage) {
        throw NifError("nif_to_usd: failed to create USD stage at " + out_path);
    }

    // NIF/Gamebryo is Z-up. Tag the stage accordingly rather than silently
    // reinterpreting axes; a Y-up bake is opt-in via options.convert_to_y_up.
    UsdGeomSetStageUpAxis(stage,
        options.convert_to_y_up ? UsdGeomTokens->y : UsdGeomTokens->z);
    UsdGeomSetStageMetersPerUnit(stage, 1.0);
    stage->SetMetadata(TfToken("comment"),
        VtValue("Exported from LU NIF: " + source_name));

    const SdfPath root_path("/model");
    UsdGeomXform root = UsdGeomXform::Define(stage, root_path);
    stage->SetDefaultPrim(root.GetPrim());

    // When baking to Y-up, rotate the root -90 about X (Z-up -> Y-up).
    if (options.convert_to_y_up) {
        UsdGeomXformCommonAPI(root).SetRotate(GfVec3f(-90.0f, 0.0f, 0.0f));
    }

    std::unordered_set<std::string> used_names;
    uint32_t mesh_counter = 0;

    for (const NifRenderMesh& rmesh : extracted.meshes) {
        const std::string base = sanitize_prim_name(
            rmesh.name, "mesh_" + std::to_string(mesh_counter));
        const std::string prim_name = unique_name(used_names, base);
        ++mesh_counter;

        const SdfPath mesh_path = root_path.AppendChild(TfToken(prim_name));
        UsdGeomMesh mesh = UsdGeomMesh::Define(stage, mesh_path);

        // Points, normals, uvs.
        VtArray<GfVec3f> points;
        VtArray<GfVec3f> normals;
        VtArray<GfVec2f> uvs;
        points.reserve(rmesh.vertices.size());
        normals.reserve(rmesh.vertices.size());
        uvs.reserve(rmesh.vertices.size());
        VtArray<GfVec3f> colors;
        VtArray<float> opacities;
        const bool want_colors = options.export_vertex_colors && rmesh.has_vertex_colors;
        for (const NifRenderVertex& v : rmesh.vertices) {
            points.push_back(GfVec3f(v.position[0], v.position[1], v.position[2]));
            normals.push_back(GfVec3f(v.normal[0], v.normal[1], v.normal[2]));
            // Flip V: NIF/D3D texture origin is top-left, USD st is bottom-left.
            uvs.push_back(GfVec2f(v.uv[0], 1.0f - v.uv[1]));
            if (want_colors) {
                colors.push_back(GfVec3f(v.color[0], v.color[1], v.color[2]));
                opacities.push_back(v.color[3]);
            }
        }
        mesh.CreatePointsAttr(VtValue(points));
        mesh.CreateNormalsAttr(VtValue(normals));
        mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);

        // Face topology: all triangles.
        VtArray<int> face_vertex_counts;
        VtArray<int> face_vertex_indices;
        const size_t tri_count = rmesh.indices.size() / 3;
        face_vertex_counts.assign(tri_count, 3);
        face_vertex_indices.reserve(rmesh.indices.size());
        for (uint32_t idx : rmesh.indices) {
            face_vertex_indices.push_back(static_cast<int>(idx));
        }
        mesh.CreateFaceVertexCountsAttr(VtValue(face_vertex_counts));
        mesh.CreateFaceVertexIndicesAttr(VtValue(face_vertex_indices));
        // NIF triangles are authored clockwise for a left-handed system;
        // declaring leftHanded keeps front-faces correct in USD's RH viewers.
        mesh.CreateOrientationAttr(VtValue(UsdGeomTokens->leftHanded));
        mesh.CreateSubdivisionSchemeAttr(VtValue(UsdGeomTokens->none));

        UsdGeomPrimvarsAPI primvars(mesh.GetPrim());
        UsdGeomPrimvar st = primvars.CreatePrimvar(
            TfToken("st"), SdfValueTypeNames->TexCoord2fArray,
            UsdGeomTokens->vertex);
        st.Set(uvs);

        if (want_colors) {
            UsdGeomPrimvar disp = primvars.CreatePrimvar(
                TfToken("displayColor"), SdfValueTypeNames->Color3fArray,
                UsdGeomTokens->vertex);
            disp.Set(colors);
            UsdGeomPrimvar dispA = primvars.CreatePrimvar(
                TfToken("displayOpacity"), SdfValueTypeNames->FloatArray,
                UsdGeomTokens->vertex);
            dispA.Set(opacities);
        }

        // Material. Apply() the schema before Bind() so the MaterialBindingAPI
        // is recorded in the prim's applied schemas (usdchecker requires it).
        UsdShadeMaterial material = author_material(stage, root_path, prim_name, rmesh.material);
        UsdShadeMaterialBindingAPI binding = UsdShadeMaterialBindingAPI::Apply(mesh.GetPrim());
        binding.Bind(material);

        // Preserve LU-specific data USD has no native slot for, as custom
        // attributes, so a future USD->NIF importer can reconstruct it.
        UsdPrim prim = mesh.GetPrim();
        prim.CreateAttribute(TfToken("lu:sourceMeshBlock"),
            SdfValueTypeNames->UInt, true)
            .Set(rmesh.source_mesh_block);
        prim.CreateAttribute(TfToken("lu:sourceNodeBlock"),
            SdfValueTypeNames->UInt, true)
            .Set(rmesh.source_node_block);
        if (rmesh.has_lod_range) {
            prim.CreateAttribute(TfToken("lu:lodNear"),
                SdfValueTypeNames->Float, true).Set(rmesh.lod_near);
            prim.CreateAttribute(TfToken("lu:lodFar"),
                SdfValueTypeNames->Float, true).Set(rmesh.lod_far);
        }
        if (rmesh.is_skinned) {
            // Phase 1 only records the bone binding; UsdSkel export is later.
            VtArray<std::string> bone_names(
                rmesh.skin_bone_names.begin(), rmesh.skin_bone_names.end());
            prim.CreateAttribute(TfToken("lu:skinBoneNames"),
                SdfValueTypeNames->StringArray, true).Set(bone_names);
        }
    }

    stage->GetRootLayer()->Save();
}

} // namespace lu::assets
