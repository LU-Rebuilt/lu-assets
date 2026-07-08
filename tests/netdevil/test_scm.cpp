#include <gtest/gtest.h>
#include "netdevil/macros/scm/scm_reader.h"

#include <string>
#include <vector>

using namespace lu::assets;

namespace {

std::vector<uint8_t> to_bytes(const std::string& s) {
    return {s.begin(), s.end()};
}

} // anonymous namespace

TEST(SCM, ParseSlashCommands) {
    auto data = to_bytes(
        "/gmadditem 3\n"
        "/teleport 100 200 300\n"
        "/fly 1\n"
    );

    auto scm = scm_parse(data);

    ASSERT_EQ(scm.commands.size(), 3u);
    EXPECT_EQ(scm.commands[0], "/gmadditem 3");
    EXPECT_EQ(scm.commands[1], "/teleport 100 200 300");
    EXPECT_EQ(scm.commands[2], "/fly 1");
}

TEST(SCM, EmptyDataReturnsEmptyList) {
    std::vector<uint8_t> empty;
    auto scm = scm_parse(empty);
    EXPECT_TRUE(scm.commands.empty());
}

TEST(SCM, SkipsEmptyLines) {
    auto data = to_bytes(
        "/cmd1\n"
        "\n"
        "/cmd2\n"
    );

    auto scm = scm_parse(data);

    ASSERT_EQ(scm.commands.size(), 2u);
    EXPECT_EQ(scm.commands[0], "/cmd1");
    EXPECT_EQ(scm.commands[1], "/cmd2");
}

// ---- Round-trip (scm_write) ----

#include "netdevil/macros/scm/scm_writer.h"

TEST(SCM, RoundTripPreservesBlankLinesAndTerminators) {
    std::string text = "setvar x 1\r\n\r\nplayanim wave"; // mixed CRLF + no final newline
    std::vector<uint8_t> data(text.begin(), text.end());
    auto scm = scm_parse(data);
    EXPECT_EQ(scm_write(scm), data);
    ASSERT_EQ(scm.commands.size(), 2u);
    EXPECT_EQ(scm.commands[1], "playanim wave");
}
