// KF -> USD animation export tests.
//
// These populate a NifFile's animation vectors directly (sequences +
// interpolators + transform data) rather than round-tripping binary, then
// export to a USD stage and read it back to assert the skeleton, animation
// clips, joints and time samples are authored correctly.

#include <gtest/gtest.h>

#include "gamebryo/nif/nif_types.h"
#include "usd/kf_to_usd.h"

#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdSkel/skeleton.h>
#include <pxr/usd/usdSkel/animation.h>
#include <pxr/base/vt/array.h>
#include <pxr/base/tf/token.h>

#include <filesystem>
#include <string>

#include <unistd.h>

using namespace lu::assets;
PXR_NAMESPACE_USING_DIRECTIVE

namespace {

std::filesystem::path temp_usd(const char* tag) {
    return std::filesystem::temp_directory_path() /
        ("lu_kf_test_" + std::string(tag) + "_" + std::to_string(::getpid()) + ".usda");
}

// A NifFile with one clip "walk" animating one node "Bone" via a translation
// channel with two keys (t=0 -> x=0, t=1 -> x=10).
NifFile build_walk_kf() {
    NifFile kf;
    kf.version = 0x14030009;

    NifTransformData td;
    td.block_index = 100;
    td.num_translation_keys = 2;
    td.translation_key_type = 1; // linear
    td.translation_keys.push_back({0.0f, {0, 0, 0}, {}, {}});
    td.translation_keys.push_back({1.0f, {10, 0, 0}, {}, {}});
    kf.transform_data.push_back(td);

    NifTransformInterpolator interp;
    interp.block_index = 50;
    interp.data_ref = 100;
    interp.translation = {0, 0, 0};
    interp.rotation = {0, 0, 0, 1};
    interp.scale = 1.0f;
    kf.transform_interpolators.push_back(interp);

    NifControlledBlock cb;
    cb.interpolator_ref = 50;
    cb.node_name = "Bone";
    cb.controller_type = "NiTransformController";

    NifControllerSequence seq;
    seq.name = "walk";
    seq.controlled_blocks.push_back(cb);
    seq.num_controlled_blocks = 1;
    seq.start_time = 0.0f;
    seq.stop_time = 1.0f;
    seq.frequency = 1.0f;
    kf.sequences.push_back(seq);

    return kf;
}

} // namespace

TEST(KfUsd, ExportsSkeletonAndAnimation) {
    NifFile kf = build_walk_kf();
    const auto path = temp_usd("basic");
    KfToUsdOptions opt;
    opt.time_codes_per_second = 30.0;
    kf_to_usd(kf, path.string(), "walk.kf", opt);

    UsdStageRefPtr stage = UsdStage::Open(path.string());
    ASSERT_TRUE(stage);

    // Skeleton exists with the one joint.
    UsdSkelSkeleton skel(stage->GetPrimAtPath(SdfPath("/anim/Skeleton")));
    ASSERT_TRUE(skel);
    VtArray<TfToken> joints;
    skel.GetJointsAttr().Get(&joints);
    ASSERT_EQ(joints.size(), 1u);
    EXPECT_EQ(joints[0].GetString(), "Bone");

    // Animation clip exists.
    UsdSkelAnimation anim(stage->GetPrimAtPath(SdfPath("/anim/anim_walk")));
    ASSERT_TRUE(anim);
    VtArray<TfToken> ajoints;
    anim.GetJointsAttr().Get(&ajoints);
    EXPECT_EQ(ajoints.size(), 1u);

    // Stage timing: 0..1s at 30 fps -> time codes 0..30.
    EXPECT_DOUBLE_EQ(stage->GetTimeCodesPerSecond(), 30.0);
    EXPECT_DOUBLE_EQ(stage->GetStartTimeCode(), 0.0);
    EXPECT_DOUBLE_EQ(stage->GetEndTimeCode(), 30.0);

    // Translation at the end key (time code 30) should be x≈10.
    VtArray<GfVec3f> trans;
    ASSERT_TRUE(anim.GetTranslationsAttr().Get(&trans, UsdTimeCode(30.0)));
    ASSERT_EQ(trans.size(), 1u);
    EXPECT_NEAR(trans[0][0], 10.0f, 1e-4f);

    // And at the start (time code 0) x≈0.
    ASSERT_TRUE(anim.GetTranslationsAttr().Get(&trans, UsdTimeCode(0.0)));
    EXPECT_NEAR(trans[0][0], 0.0f, 1e-4f);

    std::filesystem::remove(path);
}

TEST(KfUsd, NoClipsThrows) {
    NifFile kf; // empty, no sequences
    const auto path = temp_usd("empty");
    EXPECT_THROW(kf_to_usd(kf, path.string(), "empty.kf"), NifError);
}

TEST(KfUsd, ClipIndexSelectsSingleClip) {
    NifFile kf = build_walk_kf();
    // Add a second clip so selection is observable.
    NifControllerSequence run = kf.sequences[0];
    run.name = "run";
    kf.sequences.push_back(run);

    const auto path = temp_usd("clipidx");
    KfToUsdOptions opt;
    opt.clip_index = 1; // only "run"
    kf_to_usd(kf, path.string(), "two.kf", opt);

    UsdStageRefPtr stage = UsdStage::Open(path.string());
    ASSERT_TRUE(stage);
    EXPECT_TRUE(stage->GetPrimAtPath(SdfPath("/anim/anim_run")));
    EXPECT_FALSE(stage->GetPrimAtPath(SdfPath("/anim/anim_walk")));

    std::filesystem::remove(path);
}
