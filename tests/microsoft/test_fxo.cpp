#include <gtest/gtest.h>
#include "microsoft/fxo/fxo_reader.h"

#include <cstring>
#include <string>
#include <vector>

using namespace lu::assets;

namespace {

// Build a minimal well-formed FXO header (0x60 bytes + 4-byte table preamble = 0x64 bytes).
// The parameter table preamble at 0x60 is always 0.
struct FxoBuilder {
    std::vector<uint8_t> data;

    FxoBuilder() {
        // Reserve 0x64 bytes for the fixed header + table preamble
        data.resize(0x64, 0);
        u32_at(0x00, FXO_SIGNATURE);
        u32_at(0x0C, 3);   // unk_0c — always 3 in LU client
        u32_at(0x10, 2);   // unk_10 — always 2 in LU client
        u32_at(0x14, 0x60); // unk_14 — always 0x60 in LU client
        u32_at(0x18, 0x78); // unk_18 — always 0x78 in LU client
        // 0x60: table preamble = 0 (already zeroed)
    }

    void u32_at(size_t off, uint32_t v) {
        if (off + 4 > data.size()) data.resize(off + 4, 0);
        std::memcpy(data.data() + off, &v, 4);
    }

    void u32(uint32_t v) {
        uint8_t buf[4];
        std::memcpy(buf, &v, 4);
        data.insert(data.end(), buf, buf + 4);
    }

    // Append a length-prefixed, null-terminated string, padded to 4 bytes.
    void lstr(const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.size() + 1); // include null
        u32(len);
        data.insert(data.end(), s.begin(), s.end());
        data.push_back(0); // null
        size_t padded = (len + 3) & ~3u;
        while (data.size() % 4 != 0) data.push_back(0);
        // Ensure exactly padded bytes written after len field
        (void)padded;
    }

    // Append a parameter block: preamble + name + semantic + fields + tail(zeros).
    // type=3 (float), cls=1 (vector), rows=4, cols=1 produces a float4.
    // type=3, cls=2, rows=4, cols=4 produces a float4x4 matrix.
    void param(const std::string& name, const std::string& semantic,
               uint32_t type = 3, uint32_t cls = 1,
               uint32_t rows = 4, uint32_t cols = 1) {
        u32(0);               // preamble
        lstr(name);
        lstr(semantic);
        u32(type);            // type
        u32(cls);             // class
        u32(0);               // reg_offset
        u32(0);               // reg_count
        u32(0);               // ann_count
        u32(rows);
        u32(cols);
        // Minimal tail: a couple of zero dwords so the block looks plausible.
        // The scanner stops searching for the next param when no more valid
        // blocks follow, so an empty tail is fine for our synthetic tests.
    }

    // Append a technique record: a raw "Technique_<name>\0" string embedded in
    // data, preceded by 4 zero bytes (the length of the pre-name field block).
    // This simulates the technique section well enough for the technique scanner.
    void technique(const std::string& tech_name) {
        // 16-byte pre-name block (pass count etc.), all zeros
        for (int i = 0; i < 16; ++i) data.push_back(0);
        // name_len + name
        lstr(tech_name);
        // Minimal post-name data
        for (int i = 0; i < 8; ++i) u32(0);
    }

