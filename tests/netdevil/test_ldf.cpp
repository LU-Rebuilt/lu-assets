#include "netdevil/common/ldf/ldf_reader.h"
#include <gtest/gtest.h>

using namespace lu::assets;

// ── Text LDF parsing ─────────────────────────────────────────────────────────

TEST(LDF, ParseEmpty) {
    auto entries = ldf_parse("");
    EXPECT_TRUE(entries.empty());
}

TEST(LDF, ParseSingleWString) {
    auto entries = ldf_parse("name=0:TestValue");
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].key, "name");
    EXPECT_EQ(entries[0].type, LdfType::WString);
    EXPECT_EQ(std::get<std::string>(entries[0].value), "TestValue");
}

TEST(LDF, ParseS32) {
    auto entries = ldf_parse("objectLOT=1:6326");
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].type, LdfType::S32);
    EXPECT_EQ(std::get<int32_t>(entries[0].value), 6326);
}

TEST(LDF, ParseFloat) {
    auto entries = ldf_parse("modelScale=3:1.5");
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].type, LdfType::Float);
    EXPECT_FLOAT_EQ(std::get<float>(entries[0].value), 1.5f);
}

TEST(LDF, ParseBool) {
    auto entries = ldf_parse("create_physics=7:1");
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].type, LdfType::Bool);
    EXPECT_TRUE(std::get<bool>(entries[0].value));
}

TEST(LDF, ParseBoolFalse) {
    auto entries = ldf_parse("enabled=7:0");
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_FALSE(std::get<bool>(entries[0].value));
}

TEST(LDF, ParseU32) {
    auto entries = ldf_parse("flags=5:42");
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].type, LdfType::U32);
    EXPECT_EQ(std::get<uint32_t>(entries[0].value), 42u);
}

TEST(LDF, ParseMultipleEntries) {
    auto entries = ldf_parse("a=0:hello\nb=1:42\nc=7:1\n");
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].key, "a");
    EXPECT_EQ(entries[1].key, "b");
    EXPECT_EQ(entries[2].key, "c");
}

TEST(LDF, ParseEmptyValue) {
    auto entries = ldf_parse("CheckPrecondition=0:");
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(std::get<std::string>(entries[0].value), "");
}

TEST(LDF, RoundTrip) {
    auto entries = ldf_parse("key=1:42");
    ASSERT_EQ(entries.size(), 1u);
    auto str = ldf_entry_to_string(entries[0]);
    EXPECT_EQ(str, "key=1:42");
}

TEST(LDF, ParseUnknownType) {
    auto entries = ldf_parse("weird=99:data");
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].type, LdfType::Unknown);
}

// ── Binary LDF parsing ──────────────────────────────────────────────────────

TEST(LDF, BinaryParseEmpty) {
    uint8_t data[] = {0, 0, 0, 0};
    BinaryReader r(std::span<const uint8_t>(data, sizeof(data)));
    auto config = ldf_parse_binary(r);
    EXPECT_TRUE(config.empty());
}

TEST(LDF, BinaryParseSingleEntry) {
    std::vector<uint8_t> data;
    // count = 1
    data.push_back(1); data.push_back(0); data.push_back(0); data.push_back(0);
    // key "hi": len=2, UTF-16LE
    data.push_back(2);
    data.push_back('h'); data.push_back(0);
    data.push_back('i'); data.push_back(0);
    // value "ok": len=2
    data.push_back(2);
    data.push_back('o'); data.push_back(0);
    data.push_back('k'); data.push_back(0);

    BinaryReader r(std::span<const uint8_t>(data.data(), data.size()));
    auto config = ldf_parse_binary(r);
    ASSERT_EQ(config.size(), 1u);
    EXPECT_EQ(config[0].first, "hi");
    EXPECT_EQ(config[0].second, "ok");
}
