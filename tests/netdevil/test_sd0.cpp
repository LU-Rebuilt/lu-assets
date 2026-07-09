#include <gtest/gtest.h>
#include "netdevil/archive/sd0/sd0_reader.h"
#include "netdevil/archive/sd0/sd0_writer.h"

#include <zlib.h>
#include <cstring>

using namespace lu::assets;

TEST(SD0, IsCompressedValid) {
    uint8_t data[] = {0x73, 0x64, 0x30, 0x01, 0xFF};
    EXPECT_TRUE(sd0_is_compressed(data));
}

TEST(SD0, IsCompressedInvalid) {
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00};
    EXPECT_FALSE(sd0_is_compressed(data));
}

TEST(SD0, IsCompressedTooShort) {
    uint8_t data[] = {0x73, 0x64};
    EXPECT_FALSE(sd0_is_compressed(data));
}

TEST(SD0, DecompressEmptyPayload) {
    // Valid header but no chunks = empty output
    uint8_t data[] = {0x73, 0x64, 0x30, 0x01, 0xFF};
    auto result = sd0_decompress(data);
    EXPECT_TRUE(result.empty());
}

TEST(SD0, DecompressSingleChunk) {
    // Create a valid SD0 file with one zlib-compressed chunk
    std::string original = "Hello, LEGO Universe! This is a test of SD0 decompression.";

    // Compress with zlib
    uLongf compressed_size = compressBound(original.size());
    std::vector<uint8_t> compressed(compressed_size);
    compress(compressed.data(), &compressed_size,
             reinterpret_cast<const uint8_t*>(original.data()), original.size());

    // Build SD0 file: header + [u32 chunk_size] + [compressed data]
    std::vector<uint8_t> sd0_data;
    sd0_data.insert(sd0_data.end(), SD0_MAGIC, SD0_MAGIC + SD0_HEADER_SIZE);

    uint32_t chunk_size = static_cast<uint32_t>(compressed_size);
    sd0_data.insert(sd0_data.end(),
                    reinterpret_cast<uint8_t*>(&chunk_size),
                    reinterpret_cast<uint8_t*>(&chunk_size) + 4);
    sd0_data.insert(sd0_data.end(), compressed.data(), compressed.data() + compressed_size);

    auto result = sd0_decompress(sd0_data);
    std::string result_str(result.begin(), result.end());
    EXPECT_EQ(result_str, original);
}

TEST(SD0, DecompressMultipleChunks) {
    std::string chunk1_str = "First chunk of data for testing.";
    std::string chunk2_str = "Second chunk of data for testing.";

    // Compress each chunk
    uLongf c1_size = compressBound(chunk1_str.size());
    std::vector<uint8_t> c1(c1_size);
    compress(c1.data(), &c1_size,
             reinterpret_cast<const uint8_t*>(chunk1_str.data()), chunk1_str.size());

    uLongf c2_size = compressBound(chunk2_str.size());
    std::vector<uint8_t> c2(c2_size);
    compress(c2.data(), &c2_size,
             reinterpret_cast<const uint8_t*>(chunk2_str.data()), chunk2_str.size());

    // Build SD0 file with two chunks
    std::vector<uint8_t> sd0_data;
    sd0_data.insert(sd0_data.end(), SD0_MAGIC, SD0_MAGIC + SD0_HEADER_SIZE);

    uint32_t size1 = static_cast<uint32_t>(c1_size);
    sd0_data.insert(sd0_data.end(), reinterpret_cast<uint8_t*>(&size1),
                    reinterpret_cast<uint8_t*>(&size1) + 4);
    sd0_data.insert(sd0_data.end(), c1.data(), c1.data() + c1_size);

    uint32_t size2 = static_cast<uint32_t>(c2_size);
    sd0_data.insert(sd0_data.end(), reinterpret_cast<uint8_t*>(&size2),
                    reinterpret_cast<uint8_t*>(&size2) + 4);
    sd0_data.insert(sd0_data.end(), c2.data(), c2.data() + c2_size);

    auto result = sd0_decompress(sd0_data);
    std::string result_str(result.begin(), result.end());
    EXPECT_EQ(result_str, chunk1_str + chunk2_str);
}

TEST(SD0, InvalidMagicThrows) {
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00};
    EXPECT_THROW(sd0_decompress(data), Sd0Error);
}

TEST(SD0, TruncatedChunkSizeThrows) {
    // Valid header but only 2 bytes of chunk size (need 4)
    uint8_t data[] = {0x73, 0x64, 0x30, 0x01, 0xFF, 0x04, 0x00};
    EXPECT_THROW(sd0_decompress(data), Sd0Error);
}

TEST(SD0, ChunkSizeExceedsDataThrows) {
    // Header + chunk size says 100 bytes but only 2 bytes follow
    uint8_t data[] = {0x73, 0x64, 0x30, 0x01, 0xFF, 0x64, 0x00, 0x00, 0x00, 0xAA, 0xBB};
    EXPECT_THROW(sd0_decompress(data), Sd0Error);
}

TEST(SD0, CompressEmptyProducesHeaderOnly) {
    auto out = sd0_compress({});
    ASSERT_EQ(out.size(), SD0_HEADER_SIZE);
    EXPECT_TRUE(sd0_is_compressed(out));
}

TEST(SD0, CompressDecompressRoundTrip) {
    std::string original = "Hello, LEGO Universe! This is a test of SD0 round-tripping.";
    std::vector<uint8_t> data(original.begin(), original.end());

    auto compressed = sd0_compress(data);
    EXPECT_TRUE(sd0_is_compressed(compressed));
    auto decompressed = sd0_decompress(compressed);
    EXPECT_EQ(decompressed, data);
}

TEST(SD0, CompressChunksAtBoundary) {
    // A payload spanning exactly two chunks (256KB each) should produce two chunk records.
    std::vector<uint8_t> data(SD0_CHUNK_SIZE + 100, 0x42);
    auto compressed = sd0_compress(data);

    size_t pos = SD0_HEADER_SIZE;
    int chunk_count = 0;
    while (pos < compressed.size()) {
        uint32_t chunk_size;
        std::memcpy(&chunk_size, compressed.data() + pos, 4);
        pos += 4 + chunk_size;
        chunk_count++;
    }
    EXPECT_EQ(chunk_count, 2);
    EXPECT_EQ(sd0_decompress(compressed), data);
}
