#include <gtest/gtest.h>
#include "netdevil/zone/lutriggers/lutriggers_reader.h"

#include <string>
#include <vector>

using namespace lu::assets;

namespace {

std::vector<uint8_t> to_bytes(const std::string& s) {
    return {s.begin(), s.end()};
}

} // anonymous namespace

TEST(LuTriggers, ParseTwoTriggers) {
    auto data = to_bytes(
        R"(<triggers nextID="5">)"
        R"(<trigger id="1" enabled="1">)"
        R"(<event id="OnEnter">)"
        R"(<command id="updateMission" target="self" args="exploretask,1,1,1,AG_POI" />)"
        R"(</event>)"
        R"(</trigger>)"
        R"(<trigger id="4" enabled="0">)"
        R"(<event id="OnExit">)"
        R"(<command id="playEffect" target="target" args="effect1" />)"
        R"(<command id="stopEffect" target="target" args="effect2" />)"
        R"(</event>)"
        R"(</trigger>)"
        R"(</triggers>)"
    );

    auto file = lutriggers_parse(data);

    EXPECT_EQ(file.next_id, 5u);
    ASSERT_EQ(file.triggers.size(), 2u);

    // First trigger
    EXPECT_EQ(file.triggers[0].id, 1u);
    EXPECT_TRUE(file.triggers[0].enabled);
    ASSERT_EQ(file.triggers[0].events.size(), 1u);
    EXPECT_EQ(file.triggers[0].events[0].id, "OnEnter");
    ASSERT_EQ(file.triggers[0].events[0].commands.size(), 1u);
    EXPECT_EQ(file.triggers[0].events[0].commands[0].id, "updateMission");
    EXPECT_EQ(file.triggers[0].events[0].commands[0].target, "self");
    EXPECT_EQ(file.triggers[0].events[0].commands[0].args, "exploretask,1,1,1,AG_POI");

    // Second trigger
    EXPECT_EQ(file.triggers[1].id, 4u);
    EXPECT_FALSE(file.triggers[1].enabled);
    ASSERT_EQ(file.triggers[1].events.size(), 1u);
    EXPECT_EQ(file.triggers[1].events[0].id, "OnExit");
    ASSERT_EQ(file.triggers[1].events[0].commands.size(), 2u);
    EXPECT_EQ(file.triggers[1].events[0].commands[0].id, "playEffect");
    EXPECT_EQ(file.triggers[1].events[0].commands[0].target, "target");
    EXPECT_EQ(file.triggers[1].events[0].commands[0].args, "effect1");
    EXPECT_EQ(file.triggers[1].events[0].commands[1].id, "stopEffect");
    EXPECT_EQ(file.triggers[1].events[0].commands[1].target, "target");
    EXPECT_EQ(file.triggers[1].events[0].commands[1].args, "effect2");
}

TEST(LuTriggers, ParseEmptyThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(lutriggers_parse(empty), LuTriggersError);
}

TEST(LuTriggers, MissingRootElementThrows) {
    // Valid XML but wrong root element
    auto data = to_bytes(R"(<notTriggers />)");
    EXPECT_THROW(lutriggers_parse(data), LuTriggersError);
}
