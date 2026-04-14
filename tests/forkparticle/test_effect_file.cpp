#include "forkparticle/effect/effect_reader.h"
#include "forkparticle/effect/effect_writer.h"
#include <gtest/gtest.h>

using namespace lu::assets;

TEST(EffectFile, ParseEmpty) {
    auto effect = effect_parse("");
    EXPECT_TRUE(effect.emitters.empty());
}

TEST(EffectFile, ParseSingleEmitter) {
    std::string text =
        "EMITTERNAME: fire\n"
        "TRANSFORM: 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n";
    auto effect = effect_parse(text);
    ASSERT_EQ(effect.emitters.size(), 1u);
    EXPECT_EQ(effect.emitters[0].name, "fire");
}

TEST(EffectFile, ParseTransformMatrix) {
    std::string text =
        "EMITTERNAME: test\n"
        "TRANSFORM: 2 0 0 0 0 3 0 0 0 0 4 0 5 6 7 1\n";
    auto effect = effect_parse(text);
    ASSERT_EQ(effect.emitters.size(), 1u);
    EXPECT_FLOAT_EQ(effect.emitters[0].transform[0], 2.0f);
    EXPECT_FLOAT_EQ(effect.emitters[0].transform[5], 3.0f);
    EXPECT_FLOAT_EQ(effect.emitters[0].transform[10], 4.0f);
    EXPECT_FLOAT_EQ(effect.emitters[0].transform[12], 5.0f);
}

TEST(EffectFile, ParseTimeProperty) {
    std::string text =
        "EMITTERNAME: delayed\n"
        "TRANSFORM: 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n"
        "TIME: 0.5\n";
    auto effect = effect_parse(text);
    ASSERT_EQ(effect.emitters.size(), 1u);
    EXPECT_FLOAT_EQ(effect.emitters[0].time, 0.5f);
}

TEST(EffectFile, ParseMultipleEmitters) {
    std::string text =
        "EMITTERNAME: a\n"
        "TRANSFORM: 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n"
        "EMITTERNAME: b\n"
        "TRANSFORM: 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n";
    auto effect = effect_parse(text);
    EXPECT_EQ(effect.emitters.size(), 2u);
}

TEST(EffectFile, RoundTrip) {
    std::string text =
        "EMITTERNAME: spark\n"
        "TRANSFORM: 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n";
    auto effect = effect_parse(text);
    auto written = effect_write(effect);
    auto reparsed = effect_parse(written);
    ASSERT_EQ(reparsed.emitters.size(), 1u);
    EXPECT_EQ(reparsed.emitters[0].name, "spark");
}

TEST(EffectFile, ParseProperties) {
    std::string text =
        "EMITTERNAME: fx\n"
        "TRANSFORM: 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1\n"
        "FACING: 1\n"
        "ROT: 2\n"
        "TRAIL: 3\n"
        "DS: 1\n"
        "SE: 0\n";
    auto effect = effect_parse(text);
    ASSERT_EQ(effect.emitters.size(), 1u);
    EXPECT_EQ(effect.emitters[0].facing, 1);
    EXPECT_EQ(effect.emitters[0].rot, 2);
    EXPECT_EQ(effect.emitters[0].trail, 3);
}
