#include <gtest/gtest.h>
#include "netdevil/zone/aud/aud_reader.h"

#include <string>
#include <vector>

using namespace lu::assets;

namespace {

std::vector<uint8_t> to_bytes(const std::string& s) {
    return {s.begin(), s.end()};
}

} // anonymous namespace

TEST(AUD, ParseSceneAudioAttributes) {
    auto data = to_bytes(
        R"(<SceneAudioAttributes musicCue="NT_Hallway" musicParamName="param1")"
        R"( guid2D="{abc-123}" guid3D="{def-456}" groupName="grp" programName="pgm")"
        R"( musicParamValue="0.75" boredomTime="30.5" />)"
    );

    auto aud = aud_parse(data);

    EXPECT_EQ(aud.music_cue, "NT_Hallway");
    EXPECT_EQ(aud.guid_2d, "{abc-123}");
    EXPECT_FLOAT_EQ(aud.music_param_value, 0.75f);
    EXPECT_FLOAT_EQ(aud.boredom_time, 30.5f);
    EXPECT_EQ(aud.music_param_name, "param1");
    EXPECT_EQ(aud.guid_3d, "{def-456}");
    EXPECT_EQ(aud.group_name, "grp");
    EXPECT_EQ(aud.program_name, "pgm");
}

TEST(AUD, ParseEmptyThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(aud_parse(empty), AudError);
}

TEST(AUD, MissingRootElementThrows) {
    auto data = to_bytes(R"(<WrongElement musicCue="test" />)");
    EXPECT_THROW(aud_parse(data), AudError);
}
