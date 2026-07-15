// NIF <-> USD round-trip tests for the USD translation layer.
//
// These build a NIF, export it to a USD stage (nif_to_usd), import it back
// (usd_to_nif), then serialize + reparse the result and assert the geometry,
// material and texture survived. The bar is semantic fidelity (the client would
// load the same model), not byte-identity — USD can't hold Gamebryo's exact
// block layout. A temp directory holds the intermediate .usda.

#include <gtest/gtest.h>

#include "gamebryo/nif/nif_reader.h"
#include "gamebryo/nif/nif_writer.h"
#include "gamebryo/nif/nif_geometry.h"
#include "usd/nif_to_usd.h"
#include "usd/usd_to_nif.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

using namespace lu::assets;

namespace {

// Little-endian block-byte builder mirroring the NIF layout, so the test can
// synthesize a valid single-triangle NIF without depending on the client corpus.
struct BW {
    std::vector<uint8_t> b;
    void u8(uint8_t v) { b.push_back(v); }
    void u16(uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) { uint32_t x; std::memcpy(&x, &v, 4); u32(x); }
    void v3(float x, float y, float z) { f32(x); f32(y); f32(z); }
};

// Build a NIF: root NiNode -> NiTriShape -> NiTriShapeData (one triangle, with
// normals + one UV set), plus a NiMaterialProperty. Returns serialized bytes.
std::vector<uint8_t> build_triangle_nif() {
    NifFile nif;
    nif.version = 0x14030009;
    nif.user_version = 12;
    nif.user_version_2 = 34;
    nif.endian = 1;
    nif.header_line = "Gamebryo File Format, Version 20.3.0.9\n";
    nif.export_info[0] = "test";

    std::vector<std::string> str = {"Root", "Tri", "Tri_mat"};
    auto sidx = [&](const std::string& s) -> int32_t {
        for (size_t i = 0; i < str.size(); ++i) if (str[i] == s) return (int32_t)i;
        return -1;
    };

    nif.block_types = {"NiNode", "NiTriShape", "NiTriShapeData", "NiMaterialProperty"};
    nif.block_type_indices = {0, 1, 2, 3};
    nif.num_blocks = 4;
    nif.block_data.resize(4);

    // NiNode "Root", one child (block 1).
    { BW w;
      w.i32(sidx("Root")); w.u32(0); w.i32(-1);
      w.u16(14); w.v3(0,0,0);
      w.f32(1);w.f32(0);w.f32(0); w.f32(0);w.f32(1);w.f32(0); w.f32(0);w.f32(0);w.f32(1);
      w.f32(1); w.u32(0); w.i32(-1);
      w.u32(1); w.i32(1); w.u32(0);
      nif.block_data[0] = std::move(w.b); }

    // NiTriShape -> data block 2, one property (material block 3).
    { BW w;
      w.i32(sidx("Tri")); w.u32(0); w.i32(-1);
      w.u16(14); w.v3(0,0,0);
      w.f32(1);w.f32(0);w.f32(0); w.f32(0);w.f32(1);w.f32(0); w.f32(0);w.f32(0);w.f32(1);
      w.f32(1);
      w.u32(1); w.i32(3);          // 1 property: material
      w.i32(-1);                   // collision
      w.i32(2);                    // data ref
      w.i32(-1);                   // skin
      w.u32(0); w.i32(-1);         // materials, active
      w.i32(-1); w.i32(-1);        // shader, alpha prop refs
      nif.block_data[1] = std::move(w.b); }

    // NiTriShapeData: 3 verts, 1 triangle.
    { BW w;
      w.u32(0);                    // group id
      w.u16(3);                    // num vertices
      w.u16(0);                    // keep flags
      w.u8(1);                     // has vertices
      w.v3(0,0,0); w.v3(1,0,0); w.v3(0,1,0);
      w.u16(1);                    // vector flags: 1 UV set
      w.u8(1);                     // has normals
      w.v3(0,0,1); w.v3(0,0,1); w.v3(0,0,1);
      w.v3(0.333f,0.333f,0.0f);    // bound center
      w.f32(1.0f);                 // bound radius
      w.u8(0);                     // no vertex colors
      w.f32(0);w.f32(0); w.f32(1);w.f32(0); w.f32(0);w.f32(1); // UV set 0
      w.u16(0);                    // consistency flags
      w.i32(-1);                   // additional data
      w.u16(1);                    // num triangles
      w.u32(3);                    // num triangle points
      w.u8(1);                     // has triangles
      w.u16(0); w.u16(1); w.u16(2);
      w.u16(0);                    // num match groups
      nif.block_data[2] = std::move(w.b); }

    // NiMaterialProperty. NiProperty::Flags (u16) is absent at 20.3.0.9.
    { BW w;
      w.i32(sidx("Tri_mat")); w.u32(0); w.i32(-1);
      w.v3(0.25f,0.25f,0.25f);     // ambient
      w.v3(0.8f,0.1f,0.1f);        // diffuse
      w.v3(0,0,0);                 // specular
      w.v3(0,0,0);                 // emissive
      w.f32(10.0f);                // glossiness
      w.f32(1.0f);                 // alpha
      nif.block_data[3] = std::move(w.b); }

    nif.string_table = str;
    nif.roots = {0};
    return nif_write(nif);
}

std::filesystem::path temp_usd() {
    auto p = std::filesystem::temp_directory_path() /
        ("lu_usd_test_" + std::to_string(::getpid()) + ".usda");
    return p;
}

} // namespace

