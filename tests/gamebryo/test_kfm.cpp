#include <gtest/gtest.h>
#include "gamebryo/kfm/kfm_reader.h"
#include "gamebryo/kfm/kfm_writer.h"

#include <cstring>
#include <vector>

using namespace lu::assets;

namespace {

struct KfmBuilder {
    std::vector<uint8_t> data;
    void u8(uint8_t v) { data.push_back(v); }
    void u32(uint32_t v) {
        data.insert(data.end(), reinterpret_cast<uint8_t*>(&v),
                    reinterpret_cast<uint8_t*>(&v) + 4);
    }
    void s32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        u32(bits);
    }
    void raw(const std::string& s) {
        data.insert(data.end(), s.begin(), s.end());
    }
    void str(const std::string& s) { // u4_str
        u32(static_cast<uint32_t>(s.size()));
        raw(s);
    }
};

// Build a synthetic KFM: model path/root, defaults, one sequence with one
// Blend transition (extended payload) and one DefaultSync transition (no
// payload), and one sequence group with one info entry.
std::vector<uint8_t> build_kfm(const std::string& model_path) {
    KfmBuilder b;

    b.raw(";Gamebryo KFM File Version 2.2.0.0b\n");
    b.u8(1); // little-endian

    b.str(model_path);
    b.str("SceneRoot");

    b.u32(0);      // default sync transition type (Blend)
    b.u32(2);      // default non-sync transition type (Crossfade)
    b.f32(0.25f);  // default sync duration
    b.f32(0.5f);   // default non-sync duration

    b.u32(1); // num sequences
    {
        b.u32(100);                    // id
        b.str(".\\anims\\walk.kf");    // kf filename
        b.u32(0);                      // anim index
        b.u32(2);                      // num transitions
        // Transition 1: Blend (type 0) — extended payload present
        b.u32(101);   // dest id
        b.u32(0);     // stored type = Blend
        b.f32(0.3f);  // duration
        b.u32(1);     // num blend pairs
        b.str("start");
        b.str("target");
        b.u32(1);     // num chain sequences
        b.u32(102);   // chain sequence id
        b.f32(0.1f);  // chain duration
        // Transition 2: DefaultSync (type 4) — no extended payload
        b.u32(103);
        b.u32(4);
    }

    b.u32(1); // num sequence groups
    {
        b.u32(7);          // group id
        b.str("Movement"); // name
        b.u32(1);          // num sequence infos
        b.u32(100);        // sequence id
        b.s32(-1);         // priority
        b.f32(1.0f);       // weight
        b.f32(0.2f);       // ease in
        b.f32(0.2f);       // ease out
        b.u32(100);        // sync sequence id
    }

    return b.data;
}

} // anonymous namespace

TEST(KFM, ParseFullFile) {
    auto data = build_kfm(".\\models\\minifig\\body.nif");
    auto kfm = kfm_parse(data);

    EXPECT_EQ(kfm.header_line, ";Gamebryo KFM File Version 2.2.0.0b\n");
    EXPECT_EQ(kfm.endian, 1);
    EXPECT_EQ(kfm.model_path, ".\\models\\minifig\\body.nif"); // verbatim
    EXPECT_EQ(kfm.model_root, "SceneRoot");
    EXPECT_EQ(kfm.default_sync_transition_type, 0u);
    EXPECT_EQ(kfm.default_non_sync_transition_type, 2u);
    EXPECT_FLOAT_EQ(kfm.default_sync_transition_duration, 0.25f);
    EXPECT_FLOAT_EQ(kfm.default_non_sync_transition_duration, 0.5f);

    ASSERT_EQ(kfm.sequences.size(), 1u);
    const auto& seq = kfm.sequences[0];
    EXPECT_EQ(seq.id, 100u);
    EXPECT_EQ(seq.kf_filename, ".\\anims\\walk.kf");
    ASSERT_EQ(seq.transitions.size(), 2u);
    EXPECT_EQ(seq.transitions[0].stored_type, 0u);
    ASSERT_EQ(seq.transitions[0].blend_pairs.size(), 1u);
    EXPECT_EQ(seq.transitions[0].blend_pairs[0].start_key, "start");
    ASSERT_EQ(seq.transitions[0].chain_sequences.size(), 1u);
    EXPECT_EQ(seq.transitions[0].chain_sequences[0].sequence_id, 102u);
    EXPECT_EQ(seq.transitions[1].stored_type, 4u); // DefaultSync — no payload
    EXPECT_FALSE(seq.transitions[1].has_ext());

    ASSERT_EQ(kfm.sequence_groups.size(), 1u);
    EXPECT_EQ(kfm.sequence_groups[0].name, "Movement");
    ASSERT_EQ(kfm.sequence_groups[0].sequence_infos.size(), 1u);
    EXPECT_EQ(kfm.sequence_groups[0].sequence_infos[0].priority, -1);
}

TEST(KFM, RoundTripByteIdentical) {
    auto data = build_kfm(".\\models\\minifig\\body.nif");
    auto kfm = kfm_parse(data);
    auto out = kfm_write(kfm);
    EXPECT_EQ(out, data);
}

TEST(KFM, ModelPathNormalized) {
    auto data = build_kfm(".\\models\\minifig\\body.nif");
    auto kfm = kfm_parse(data);
    // On-disk field keeps backslashes; the helper normalizes for POSIX lookups.
    EXPECT_EQ(kfm.model_path_normalized(), "models/minifig/body.nif");
}

TEST(KFM, EmptyDataThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(kfm_parse(empty), KfmError);
}

TEST(KFM, NoNewlineThrows) {
    // Data without a newline in the first 256 bytes
    std::vector<uint8_t> no_newline(256, 'X');
    EXPECT_THROW(kfm_parse(no_newline), KfmError);
}

TEST(KFM, TruncatedAfterPathThrows) {
    auto data = build_kfm(".\\models\\minifig\\body.nif");
    data.resize(60); // cut inside model_root/defaults
    EXPECT_THROW(kfm_parse(data), std::exception);
}
