#include <gtest/gtest.h>
#include "netdevil/zone/zal/zal_reader.h"

#include <string>
#include <vector>

using namespace lu::assets;

namespace {

std::vector<uint8_t> to_bytes(const std::string& s) {
    return {s.begin(), s.end()};
}

} // anonymous namespace

TEST(ZAL, ParseThreePathsNormalized) {
    auto data = to_bytes(
        "res\\textures\\image.dds\n"
        "res/audio/sound.fsb\n"
        "res\\models\\mesh.nif\n"
    );

    auto zal = zal_parse(data);

    ASSERT_EQ(zal.asset_paths.size(), 3u);
    EXPECT_EQ(zal.asset_paths[0], "res/textures/image.dds");
    EXPECT_EQ(zal.asset_paths[1], "res/audio/sound.fsb");
    EXPECT_EQ(zal.asset_paths[2], "res/models/mesh.nif");
}

TEST(ZAL, EmptyDataReturnsEmptyList) {
    std::vector<uint8_t> empty;
    auto zal = zal_parse(empty);
    EXPECT_TRUE(zal.asset_paths.empty());
}

TEST(ZAL, SkipsEmptyLines) {
    auto data = to_bytes(
        "res\\file1.dds\n"
        "\n"
        "res\\file2.dds\n"
    );

    auto zal = zal_parse(data);

    ASSERT_EQ(zal.asset_paths.size(), 2u);
    EXPECT_EQ(zal.asset_paths[0], "res/file1.dds");
    EXPECT_EQ(zal.asset_paths[1], "res/file2.dds");
}
