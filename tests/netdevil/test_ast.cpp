#include <gtest/gtest.h>
#include "netdevil/zone/ast/ast_reader.h"

#include <string>
#include <vector>

using namespace lu::assets;

namespace {

std::vector<uint8_t> to_bytes(const std::string& s) {
    return {s.begin(), s.end()};
}

} // anonymous namespace

TEST(AST, ParsePrefixedPathsNormalized) {
    auto data = to_bytes(
        "A:res\\audio\\file.fsb\n"
        "A:res\\textures\\image.dds\n"
        "A:res/models/mesh.nif\n"
    );

    auto ast = ast_parse(data);

    ASSERT_EQ(ast.asset_paths.size(), 3u);
    EXPECT_EQ(ast.asset_paths[0], "res/audio/file.fsb");
    EXPECT_EQ(ast.asset_paths[1], "res/textures/image.dds");
    EXPECT_EQ(ast.asset_paths[2], "res/models/mesh.nif");
}

TEST(AST, CommentsAndEmptyLinesSkipped) {
    auto data = to_bytes(
        "# This is a comment\n"
        "\n"
        "A:res\\file1.dds\n"
        "# Another comment\n"
        "\n"
        "A:res\\file2.dds\n"
    );

    auto ast = ast_parse(data);

    ASSERT_EQ(ast.asset_paths.size(), 2u);
    EXPECT_EQ(ast.asset_paths[0], "res/file1.dds");
    EXPECT_EQ(ast.asset_paths[1], "res/file2.dds");
}

TEST(AST, EmptyDataReturnsEmptyList) {
    std::vector<uint8_t> empty;
    auto ast = ast_parse(empty);
    EXPECT_TRUE(ast.asset_paths.empty());
}
