#include <gtest/gtest.h>
#include "gamebryo/settings/settings_reader.h"

#include <cstring>
#include <string>
#include <vector>

using namespace lu::assets;

namespace {

// Builds a well-formed .settings binary matching the observed client format.
struct SettingsBuilder {
    std::vector<uint8_t> data;

    void u8(uint8_t v) { data.push_back(v); }

    void u32(uint32_t v) {
        uint8_t buf[4];
        std::memcpy(buf, &v, 4);
        data.insert(data.end(), buf, buf + 4);
    }

    // u8-length-prefixed string (as used for version, section markers, names)
    void str8(const std::string& s) {
        u8(static_cast<uint8_t>(s.size()));
        data.insert(data.end(), s.begin(), s.end());
    }

    // Write the header fields between version and "Sequences" marker.
    void header(uint32_t flags = 1, uint32_t filename_len = 0,
                uint32_t c = 0, uint8_t d = 0,
                uint32_t e = 0xFFFFFFFF, uint8_t f = 0,
                uint32_t section_type_seq = 1) {
        u32(flags);           // header_flags
        u32(filename_len);    // model_filename_len
        u32(c);               // unk_c
        u8(d);                // unk_d
        u32(e);               // unk_e
        u8(f);                // unk_f
        u32(section_type_seq);// section_type_sequences
    }

    void sequences_marker() { str8("Sequences"); }
    void groups_marker()    { str8("Sequence Groups"); }

    // Write a sequence entry (type 3 = animation, type 2 = group name).
    void sequence(const std::string& name, uint32_t entry_type = 3, uint32_t entry_id = 0) {
        u32(entry_type);
        str8(name);
        u32(entry_id);
    }

    // Write an animation table entry (15 bytes).
    void animation(uint32_t event_code, uint32_t num_trans = 0, uint32_t unk = 0,
                   uint8_t b1 = 0, uint8_t b2 = 0, uint8_t b3 = 0) {
        u32(event_code);
        u32(num_trans);
        u32(unk);
        u8(b1);
        u8(b2);
        u8(b3);
    }
};

// Full well-formed file: version + header + 0 sequences + 0 groups + animation table + footer.
std::vector<uint8_t> build_empty_sequences(const std::string& version = "2.3.0") {
    SettingsBuilder b;
    b.str8(version);
    b.header();
    b.sequences_marker();
    b.u32(0);          // declared_sequence_count = 0
    b.u32(4);          // section_type_groups = 4
    b.groups_marker();
    b.u32(0);          // group_count = 0
    b.u32(0);          // animation_count = 0
    // 4-byte footer (all zeros)
    for (int i = 0; i < 4; ++i) b.u8(0);
    return b.data;
}

// Full well-formed file with one animation sequence.
std::vector<uint8_t> build_one_sequence() {
    SettingsBuilder b;
    b.str8("2.3.0");
    b.header();
    b.sequences_marker();
    b.u32(1);                                  // declared_sequence_count = 1
    b.sequence("test-obj_idle", 3, 0);         // entry_type=3, entry_id=0
    b.u32(4);                                  // section_type_groups
    b.groups_marker();
    b.u32(0);                                  // group_count
    b.u32(1);                                  // animation_count = 1
    b.animation(0);                            // event_code=0, 15 bytes
    // 4-byte footer
    for (int i = 0; i < 4; ++i) b.u8(0);
    return b.data;
}

} // anonymous namespace

TEST(Settings, ParseVersion) {
    auto data = build_empty_sequences("2.3.0");
    auto s = settings_parse(data);
    EXPECT_EQ(s.version, "2.3.0");
}

TEST(Settings, ParseHeader) {
    auto data = build_empty_sequences();
    auto s = settings_parse(data);
    EXPECT_EQ(s.header_flags, 1u);
    EXPECT_EQ(s.model_filename_len, 0u);
    EXPECT_EQ(s.unk_c, 0u);
    EXPECT_EQ(s.unk_d, 0u);
    EXPECT_EQ(s.unk_e, 0xFFFFFFFFu);
    EXPECT_EQ(s.unk_f, 0u);
    EXPECT_EQ(s.section_type_sequences, 1u);
}

TEST(Settings, ParseZeroSequences) {
    auto data = build_empty_sequences();
    auto s = settings_parse(data);
    EXPECT_EQ(s.sequences.size(), 0u);
    EXPECT_EQ(s.section_type_groups, 4u);
    EXPECT_EQ(s.group_count, 0u);
    EXPECT_EQ(s.animations.size(), 0u);
}

