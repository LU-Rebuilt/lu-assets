// NIF -> USD LOD variant-set tests.
//
// A NIF whose NiLODNode children carry NiRangeLODData bands exports every LOD
// level as a mesh. Without a switch they would all render on top of each other,
// so nif_to_usd exposes the levels as a "LOD" variant set on the model prim:
// each variant ("LOD<level>") leaves only that level's meshes visible. These
// tests build the NIF in memory (no client corpus needed), export it, and check
// the variant set, the default selection, per-variant visibility, that meshes
// outside any LOD node stay visible, and that usd_to_nif still imports it.

#include <gtest/gtest.h>

#include "gamebryo/nif/nif_geometry.h"
#include "gamebryo/nif/nif_reader.h"
#include "gamebryo/nif/nif_writer.h"
#include "usd/nif_to_usd.h"
#include "usd/usd_to_nif.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/tokens.h>

#include <filesystem>
#include <string>
#include <vector>

#include <unistd.h>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace lu::assets;

namespace {

NifMesh make_triangle(uint32_t block_index) {
    NifMesh mesh;
    mesh.block_index = block_index;
    mesh.vertices.resize(3);
    mesh.vertices[0].position = {0.0f, 0.0f, 0.0f};
    mesh.vertices[1].position = {1.0f, 0.0f, 0.0f};
    mesh.vertices[2].position = {0.0f, 1.0f, 0.0f};
    mesh.triangles.push_back({0, 1, 2});
    return mesh;
}

NifNode make_shape(const std::string& name, uint32_t block_index, int32_t data_ref) {
    NifNode node;
    node.name = name;
    node.type_name = "NiTriShape";
    node.block_index = block_index;
    node.data_ref = data_ref;
    return node;
}

// Root NiNode -> NiLODNode with `levels` shape children ("Level0", "Level1", ...)
// banded by one NiRangeLODData, plus (optionally) a "Static" shape directly
// under the root that belongs to no LOD node.
//
// Block layout: 0 root, 1 LOD node, 2..2+levels-1 level shapes, then the static
// shape, then one NiTriShapeData per shape, then the NiRangeLODData.
NifFile make_lod_nif(uint32_t levels, bool with_static) {
    NifFile nif;
    const uint32_t first_shape = 2;
    const uint32_t static_block = first_shape + levels;
    const uint32_t shape_count = levels + (with_static ? 1 : 0);
    const uint32_t first_data = first_shape + shape_count;
    const uint32_t range_block = first_data + shape_count;

    NifNode root;
    root.name = "Root";
    root.type_name = "NiNode";
    root.block_index = 0;
    root.children = {1};
    if (with_static) root.children.push_back(static_cast<int32_t>(static_block));

    NifNode lod;
    lod.name = "LOD";
    lod.type_name = "NiLODNode";
    lod.block_index = 1;
    lod.lod_data_ref = static_cast<int32_t>(range_block);
    lod.lod_level_count = levels;

    NifRangeLODData ranges;
    ranges.block_index = range_block;
    for (uint32_t level = 0; level < levels; ++level) {
        lod.children.push_back(static_cast<int32_t>(first_shape + level));
        ranges.ranges.push_back({level * 50.0f, (level + 1) * 50.0f});
    }

    nif.nodes = {root, lod};
    for (uint32_t level = 0; level < levels; ++level) {
        nif.nodes.push_back(make_shape("Level" + std::to_string(level),
            first_shape + level, static_cast<int32_t>(first_data + level)));
        nif.meshes.push_back(make_triangle(first_data + level));
    }
    if (with_static) {
        nif.nodes.push_back(make_shape("Static", static_block,
            static_cast<int32_t>(first_data + levels)));
        nif.meshes.push_back(make_triangle(first_data + levels));
    }
    nif.range_lod_data = {ranges};
    nif.roots = {0};
    return nif;
}

std::filesystem::path temp_usd(const std::string& tag) {
    return std::filesystem::temp_directory_path() /
        ("lu_usd_lod_" + tag + "_" + std::to_string(::getpid()) + ".usda");
}

// Find the exported mesh prim for a NIF shape name (prim names are the
// sanitized shape name, possibly suffixed to stay unique).
UsdPrim find_mesh(const UsdStageRefPtr& stage, const std::string& name) {
    for (const UsdPrim& prim : stage->Traverse()) {
        if (!prim.IsA<UsdGeomMesh>()) continue;
        if (prim.GetName().GetString().rfind(name, 0) == 0) return prim;
    }
    return UsdPrim();
}

bool is_visible(const UsdPrim& prim) {
    return UsdGeomImageable(prim).ComputeVisibility() != UsdGeomTokens->invisible;
}

} // namespace

TEST(NifUsdLod, FixtureExtractsOneMeshPerLevel) {
    // Guard the fixture itself: the variant tests below rely on extraction
    // tagging each level shape with its band.
    NifRenderExtractionResult geo = extractNifRenderGeometry(make_lod_nif(3, true));
    ASSERT_EQ(geo.meshes.size(), 4u);
    uint32_t lod_tagged = 0;
    for (const NifRenderMesh& mesh : geo.meshes) {
        if (mesh.has_lod_range) ++lod_tagged;
    }
    EXPECT_EQ(lod_tagged, 3u);
}