TEST(NifUsd, GeometrySurvivesRoundTrip) {
    std::vector<uint8_t> src = build_triangle_nif();
    NifFile src_nif = nif_parse(std::span<const uint8_t>(src.data(), src.size()));
    NifRenderExtractionResult src_geo = extractNifRenderGeometry(src_nif);
    ASSERT_EQ(src_geo.meshes.size(), 1u);
    ASSERT_EQ(src_geo.total_vertices, 3u);
    ASSERT_EQ(src_geo.total_triangles, 1u);

    const std::filesystem::path usd = temp_usd();
    nif_to_usd(src_nif, usd.string(), "test.nif");

    NifFile imported = usd_to_nif(usd.string());
    std::vector<uint8_t> out = nif_write(imported);
    NifFile reparsed = nif_parse(std::span<const uint8_t>(out.data(), out.size()));
    NifRenderExtractionResult geo = extractNifRenderGeometry(reparsed);

    EXPECT_EQ(geo.meshes.size(), 1u);
    EXPECT_EQ(geo.total_vertices, 3u);
    EXPECT_EQ(geo.total_triangles, 1u);

    // Vertex positions preserved (order may differ; compare as an unordered set
    // by matching each source vertex to some output vertex within a tolerance).
    const auto& sv = src_geo.meshes[0].vertices;
    const auto& ov = geo.meshes[0].vertices;
    ASSERT_EQ(sv.size(), ov.size());
    for (const auto& s : sv) {
        bool found = false;
        for (const auto& o : ov) {
            float d = 0;
            for (int i = 0; i < 3; ++i) d += std::abs(s.position[i] - o.position[i]);
            if (d < 1e-4f) { found = true; break; }
        }
        EXPECT_TRUE(found) << "vertex position lost in round-trip";
    }

    std::filesystem::remove(usd);
}

TEST(NifUsd, MaterialColorSurvivesRoundTrip) {
    std::vector<uint8_t> src = build_triangle_nif();
    NifFile src_nif = nif_parse(std::span<const uint8_t>(src.data(), src.size()));

    const std::filesystem::path usd = temp_usd();
    nif_to_usd(src_nif, usd.string(), "test.nif");
    NifFile imported = usd_to_nif(usd.string());
    std::vector<uint8_t> out = nif_write(imported);
    NifFile reparsed = nif_parse(std::span<const uint8_t>(out.data(), out.size()));
    NifRenderExtractionResult geo = extractNifRenderGeometry(reparsed);

    ASSERT_EQ(geo.meshes.size(), 1u);
    const NifRenderMaterial& mat = geo.meshes[0].material;
    // Source diffuse was (0.8, 0.1, 0.1); UsdPreviewSurface carries it through.
    EXPECT_NEAR(mat.diffuse[0], 0.8f, 0.02f);
    EXPECT_NEAR(mat.diffuse[1], 0.1f, 0.02f);
    EXPECT_NEAR(mat.diffuse[2], 0.1f, 0.02f);

    std::filesystem::remove(usd);
}

TEST(NifUsd, EmptyStageThrows) {
    // A stage with no meshes must be rejected, not silently produce an empty NIF.
    const std::filesystem::path usd = temp_usd();
    {
        std::ofstream f(usd);
        f << "#usda 1.0\n(\n    defaultPrim = \"root\"\n)\ndef Xform \"root\" {}\n";
    }
    EXPECT_THROW(usd_to_nif(usd.string()), NifError);
    std::filesystem::remove(usd);
}