TEST(Settings, ParseOneSequence) {
    auto data = build_one_sequence();
    auto s = settings_parse(data);

    ASSERT_EQ(s.sequences.size(), 1u);
    EXPECT_EQ(s.sequences[0].name, "test-obj_idle");
    EXPECT_EQ(s.sequences[0].entry_type, 3u);
    EXPECT_EQ(s.sequences[0].entry_id, 0u);

    // Animation table should have 1 entry
    ASSERT_EQ(s.animations.size(), 1u);
    EXPECT_EQ(s.animations[0].event_code, 0u);
    EXPECT_EQ(s.animations[0].num_transitions, 0u);
}

TEST(Settings, ParseMultipleSequences) {
    SettingsBuilder b;
    b.str8("2.3.0");
    b.header();
    b.sequences_marker();
    b.u32(3); // declared_sequence_count = 3
    b.sequence("idle",   3, 0);  // entry_type=3, entry_id=0
    b.sequence("walk",   3, 1);  // entry_type=3, entry_id=1
    b.sequence("attack", 3, 2);  // entry_type=3, entry_id=2
    b.u32(4);
    b.groups_marker();
    b.u32(0);
    b.u32(3); // animation_count = 3
    b.animation(0);
    b.animation(1);
    b.animation(2);
    for (int i = 0; i < 4; ++i) b.u8(0);

    auto s = settings_parse(b.data);
    ASSERT_EQ(s.sequences.size(), 3u);
    EXPECT_EQ(s.sequences[0].name, "idle");
    EXPECT_EQ(s.sequences[0].entry_type, 3u);
    EXPECT_EQ(s.sequences[0].entry_id, 0u);
    EXPECT_EQ(s.sequences[1].name, "walk");
    EXPECT_EQ(s.sequences[1].entry_id, 1u);
    EXPECT_EQ(s.sequences[2].name, "attack");
    EXPECT_EQ(s.sequences[2].entry_id, 2u);

    ASSERT_EQ(s.animations.size(), 3u);
}

TEST(Settings, ParseGroupEntries) {
    // Test with type=2 group entries and type=3 animation entries,
    // simulating the mf_darkling.settings structure where declared_count
    // undercounts the actual entries.
    SettingsBuilder b;
    b.str8("2.3.0");
    b.header();
    b.sequences_marker();
    b.u32(2); // declared_count = 2 (undercount - actual is 4)
    b.sequence("Combat", 2, 2);          // group with 2 animations
    b.sequence("slash-attack", 3, 100);  // animation entry_id=100
    b.sequence("slash-spin", 3, 101);    // animation entry_id=101
    b.sequence("idle", 3, 0);            // extra entry beyond declared count
    b.u32(4);
    b.groups_marker();
    b.u32(0);
    b.u32(3); // animation_count = 3 (type-3 entries only)
    b.animation(100);
    b.animation(101);
    b.animation(0);
    for (int i = 0; i < 4; ++i) b.u8(0);

    auto s = settings_parse(b.data);
    // Parser should read ALL 4 entries despite declared_count=2
    ASSERT_EQ(s.sequences.size(), 4u);
    EXPECT_EQ(s.sequences[0].name, "Combat");
    EXPECT_EQ(s.sequences[0].entry_type, 2u);
    EXPECT_EQ(s.sequences[0].entry_id, 2u);   // group has 2 animations
    EXPECT_EQ(s.sequences[1].entry_type, 3u);
    EXPECT_EQ(s.sequences[1].entry_id, 100u);
    EXPECT_EQ(s.sequences[3].name, "idle");
    EXPECT_EQ(s.sequences[3].entry_id, 0u);

    ASSERT_EQ(s.animations.size(), 3u);
    EXPECT_EQ(s.animations[0].event_code, 100u);
}

TEST(Settings, FooterBytesStored) {
    auto data = build_empty_sequences();
    auto s = settings_parse(data);
    EXPECT_EQ(s.footer_bytes.size(), 4u);
}

TEST(Settings, EmptyDataThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(settings_parse(empty), SettingsError);
}

TEST(Settings, TruncatedAfterVersionThrows) {
    SettingsBuilder b;
    b.str8("2.3.0");
    b.u32(1); // only partial header
    EXPECT_THROW(settings_parse(b.data), SettingsError);
}

TEST(Settings, WrongSequencesMarkerThrows) {
    SettingsBuilder b;
    b.str8("2.3.0");
    b.header();
    b.str8("BADMARKER"); // wrong section name
    EXPECT_THROW(settings_parse(b.data), SettingsError);
}