    void set_meta_size() {
        // Set meta_size to total file size (approximate; fine for validation test)
        u32_at(0x04, static_cast<uint32_t>(data.size()));
    }
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Signature and empty-file tests
// ---------------------------------------------------------------------------

TEST(Fxo, EmptyDataReturnsEmptyFile) {
    // FXP pool files (common.fxp, legoppcommon.fxp) are 0 bytes in the LU client.
    std::vector<uint8_t> empty;
    auto fxo = fxo_parse(empty);
    EXPECT_EQ(fxo.total_size, 0u);
    EXPECT_EQ(fxo.signature, 0u);
    EXPECT_TRUE(fxo.parameters.empty());
    EXPECT_TRUE(fxo.techniques.empty());
}

TEST(Fxo, BadSignatureThrows) {
    FxoBuilder b;
    b.set_meta_size();
    b.u32_at(0x00, 0xDEADBEEF);
    EXPECT_THROW(fxo_parse(b.data), FxoError);
}

TEST(Fxo, TooSmallThrows) {
    std::vector<uint8_t> tiny(8, 0);
    // Write valid signature
    uint32_t sig = FXO_SIGNATURE;
    std::memcpy(tiny.data(), &sig, 4);
    EXPECT_THROW(fxo_parse(tiny), FxoError);
}

TEST(Fxo, ValidHeaderParsed) {
    FxoBuilder b;
    b.set_meta_size();
    auto fxo = fxo_parse(b.data);
    EXPECT_EQ(fxo.signature, FXO_SIGNATURE);
    EXPECT_EQ(fxo.unk_0c,  3u);
    EXPECT_EQ(fxo.unk_10,  2u);
    EXPECT_EQ(fxo.unk_14,  0x60u);
    EXPECT_EQ(fxo.unk_18,  0x78u);
    EXPECT_EQ(fxo.total_size, b.data.size());
}

// ---------------------------------------------------------------------------
// Parameter parsing tests
// ---------------------------------------------------------------------------

TEST(Fxo, NoParametersWhenEmpty) {
    FxoBuilder b;
    b.set_meta_size();
    auto fxo = fxo_parse(b.data);
    EXPECT_TRUE(fxo.parameters.empty());
}

TEST(Fxo, SingleParameterParsed) {
    FxoBuilder b;
    b.param("g_matWorldViewProj", "WORLDVIEWPROJECTION", 3, 2, 4, 4);
    b.set_meta_size();
    auto fxo = fxo_parse(b.data);
    ASSERT_EQ(fxo.parameters.size(), 1u);
    EXPECT_EQ(fxo.parameters[0].name,        "g_matWorldViewProj");
    EXPECT_EQ(fxo.parameters[0].semantic,    "WORLDVIEWPROJECTION");
    EXPECT_EQ(fxo.parameters[0].type,        3u);
    EXPECT_EQ(fxo.parameters[0].param_class, 2u);
    EXPECT_EQ(fxo.parameters[0].rows,        4u);
    EXPECT_EQ(fxo.parameters[0].cols,        4u);
}

TEST(Fxo, MultipleParametersParsed) {
    FxoBuilder b;
    b.param("g_view",       "VIEW",       3, 2, 4, 4);
    b.param("g_projection", "PROJECTION", 3, 2, 4, 4);
    b.param("g_fadeColor",  "GLOBAL",     3, 1, 4, 1);
    b.set_meta_size();
    auto fxo = fxo_parse(b.data);
    ASSERT_EQ(fxo.parameters.size(), 3u);
    EXPECT_EQ(fxo.parameters[0].name, "g_view");
    EXPECT_EQ(fxo.parameters[1].name, "g_projection");
    EXPECT_EQ(fxo.parameters[2].name, "g_fadeColor");
    EXPECT_EQ(fxo.parameters[2].semantic, "GLOBAL");
    EXPECT_EQ(fxo.parameters[2].rows, 4u);
    EXPECT_EQ(fxo.parameters[2].cols, 1u);
}

// ---------------------------------------------------------------------------
// Technique extraction tests
// ---------------------------------------------------------------------------

TEST(Fxo, SingleTechniqueExtracted) {
    FxoBuilder b;
    b.param("g_view", "VIEW", 3, 2, 4, 4);
    b.technique("Technique_VertexFog");
    b.set_meta_size();
    auto fxo = fxo_parse(b.data);
    ASSERT_EQ(fxo.techniques.size(), 1u);
    EXPECT_EQ(fxo.techniques[0].name, "Technique_VertexFog");
}

TEST(Fxo, MultipleTechniquesExtracted) {
    FxoBuilder b;
    b.technique("Technique_Basic_Lighting");
    b.technique("Technique_Basic_NoLighting");
    b.technique("Technique_Basic_Lighting_Skinned");
    b.set_meta_size();
    auto fxo = fxo_parse(b.data);
    ASSERT_EQ(fxo.techniques.size(), 3u);
    EXPECT_EQ(fxo.techniques[0].name, "Technique_Basic_Lighting");
    EXPECT_EQ(fxo.techniques[1].name, "Technique_Basic_NoLighting");
    EXPECT_EQ(fxo.techniques[2].name, "Technique_Basic_Lighting_Skinned");
}

// ---------------------------------------------------------------------------
// Raw data coverage test (no gaps)
// ---------------------------------------------------------------------------

TEST(Fxo, RawDataStoresFullFile) {
    FxoBuilder b;
    b.param("g_view", "VIEW", 3, 2, 4, 4);
    b.technique("Technique_Test");
    b.set_meta_size();
    auto fxo = fxo_parse(b.data);
    // raw_data must be the complete file — no gaps
    EXPECT_EQ(fxo.raw_data.size(), b.data.size());
    EXPECT_EQ(fxo.raw_data, b.data);
}

// ---------------------------------------------------------------------------
// Known-value test: D3DXPARAMETER_CLASS for matrix (2 = MatrixRows)
// ---------------------------------------------------------------------------

TEST(Fxo, MatrixParameterClassDetected) {
    FxoBuilder b;
    // 4x4 row-major matrix (HLSL float4x4)
    b.param("g_worldViewProj", "WORLDVIEWPROJECTION", 3, 2, 4, 4);
    // 4x1 vector (HLSL float4)
    b.param("g_lightColor", "GLOBAL", 3, 1, 4, 1);
    // scalar (HLSL float)
    b.param("g_time", "GLOBAL", 3, 0, 1, 1);
    b.set_meta_size();
    auto fxo = fxo_parse(b.data);
    ASSERT_EQ(fxo.parameters.size(), 3u);
    // Matrix: class=2 (MatrixRows)
    EXPECT_EQ(fxo.parameters[0].param_class, 2u);
    EXPECT_EQ(fxo.parameters[0].rows, 4u);
    EXPECT_EQ(fxo.parameters[0].cols, 4u);
    // Vector: class=1
    EXPECT_EQ(fxo.parameters[1].param_class, 1u);
    EXPECT_EQ(fxo.parameters[1].rows, 4u);
    EXPECT_EQ(fxo.parameters[1].cols, 1u);
    // Scalar: class=0
    EXPECT_EQ(fxo.parameters[2].param_class, 0u);
    EXPECT_EQ(fxo.parameters[2].rows, 1u);
    EXPECT_EQ(fxo.parameters[2].cols, 1u);
}
