#include <gtest/gtest.h>
#include "netdevil/archive/pk/pk_reader.h"
#include "netdevil/archive/sd0/sd0_reader.h"

#include <cstring>
#include <zlib.h>

using namespace lu::assets;

namespace {

// Helper to build a minimal valid PK archive in memory for testing
std::vector<uint8_t> build_test_pk(
    const std::vector<std::pair<std::vector<uint8_t>, bool>>& files)
{
    std::vector<uint8_t> pk;

    // Write header
    pk.insert(pk.end(), PK_MAGIC, PK_MAGIC + PK_HEADER_SIZE);

    // Write file data and build index entries
    std::vector<PackIndexEntry> entries;
    for (size_t i = 0; i < files.size(); ++i) {
        const auto& [file_data, compress] = files[i];

        PackIndexEntry entry{};
        entry.crc = static_cast<uint32_t>(i + 1);
        entry.uncompressed_size = static_cast<uint32_t>(file_data.size());
        entry.data_offset = static_cast<uint32_t>(pk.size());

        if (compress) {
            // Build SD0 compressed data
            uLongf comp_size = compressBound(file_data.size());
            std::vector<uint8_t> comp(comp_size);
            ::compress(comp.data(), &comp_size, file_data.data(), file_data.size());

            // SD0 format: header + [u32 size] + [compressed data]
            std::vector<uint8_t> sd0;
            sd0.insert(sd0.end(), SD0_MAGIC, SD0_MAGIC + SD0_HEADER_SIZE);
            uint32_t cs = static_cast<uint32_t>(comp_size);
            sd0.insert(sd0.end(), reinterpret_cast<uint8_t*>(&cs),
                       reinterpret_cast<uint8_t*>(&cs) + 4);
            sd0.insert(sd0.end(), comp.data(), comp.data() + comp_size);

            entry.compressed_size = static_cast<uint32_t>(sd0.size());
            entry.is_compressed = 1;
            pk.insert(pk.end(), sd0.begin(), sd0.end());
        } else {
            entry.compressed_size = 0;
            entry.is_compressed = 0;
            pk.insert(pk.end(), file_data.begin(), file_data.end());
        }

        // Data divider
        pk.insert(pk.end(), PK_DATA_DIVIDER, PK_DATA_DIVIDER + 5);
        entries.push_back(entry);
    }

    // Write TOC
    uint32_t toc_offset = static_cast<uint32_t>(pk.size());
    uint32_t num_entries = static_cast<uint32_t>(entries.size());
    pk.insert(pk.end(), reinterpret_cast<uint8_t*>(&num_entries),
              reinterpret_cast<uint8_t*>(&num_entries) + 4);

    for (auto& entry : entries) {
        pk.insert(pk.end(), reinterpret_cast<uint8_t*>(&entry),
                  reinterpret_cast<uint8_t*>(&entry) + sizeof(PackIndexEntry));
    }

    // Write TOC offset and file revision
    pk.insert(pk.end(), reinterpret_cast<uint8_t*>(&toc_offset),
              reinterpret_cast<uint8_t*>(&toc_offset) + 4);
    uint32_t revision = 0;
    pk.insert(pk.end(), reinterpret_cast<uint8_t*>(&revision),
              reinterpret_cast<uint8_t*>(&revision) + 4);

    return pk;
}

} // anonymous namespace

TEST(PK, EntrySize) {
    EXPECT_EQ(sizeof(PackIndexEntry), 100u);
}

TEST(PK, OpenEmpty) {
    auto pk_data = build_test_pk({});
    PkArchive pk(pk_data);
    EXPECT_EQ(pk.entry_count(), 0u);
}

TEST(PK, ExtractUncompressed) {
    std::vector<uint8_t> file_content = {1, 2, 3, 4, 5};
    auto pk_data = build_test_pk({{file_content, false}});

    PkArchive pk(pk_data);
    EXPECT_EQ(pk.entry_count(), 1u);

    auto extracted = pk.extract(0);
    EXPECT_EQ(extracted, file_content);
}

TEST(PK, ExtractCompressed) {
    std::string text = "This is test data that should be SD0 compressed in the PK archive.";
    std::vector<uint8_t> file_content(text.begin(), text.end());
    auto pk_data = build_test_pk({{file_content, true}});

    PkArchive pk(pk_data);
    EXPECT_EQ(pk.entry_count(), 1u);
    EXPECT_EQ(pk.entry(0).is_compressed, 1u);

    auto extracted = pk.extract(0);
    EXPECT_EQ(extracted, file_content);
}

TEST(PK, MultipleEntries) {
    std::vector<uint8_t> f1 = {10, 20, 30};
    std::vector<uint8_t> f2 = {40, 50, 60, 70};
    std::string f3_text = "compressed file content here";
    std::vector<uint8_t> f3(f3_text.begin(), f3_text.end());

    auto pk_data = build_test_pk({
        {f1, false},
        {f2, false},
        {f3, true}
    });

    PkArchive pk(pk_data);
    EXPECT_EQ(pk.entry_count(), 3u);

    EXPECT_EQ(pk.extract(0), f1);
    EXPECT_EQ(pk.extract(1), f2);
    EXPECT_EQ(pk.extract(2), f3);
}

TEST(PK, FindByCrc) {
    std::vector<uint8_t> f1 = {1};
    auto pk_data = build_test_pk({{f1, false}});
    PkArchive pk(pk_data);

    auto* found = pk.find_by_crc(1);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->crc, 1u);

    EXPECT_EQ(pk.find_by_crc(999), nullptr);
}

TEST(PK, InvalidMagicThrows) {
    std::vector<uint8_t> bad_data(100, 0);
    EXPECT_THROW(PkArchive{bad_data}, PkError);
}

TEST(PK, TooSmallThrows) {
    std::vector<uint8_t> tiny = {0x6E, 0x64};
    EXPECT_THROW(PkArchive{tiny}, PkError);
}
