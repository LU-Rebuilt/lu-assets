#include "lego/brick_assembly/brick_colors.h"
#include <gtest/gtest.h>

using namespace lu::assets;

TEST(BrickColors, KnownColorWhite) {
    auto c = brick_color_lookup(1); // Bright White
    EXPECT_NEAR(c.r, 1.0f, 0.1f);
    EXPECT_NEAR(c.g, 1.0f, 0.1f);
    EXPECT_NEAR(c.b, 1.0f, 0.1f);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(BrickColors, KnownColorRed) {
    auto c = brick_color_lookup(21); // Bright Red
    EXPECT_GT(c.r, 0.5f);
    EXPECT_LT(c.g, 0.3f);
    EXPECT_LT(c.b, 0.3f);
}

TEST(BrickColors, UnknownReturnsBlack) {
    auto c = brick_color_lookup(99999);
    // Unknown materials should return a default color (black/ID 26)
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

TEST(BrickColors, DifferentMaterialsDifferentColors) {
    auto red = brick_color_lookup(21);   // Bright Red
    auto blue = brick_color_lookup(23);  // Bright Blue
    // Different materials should return different colors
    EXPECT_NE(red.r, blue.r);
}

TEST(BrickColors, MultipleLookupsConsistent) {
    auto c1 = brick_color_lookup(5);
    auto c2 = brick_color_lookup(5);
    EXPECT_FLOAT_EQ(c1.r, c2.r);
    EXPECT_FLOAT_EQ(c1.g, c2.g);
    EXPECT_FLOAT_EQ(c1.b, c2.b);
}