TEST(NifUsdLod, MultipleLevelsAuthorVariantSetDefaultingToHighestDetail) {
    const std::filesystem::path usd = temp_usd("default");
    nif_to_usd(make_lod_nif(3, false), usd.string(), "lod.nif");

    UsdStageRefPtr stage = UsdStage::Open(usd.string());
    ASSERT_TRUE(stage);
    UsdPrim model = stage->GetPrimAtPath(SdfPath("/model"));
    ASSERT_TRUE(model);
    ASSERT_TRUE(model.GetVariantSets().HasVariantSet("LOD"));

    UsdVariantSet vset = model.GetVariantSet("LOD");
    EXPECT_EQ(vset.GetVariantNames(),
              (std::vector<std::string>{"LOD0", "LOD1", "LOD2"}));
    EXPECT_EQ(vset.GetVariantSelection(), "LOD0");

    UsdPrim l0 = find_mesh(stage, "Level0");
    UsdPrim l1 = find_mesh(stage, "Level1");
    UsdPrim l2 = find_mesh(stage, "Level2");
    ASSERT_TRUE(l0 && l1 && l2);
    EXPECT_TRUE(is_visible(l0));
    EXPECT_FALSE(is_visible(l1));
    EXPECT_FALSE(is_visible(l2));

    std::filesystem::remove(usd);
}

TEST(NifUsdLod, EachVariantShowsOnlyItsLevel) {
    const std::filesystem::path usd = temp_usd("switch");
    nif_to_usd(make_lod_nif(3, false), usd.string(), "lod.nif");

    UsdStageRefPtr stage = UsdStage::Open(usd.string());
    ASSERT_TRUE(stage);
    UsdVariantSet vset = stage->GetPrimAtPath(SdfPath("/model")).GetVariantSet("LOD");

    for (uint32_t selected = 0; selected < 3; ++selected) {
        ASSERT_TRUE(vset.SetVariantSelection("LOD" + std::to_string(selected)));
        for (uint32_t level = 0; level < 3; ++level) {
            UsdPrim mesh = find_mesh(stage, "Level" + std::to_string(level));
            ASSERT_TRUE(mesh);
            EXPECT_EQ(is_visible(mesh), level == selected)
                << "variant LOD" << selected << ", mesh Level" << level;
        }
    }

    std::filesystem::remove(usd);
}

TEST(NifUsdLod, MeshOutsideLodNodeStaysVisibleInEveryVariant) {
    const std::filesystem::path usd = temp_usd("static");
    nif_to_usd(make_lod_nif(2, true), usd.string(), "lod.nif");

    UsdStageRefPtr stage = UsdStage::Open(usd.string());
    ASSERT_TRUE(stage);
    UsdVariantSet vset = stage->GetPrimAtPath(SdfPath("/model")).GetVariantSet("LOD");
    UsdPrim static_mesh = find_mesh(stage, "Static");
    ASSERT_TRUE(static_mesh);

    for (const std::string& name : vset.GetVariantNames()) {
        ASSERT_TRUE(vset.SetVariantSelection(name));
        EXPECT_TRUE(is_visible(static_mesh)) << "hidden in variant " << name;
    }

    std::filesystem::remove(usd);
}

TEST(NifUsdLod, SingleLevelAuthorsNoVariantSet) {
    // One band means nothing overlaps, so there is nothing to switch between.
    const std::filesystem::path usd = temp_usd("single");
    nif_to_usd(make_lod_nif(1, true), usd.string(), "lod.nif");

    UsdStageRefPtr stage = UsdStage::Open(usd.string());
    ASSERT_TRUE(stage);
    UsdPrim model = stage->GetPrimAtPath(SdfPath("/model"));
    ASSERT_TRUE(model);
    EXPECT_FALSE(model.GetVariantSets().HasVariantSet("LOD"));
    UsdPrim l0 = find_mesh(stage, "Level0");
    ASSERT_TRUE(l0);
    EXPECT_TRUE(is_visible(l0));

    std::filesystem::remove(usd);
}

TEST(NifUsdLod, VariantStageStillImportsEveryMesh) {
    // usd_to_nif imports meshes regardless of visibility, so the variant set must
    // not cost the hidden LOD levels on the way back to NIF.
    const std::filesystem::path usd = temp_usd("import");
    nif_to_usd(make_lod_nif(3, true), usd.string(), "lod.nif");

    // usd_to_nif builds raw blocks; serialize + reparse to get the typed view.
    NifFile imported = usd_to_nif(usd.string());
    std::vector<uint8_t> bytes = nif_write(imported);
    NifFile reparsed = nif_parse(std::span<const uint8_t>(bytes.data(), bytes.size()));
    NifRenderExtractionResult geo = extractNifRenderGeometry(reparsed);
    EXPECT_EQ(geo.meshes.size(), 4u);
    EXPECT_EQ(geo.total_triangles, 4u);

    std::filesystem::remove(usd);
}
