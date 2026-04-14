#include <gtest/gtest.h>
#include "gamebryo/kfm/kfm_reader.h"

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
    void str(const std::string& s) {
        data.insert(data.end(), s.begin(), s.end());
    }
};

// Build a synthetic KFM file with a given NIF path.
std::vector<uint8_t> build_kfm(const std::string& nif_path) {
    KfmBuilder b;

    // Header line (terminated by newline)
    b.str(";Gamebryo KFM File Version 2.2.0.0b\n");

    // Unknown byte
    b.u8(1);

    // NIF path: u32 length + characters
    b.u32(static_cast<uint32_t>(nif_path.size()));
    b.str(nif_path);

    return b.data;
}

} // anonymous namespace

TEST(KFM, ParseSimplePath) {
    auto data = build_kfm("models/minifig/body.nif");
    auto kfm = kfm_parse(data);

    EXPECT_EQ(kfm.nif_path, "models/minifig/body.nif");
}

TEST(KFM, BackslashNormalization) {
    // Windows-style backslashes should be converted to forward slashes
    auto data = build_kfm("models\\minifig\\body.nif");
    auto kfm = kfm_parse(data);

    EXPECT_EQ(kfm.nif_path, "models/minifig/body.nif");
}

TEST(KFM, MixedSeparators) {
    auto data = build_kfm("models\\minifig/head.nif");
    auto kfm = kfm_parse(data);

    EXPECT_EQ(kfm.nif_path, "models/minifig/head.nif");
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
