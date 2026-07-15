#include "usd/usd_to_nif.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/vt/array.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

namespace lu::assets {

namespace {

// ---------------------------------------------------------------------------
// Little-endian byte emitter for hand-building NIF block payloads. NIF is
// little-endian on every version LU ships (endian flag == 1).
// ---------------------------------------------------------------------------
struct ByteWriter {
    std::vector<uint8_t> bytes;

    void u8(uint8_t v) { bytes.push_back(v); }
    void u16(uint16_t v) { bytes.push_back(v & 0xFF); bytes.push_back((v >> 8) & 0xFF); }
    void u32(uint32_t v) {
        bytes.push_back(v & 0xFF);
        bytes.push_back((v >> 8) & 0xFF);
        bytes.push_back((v >> 16) & 0xFF);
        bytes.push_back((v >> 24) & 0xFF);
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        u32(bits);
    }
    void vec3(float x, float y, float z) { f32(x); f32(y); f32(z); }
};

// A NIF string table de-duplicator: NIF version 20.3.0.9 references all names
// by index into the header string table.
struct StringTable {
    std::vector<std::string> entries;
    std::map<std::string, int32_t> lookup;

    // Returns the index for a name, adding it if new. Empty name -> -1 (no name),
    // matching how NiObjectNET encodes "unnamed".
    int32_t intern(const std::string& s) {
        if (s.empty()) return -1;
        auto it = lookup.find(s);
        if (it != lookup.end()) return it->second;
        int32_t idx = static_cast<int32_t>(entries.size());
        entries.push_back(s);
        lookup[s] = idx;
        return idx;
    }
};

// One geometry mesh pulled from the USD stage, already triangulated and in the
// coordinate space we will write to the NIF.
struct ImportMesh {
    std::string name;
    std::vector<GfVec3f> positions;
    std::vector<GfVec3f> normals;
    std::vector<GfVec2f> uvs;
    std::vector<GfVec3f> colors;
    std::vector<float> opacities;
    bool has_colors = false;
    std::vector<uint32_t> indices; // triangle list

