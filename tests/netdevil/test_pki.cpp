#include "netdevil/archive/pki/pki_reader.h"
#include <gtest/gtest.h>

using namespace lu::assets;

static std::vector<uint8_t> make_pki(uint32_t version, std::vector<std::string> packs,
                                      std::vector<std::tuple<uint32_t, uint32_t>> crc_pack_pairs) {
    std::vector<uint8_t> data;
    auto write_u32 = [&](uint32_t v) {
        data.push_back(v & 0xFF);
        data.push_back((v >> 8) & 0xFF);
        data.push_back((v >> 16) & 0xFF);
        data.push_back((v >> 24) & 0xFF);
    };

    write_u32(version);
    write_u32(static_cast<uint32_t>(packs.size()));
    for (const auto& p : packs) {
        write_u32(static_cast<uint32_t>(p.size()));
        data.insert(data.end(), p.begin(), p.end());
    }
    write_u32(static_cast<uint32_t>(crc_pack_pairs.size()));
    for (const auto& [crc, pack_idx] : crc_pack_pairs) {
        write_u32(crc);
        write_u32(0); // lower_crc
        write_u32(0); // upper_crc
        write_u32(pack_idx);
        write_u32(0); // unknown
    }
    return data;
}

TEST(PKI, ParseEmpty) {
    auto data = make_pki(3, {}, {});
    auto pki = pki_parse({data.data(), data.size()});
    EXPECT_EQ(pki.version, 3u);
    EXPECT_TRUE(pki.pack_paths.empty());
    EXPECT_TRUE(pki.entries.empty());
}

TEST(PKI, ParseWithPacks) {
    auto data = make_pki(3, {"client/res/pack/primary.pk", "client/res/pack/textures.pk"}, {});
    auto pki = pki_parse({data.data(), data.size()});
    ASSERT_EQ(pki.pack_paths.size(), 2u);
    EXPECT_EQ(pki.pack_paths[0], "client/res/pack/primary.pk");
    EXPECT_EQ(pki.pack_paths[1], "client/res/pack/textures.pk");
}

TEST(PKI, CrcToPackMapping) {
    auto data = make_pki(3, {"pack0.pk", "pack1.pk"}, {{0xDEADBEEF, 0}, {0xCAFEBABE, 1}});
    auto pki = pki_parse({data.data(), data.size()});
    ASSERT_EQ(pki.crc_to_pack.size(), 2u);
    EXPECT_EQ(pki.crc_to_pack[0xDEADBEEF], 0u);
    EXPECT_EQ(pki.crc_to_pack[0xCAFEBABE], 1u);
}

TEST(PKI, BackslashNormalization) {
    auto data = make_pki(3, {"client\\res\\pack\\primary.pk"}, {});
    auto pki = pki_parse({data.data(), data.size()});
    ASSERT_EQ(pki.pack_paths.size(), 1u);
    EXPECT_EQ(pki.pack_paths[0], "client/res/pack/primary.pk");
}

TEST(PKI, InvalidVersion) {
    auto data = make_pki(99, {}, {});
    auto pki = pki_parse({data.data(), data.size()});
    EXPECT_EQ(pki.version, 99u);
    EXPECT_TRUE(pki.pack_paths.empty()); // rejected by sanity check
}

TEST(PKI, TruncatedData) {
    uint8_t data[] = {3, 0, 0, 0}; // version only, no pack count
    auto pki = pki_parse({data, sizeof(data)});
    EXPECT_EQ(pki.version, 3u);
    EXPECT_TRUE(pki.pack_paths.empty());
}