    // Material (constant inputs off the bound UsdPreviewSurface).
    GfVec3f diffuse{0.7f, 0.7f, 0.7f};
    GfVec3f emissive{0.0f, 0.0f, 0.0f};
    GfVec3f specular{0.0f, 0.0f, 0.0f};
    float glossiness = 10.0f;
    float alpha = 1.0f;
    std::string diffuse_texture; // asset path, empty if untextured
};

// Read the bound material's UsdPreviewSurface constant inputs into `mesh`.
void read_material(const UsdPrim& mesh_prim, ImportMesh& mesh) {
    UsdShadeMaterialBindingAPI binding(mesh_prim);
    UsdShadeMaterial material = binding.ComputeBoundMaterial();
    if (!material) return;

    UsdShadeShader surface;
    if (UsdShadeOutput out = material.GetSurfaceOutput()) {
        UsdShadeConnectableAPI src;
        TfToken srcName;
        UsdShadeAttributeType srcType;
        if (out.GetConnectedSource(&src, &srcName, &srcType)) {
            surface = UsdShadeShader(src.GetPrim());
        }
    }
    if (!surface) return;

    auto get_color = [&](const char* name, GfVec3f& dst) {
        if (UsdShadeInput in = surface.GetInput(TfToken(name))) {
            GfVec3f v;
            if (in.Get(&v)) dst = v;
        }
    };
    get_color("diffuseColor", mesh.diffuse);
    get_color("emissiveColor", mesh.emissive);
    if (UsdShadeInput in = surface.GetInput(TfToken("opacity"))) {
        float a;
        if (in.Get(&a)) mesh.alpha = a;
    }
    if (UsdShadeInput in = surface.GetInput(TfToken("roughness"))) {
        float rough;
        if (in.Get(&rough)) {
            // Inverse of the export approximation (specular luminance -> 1-rough).
            const float spec = std::clamp(1.0f - rough, 0.0f, 1.0f);
            mesh.specular = GfVec3f(spec, spec, spec);
        }
    }

    // Diffuse texture: follow diffuseColor's connection to a UsdUVTexture's file.
    if (UsdShadeInput diff = surface.GetInput(TfToken("diffuseColor"))) {
        UsdShadeConnectableAPI src;
        TfToken srcName;
        UsdShadeAttributeType srcType;
        if (diff.GetConnectedSource(&src, &srcName, &srcType)) {
            UsdShadeShader tex(src.GetPrim());
            if (UsdShadeInput file = tex.GetInput(TfToken("file"))) {
                SdfAssetPath path;
                if (file.Get(&path)) {
                    mesh.diffuse_texture = path.GetAssetPath();
                }
            }
        }
    }
}

// Fan-triangulate a UsdGeomMesh's arbitrary polygons into a triangle list,
// resolving points/normals/uvs/colors to per-vertex arrays. Meshes exported by
// nif_to_usd are already triangles; this also handles hand-authored quads/ngons.
bool extract_mesh(const UsdGeomMesh& gmesh, const GfMatrix4d& world,
                  ImportMesh& out) {
    VtArray<GfVec3f> points;
    gmesh.GetPointsAttr().Get(&points);
    if (points.empty()) return false;

    VtArray<int> face_counts, face_indices;
    gmesh.GetFaceVertexCountsAttr().Get(&face_counts);
    gmesh.GetFaceVertexIndicesAttr().Get(&face_indices);
    if (face_counts.empty()) return false;

    VtArray<GfVec3f> normals;
    gmesh.GetNormalsAttr().Get(&normals);

    // st primvar (first UV set).
    VtArray<GfVec2f> uvs;
    UsdGeomPrimvarsAPI primvars(gmesh.GetPrim());
    if (UsdGeomPrimvar st = primvars.GetPrimvar(TfToken("st"))) {
        st.ComputeFlattened(&uvs);
    }
    VtArray<GfVec3f> disp_colors;
    VtArray<float> disp_opacity;
    if (UsdGeomPrimvar dc = primvars.GetPrimvar(TfToken("displayColor"))) {
        dc.ComputeFlattened(&disp_colors);
    }
    if (UsdGeomPrimvar da = primvars.GetPrimvar(TfToken("displayOpacity"))) {
        da.ComputeFlattened(&disp_opacity);
    }

    const bool normals_per_vertex = normals.size() == points.size();
    const bool uvs_per_vertex = uvs.size() == points.size();
    const bool colors_per_vertex = disp_colors.size() == points.size();
    out.has_colors = colors_per_vertex;

    // Transform points/normals by the prim's world matrix so the flat NIF
    // scene (all shapes under one identity-transform root) matches the USD
    // layout. Normals use the inverse-transpose to stay correct under scale.
    const GfMatrix4d normal_mat = world.GetInverse().GetTranspose();

    out.positions.resize(points.size());
    out.normals.assign(points.size(), GfVec3f(0, 1, 0));
    out.uvs.assign(points.size(), GfVec2f(0, 0));
    if (out.has_colors) {
        out.colors.assign(points.size(), GfVec3f(1, 1, 1));
        out.opacities.assign(points.size(), 1.0f);
    }
    for (size_t i = 0; i < points.size(); ++i) {
        GfVec3d p = world.Transform(GfVec3d(points[i]));
        out.positions[i] = GfVec3f(p);
        if (normals_per_vertex) {
            GfVec3f n = GfVec3f(normal_mat.TransformDir(GfVec3d(normals[i])));
            n.Normalize();
            out.normals[i] = n;
        }
        if (uvs_per_vertex) {
            // Undo the V-flip nif_to_usd applies (NIF/D3D top-left origin).
            out.uvs[i] = GfVec2f(uvs[i][0], 1.0f - uvs[i][1]);
        }
        if (out.has_colors) {
            out.colors[i] = disp_colors[i];
            out.opacities[i] = i < disp_opacity.size() ? disp_opacity[i] : 1.0f;
        }
    }

    // Triangulate. Skip degenerate faces (< 3 corners).
    size_t cursor = 0;
    for (int count : face_counts) {
        if (count >= 3) {
            const int base = face_indices[cursor];
            for (int k = 1; k + 1 < count; ++k) {
                out.indices.push_back(static_cast<uint32_t>(base));
                out.indices.push_back(static_cast<uint32_t>(face_indices[cursor + k]));
                out.indices.push_back(static_cast<uint32_t>(face_indices[cursor + k + 1]));
            }
        }
        cursor += count;
    }
    return !out.indices.empty();
}

// Compute a bounding sphere (center + radius) over positions — NiTriShapeData
// stores one and the client uses it for culling.
void bounding_sphere(const std::vector<GfVec3f>& pts, GfVec3f& center, float& radius) {
    center = GfVec3f(0, 0, 0);
    for (const auto& p : pts) center += p;
    if (!pts.empty()) center /= static_cast<float>(pts.size());
    radius = 0.0f;
    for (const auto& p : pts) {
        radius = std::max(radius, (p - center).GetLength());
    }
}

} // namespace

NifFile usd_to_nif(const std::string& usd_path, const UsdToNifOptions& options) {
    UsdStageRefPtr stage = UsdStage::Open(usd_path);
    if (!stage) {
        throw NifError("usd_to_nif: failed to open USD stage " + usd_path);
    }

    // If the stage is Y-up, we need to rotate geometry back to NIF's Z-up. Bake
    // it into the world matrix used for every mesh: (x, y, z) -> (x, -z, y).
    const bool y_up =
        UsdGeomGetStageUpAxis(stage) == UsdGeomTokens->y || options.force_z_up_from_y;
    GfMatrix4d up_fix(1.0);
    if (y_up) {
        up_fix.SetRow(0, GfVec4d(1, 0, 0, 0));
        up_fix.SetRow(1, GfVec4d(0, 0, 1, 0));
        up_fix.SetRow(2, GfVec4d(0, -1, 0, 0));
        up_fix.SetRow(3, GfVec4d(0, 0, 0, 1));
    }

    std::vector<ImportMesh> meshes;
    UsdGeomXformCache xform_cache;
    for (const UsdPrim& prim : stage->Traverse()) {
        if (!prim.IsA<UsdGeomMesh>()) continue;
        UsdGeomMesh gmesh(prim);
        GfMatrix4d world = xform_cache.GetLocalToWorldTransform(prim);
        if (y_up) world = world * up_fix;

        ImportMesh im;
        im.name = prim.GetName().GetString();
        if (!extract_mesh(gmesh, world, im)) continue;
        read_material(prim, im);
        meshes.push_back(std::move(im));
    }

    if (meshes.empty()) {
        throw NifError("usd_to_nif: no usable UsdGeomMesh geometry on stage " + usd_path);
    }

    // -----------------------------------------------------------------------
    // Assemble the NIF. Block layout (indices):
    //   0: NiNode "Scene Root"
    //   then per mesh, in order:
    //     NiTriShape, NiTriShapeData, NiMaterialProperty,
    //     [NiTexturingProperty, NiSourceTexture]  (only when textured)
    // -----------------------------------------------------------------------
    NifFile nif;
    nif.version = options.version;
    nif.user_version = options.user_version;
    nif.user_version_2 = options.user_version_2;
    nif.endian = 1;
    nif.header_line = "Gamebryo File Format, Version 20.3.0.9\n";
    nif.export_info[0] = "lu-assets usd_to_nif";
    nif.export_info[1] = "";
    nif.export_info[2] = "";

    StringTable strings;

    // Reserve block index 0 for the root node; compute child indices as we go.
    std::vector<std::string> block_types;
    std::vector<std::vector<uint8_t>> block_data;

    // First pass: allocate block indices so shapes can reference their data.
    struct MeshBlocks {
        int32_t shape = -1, data = -1, material = -1, texturing = -1, source = -1;
    };
    std::vector<MeshBlocks> layout(meshes.size());
    int32_t next = 1; // 0 = root node
    for (size_t i = 0; i < meshes.size(); ++i) {
        layout[i].shape = next++;
        layout[i].data = next++;
        layout[i].material = next++;
        if (!meshes[i].diffuse_texture.empty()) {
            layout[i].texturing = next++;
            layout[i].source = next++;
        }
    }
    const int32_t total_blocks = next;

    block_types.resize(total_blocks);
    block_data.resize(total_blocks);

    // --- Block 0: NiNode "Scene Root" ---
    {
        ByteWriter w;
        w.i32(strings.intern("Scene Root")); // name
        w.u32(0);                              // num extra data
        w.i32(-1);                             // controller
        w.u16(14);                             // flags (typical NiNode default)
        w.vec3(0, 0, 0);                       // translation
        // identity 3x3 rotation, row-major
        w.f32(1); w.f32(0); w.f32(0);
        w.f32(0); w.f32(1); w.f32(0);
        w.f32(0); w.f32(0); w.f32(1);
        w.f32(1.0f);                           // scale
        w.u32(0);                              // num properties
        w.i32(-1);                             // collision
        w.u32(static_cast<uint32_t>(meshes.size())); // num children
        for (const auto& mb : layout) w.i32(mb.shape);
        w.u32(0);                              // num effects
        block_types[0] = "NiNode";
        block_data[0] = std::move(w.bytes);
    }

    for (size_t i = 0; i < meshes.size(); ++i) {
        const ImportMesh& m = meshes[i];
        const MeshBlocks& mb = layout[i];
        const bool textured = mb.texturing >= 0;
        const uint16_t vcount = static_cast<uint16_t>(m.positions.size());
        const uint16_t tcount = static_cast<uint16_t>(m.indices.size() / 3);

        // --- NiTriShape ---
        {
            ByteWriter w;
            w.i32(strings.intern(m.name));     // name
            w.u32(0);                          // num extra data
            w.i32(-1);                         // controller
            w.u16(14);                         // flags
            w.vec3(0, 0, 0);                   // translation (baked into verts)
            w.f32(1); w.f32(0); w.f32(0);
            w.f32(0); w.f32(1); w.f32(0);
            w.f32(0); w.f32(0); w.f32(1);
            w.f32(1.0f);                       // scale
            // properties: material (+ texturing when present)
            const uint32_t nprops = textured ? 2u : 1u;
            w.u32(nprops);
            w.i32(mb.material);
            if (textured) w.i32(mb.texturing);
            w.i32(-1);                         // collision
            w.i32(mb.data);                    // data ref
            w.i32(-1);                         // skin instance ref
            w.u32(0);                          // num materials
            w.i32(-1);                         // active material
            w.i32(-1);                         // shader property ref (v>=20.2.0.7)
            w.i32(-1);                         // alpha property ref (v>=20.2.0.7)
            block_types[mb.shape] = "NiTriShape";
            block_data[mb.shape] = std::move(w.bytes);
        }

        // --- NiTriShapeData ---
        {
            ByteWriter w;
            w.u32(0);                          // group id (v>=20.3.0.9)
            w.u16(vcount);                     // num vertices
            w.u16(0);                          // keep flags (v>=10.1.0.0)
            w.u8(1);                           // has vertices
            for (const auto& p : m.positions) w.vec3(p[0], p[1], p[2]);
            // vector flags: low 6 bits = UV set count (1), no tangent space.
            w.u16(1);
            w.u8(1);                           // has normals
            for (const auto& n : m.normals) w.vec3(n[0], n[1], n[2]);
            GfVec3f center; float radius;
            bounding_sphere(m.positions, center, radius);
            w.vec3(center[0], center[1], center[2]);
            w.f32(radius);
            w.u8(m.has_colors ? 1 : 0);        // has vertex colors
            if (m.has_colors) {
                for (size_t v = 0; v < m.positions.size(); ++v) {
                    w.f32(m.colors[v][0]);
                    w.f32(m.colors[v][1]);
                    w.f32(m.colors[v][2]);
                    w.f32(m.opacities[v]);
                }
            }
            // UV set 0.
            for (const auto& uv : m.uvs) { w.f32(uv[0]); w.f32(uv[1]); }
            w.u16(0);                          // consistency flags (v>=10.0.1.0)
            w.i32(-1);                         // additional data (v>=20.0.0.4)
            w.u16(tcount);                     // num triangles
            w.u32(static_cast<uint32_t>(m.indices.size())); // num triangle points
            w.u8(1);                           // has triangles
            for (uint32_t idx : m.indices) w.u16(static_cast<uint16_t>(idx));
            w.u16(0);                          // num match groups
            block_types[mb.data] = "NiTriShapeData";
            block_data[mb.data] = std::move(w.bytes);
        }

        // --- NiMaterialProperty ---
        {
            ByteWriter w;
            w.i32(strings.intern(m.name + "_mat")); // name
            w.u32(0);                          // num extra data
            w.i32(-1);                         // controller
            // NiProperty::Flags (u16) is only present for version <= 10.0.0.0;
            // absent at LU's 20.3.0.9, so it is intentionally not written here —
            // writing it would shift every colour field by two bytes.
            w.vec3(0.25f, 0.25f, 0.25f);       // ambient
            w.vec3(m.diffuse[0], m.diffuse[1], m.diffuse[2]);
            w.vec3(m.specular[0], m.specular[1], m.specular[2]);
            w.vec3(m.emissive[0], m.emissive[1], m.emissive[2]);
            w.f32(m.glossiness);
            w.f32(m.alpha);
            block_types[mb.material] = "NiMaterialProperty";
            block_data[mb.material] = std::move(w.bytes);
        }

        if (textured) {
            // --- NiTexturingProperty --- (base slot only)
            // Layout is version-specific; these fields match how the reader
            // parses version 20.3.0.9 (see parse_texturing_property /
            // read_tex_desc in nif_reader.cpp):
            //   NiObjectNET, u16 flags, u32 texture_count, then per slot a bool
            //   presence flag followed (when present) by a texture descriptor.
            //   Apply Mode is absent at this version (only < 20.1.0.1). The base
            //   descriptor at 20.3.0.9 is: i32 source_ref, u16 map_flags (low 2
            //   bits = clamp mode), bool has_texture_transform.
            // texture_count = 7 activates the base..normal slot booleans without
            // reaching the decal thresholds (> 8) at this version.
            {
                ByteWriter w;
                w.i32(-1);                     // name (unnamed)
                w.u32(0);                      // num extra data
                w.i32(-1);                     // controller
                w.u16(0);                      // flags
                w.u32(7);                      // texture count
                w.u8(1);                       // has base texture
                w.i32(mb.source);              //   descriptor: source ref
                w.u16(0x0003);                 //   map flags: clamp mode 3 (wrap S, wrap T)
                w.u8(0);                       //   has texture transform = false
                w.u8(0);                       // has dark
                w.u8(0);                       // has detail
                w.u8(0);                       // has gloss
                w.u8(0);                       // has glow
                w.u8(0);                       // has bump   (texture_count > 5)
                w.u8(0);                       // has normal (texture_count > 6, v>=20.2.0.5)
                // No decals: count 7 <= decal base threshold 8 at v>=20.2.0.5.
                w.u32(0);                       // num shader textures (u32, unconditional v>=10.0.1.0)
                block_types[mb.texturing] = "NiTexturingProperty";
                block_data[mb.texturing] = std::move(w.bytes);
            }
            // --- NiSourceTexture ---
            {
                ByteWriter w;
                w.i32(-1);                     // name
                w.u32(0);                      // num extra data
                w.i32(-1);                     // controller
                w.u8(1);                       // use external
                w.i32(strings.intern(m.diffuse_texture)); // file name (string index at this version)
                w.i32(-1);                     // unknown link / ATextureRenderData ref
                w.u32(1);                      // pixel layout
                w.u32(1);                      // use mipmaps
                w.u32(1);                      // alpha format
                w.u8(1);                       // is static
                w.u8(1);                       // direct render
                w.u8(1);                       // persist render data
                block_types[mb.source] = "NiSourceTexture";
                block_data[mb.source] = std::move(w.bytes);
            }
        }
    }

    // Build the header block-type table + per-block index.
    std::map<std::string, uint16_t> type_index;
    for (const std::string& t : block_types) {
        if (!type_index.count(t)) {
            uint16_t idx = static_cast<uint16_t>(nif.block_types.size());
            nif.block_types.push_back(t);
            type_index[t] = idx;
        }
    }
    nif.block_type_indices.resize(total_blocks);
    for (int32_t i = 0; i < total_blocks; ++i) {
        nif.block_type_indices[i] = type_index[block_types[i]];
    }

    nif.num_blocks = static_cast<uint32_t>(total_blocks);
    nif.block_data = std::move(block_data);
    nif.string_table = strings.entries;
    nif.string_table_max_len = 0;
    for (const std::string& s : nif.string_table) {
        nif.string_table_max_len =
            std::max<uint32_t>(nif.string_table_max_len, static_cast<uint32_t>(s.size()));
    }
    // Root: the scene node (block 0).
    nif.roots = {0};

    // Block sizes are recomputed by nif_write from block_data.
    return nif;
}

} // namespace lu::assets
