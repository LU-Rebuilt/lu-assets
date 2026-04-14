#include <gtest/gtest.h>
#include "scaleform/gfx/gfx_reader.h"

#include <cstring>
#include <vector>

using namespace lu::assets;

namespace {

// Build a minimal GFX (uncompressed) file in memory.
// RECT: xmin=0, xmax=550, ymin=0, ymax=400 (TWIPS), nbits=11
// Bit layout: [01011 | 00000000000 | 01000100110 | 00000000000 | 00110010000]
// = 0x58 0x00 0x44 0xC0 0x00 0xC8 0x00 (7 bytes, last bit padding)
static const uint8_t RECT_BYTES[] = {0x58, 0x00, 0x44, 0xC0, 0x00, 0xC8, 0x00};

// Make a short-form tag record header (type << 6 | len, only valid when len < 63).
static uint16_t make_tag_hdr(uint16_t type, uint8_t len) {
    return static_cast<uint16_t>((type << 6) | (len & 0x3F));
}

// Build a complete uncompressed GFX body (everything after the 8-byte header).
static std::vector<uint8_t> make_body(uint16_t frame_rate_88,  // 8.8 fixed
                                       uint16_t frame_count,
                                       std::vector<std::pair<uint16_t, std::vector<uint8_t>>> tags) {
    std::vector<uint8_t> body;
    // RECT
    body.insert(body.end(), RECT_BYTES, RECT_BYTES + sizeof(RECT_BYTES));
    // FrameRate (LE u16)
    body.push_back(frame_rate_88 & 0xFF);
    body.push_back(frame_rate_88 >> 8);
    // FrameCount (LE u16)
    body.push_back(frame_count & 0xFF);
    body.push_back(frame_count >> 8);
    // Tags
    for (auto& [type, data] : tags) {
        uint16_t hdr;
        if (data.size() < 63) {
            hdr = make_tag_hdr(type, static_cast<uint8_t>(data.size()));
            body.push_back(hdr & 0xFF);
            body.push_back(hdr >> 8);
        } else {
            hdr = make_tag_hdr(type, 0x3F);
            body.push_back(hdr & 0xFF);
            body.push_back(hdr >> 8);
            uint32_t len = static_cast<uint32_t>(data.size());
            body.push_back(len & 0xFF); body.push_back((len>>8)&0xFF);
            body.push_back((len>>16)&0xFF); body.push_back(len>>24);
        }
        body.insert(body.end(), data.begin(), data.end());
    }
    // End tag (type=0, len=0)
    body.push_back(0x00); body.push_back(0x00);
    return body;
}

static std::vector<uint8_t> make_gfx(uint8_t version, uint16_t frame_rate_88,
                                      uint16_t frame_count,
                                      std::vector<std::pair<uint16_t, std::vector<uint8_t>>> tags = {}) {
    auto body = make_body(frame_rate_88, frame_count, std::move(tags));
    uint32_t total_len = static_cast<uint32_t>(8 + body.size());

    std::vector<uint8_t> file;
    file.push_back('G'); file.push_back('F'); file.push_back('X');
    file.push_back(version);
    file.push_back(total_len & 0xFF); file.push_back((total_len>>8)&0xFF);
    file.push_back((total_len>>16)&0xFF); file.push_back(total_len>>24);
    file.insert(file.end(), body.begin(), body.end());
    return file;
}

} // namespace

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

TEST(Gfx, TooSmallThrows) {
    std::vector<uint8_t> tiny(4, 0);
    EXPECT_THROW(gfx_parse(tiny), GfxError);
}

TEST(Gfx, BadMagicThrows) {
    auto gfx = make_gfx(10, 24*256, 1);
    gfx[0] = 'X'; gfx[1] = 'X'; gfx[2] = 'X'; // bad magic
    EXPECT_THROW(gfx_parse(gfx), GfxError);
}

// ---------------------------------------------------------------------------
// Header parsing
// ---------------------------------------------------------------------------

TEST(Gfx, UncompressedHeaderParsed) {
    auto gfx = make_gfx(10, 24*256, 3);
    auto f = gfx_parse(gfx);
    EXPECT_FALSE(f.is_compressed);
    EXPECT_EQ(f.swf_version, 10);
    EXPECT_EQ(f.frame_count, 3);
    // FrameRate: 24 fps = 24*256 = 6144 (8.8 fixed)
    EXPECT_EQ(f.frame_rate, 24 * 256);
}

TEST(Gfx, FWSMagicAccepted) {
    // Standard SWF magic should also parse
    auto gfx = make_gfx(10, 30*256, 1);
    gfx[0] = 'F'; gfx[1] = 'W'; gfx[2] = 'S';
    auto f = gfx_parse(gfx);
    EXPECT_FALSE(f.is_compressed);
    EXPECT_EQ(f.swf_version, 10);
}

// ---------------------------------------------------------------------------
// RECT parsing
// ---------------------------------------------------------------------------

TEST(Gfx, FrameSizeParsed) {
    // RECT bytes encode xmin=0, xmax=550, ymin=0, ymax=400 with nbits=11
    auto gfx = make_gfx(10, 30*256, 1);
    auto f = gfx_parse(gfx);
    EXPECT_EQ(f.frame_size.x_min, 0);
    EXPECT_EQ(f.frame_size.x_max, 550);
    EXPECT_EQ(f.frame_size.y_min, 0);
    EXPECT_EQ(f.frame_size.y_max, 400);
}

// ---------------------------------------------------------------------------
// Tag parsing
// ---------------------------------------------------------------------------

TEST(Gfx, MinimalFileHasEndTag) {
    // Even an empty-tag file must end with the End tag (type=0)
    auto gfx = make_gfx(10, 24*256, 1);
    auto f = gfx_parse(gfx);
    ASSERT_FALSE(f.tags.empty());
    EXPECT_EQ(f.tags.back().type, 0);
}

TEST(Gfx, TagsExtracted) {
    // FileAttributes(69) + SetBackgroundColor(9) + ShowFrame(1) + End(0)
    auto gfx = make_gfx(8, 24*256, 1, {
        {69, {0x08, 0x00, 0x00, 0x00}},   // FileAttributes
        {9,  {0xFF, 0x00, 0x00}},          // SetBackgroundColor (white)
        {1,  {}},                           // ShowFrame
    });
    auto f = gfx_parse(gfx);
    // tags: FileAttributes, SetBackgroundColor, ShowFrame, End
    ASSERT_EQ(f.tags.size(), 4u);
    EXPECT_EQ(f.tags[0].type, 69u);
    EXPECT_EQ(f.tags[0].data, (std::vector<uint8_t>{0x08,0x00,0x00,0x00}));
    EXPECT_EQ(f.tags[1].type, 9u);
    EXPECT_EQ(f.tags[1].data, (std::vector<uint8_t>{0xFF,0x00,0x00}));
    EXPECT_EQ(f.tags[2].type, 1u);
    EXPECT_TRUE(f.tags[2].data.empty());
    EXPECT_EQ(f.tags[3].type, 0u); // End
}

TEST(Gfx, LongFormTagParsed) {
    // A tag with payload >= 63 bytes requires the long-form record header
    std::vector<uint8_t> big_payload(100, 0xAB);
    auto gfx = make_gfx(8, 24*256, 1, {
        {87, big_payload},  // DefineBinaryData
    });
    auto f = gfx_parse(gfx);
    ASSERT_EQ(f.tags.size(), 2u); // DefineBinaryData + End
    EXPECT_EQ(f.tags[0].type, 87u);
    EXPECT_EQ(f.tags[0].data.size(), 100u);
    EXPECT_EQ(f.tags[0].data[0], 0xABu);
}

TEST(Gfx, ScaleformCustomTagAccepted) {
    // Scaleform extension tags (>= 1000) must parse without error
    auto gfx = make_gfx(10, 30*256, 1, {
        {1000, {0x01, 0x02, 0x03}},
        {1001, {0xAA, 0xBB}},
    });
    auto f = gfx_parse(gfx);
    ASSERT_GE(f.tags.size(), 3u);
    EXPECT_EQ(f.tags[0].type, 1000u);
    EXPECT_EQ(f.tags[1].type, 1001u);
    EXPECT_EQ(f.tags.back().type, 0u); // End
}

// ---------------------------------------------------------------------------
// Raw data coverage (no gaps)
// ---------------------------------------------------------------------------

TEST(Gfx, SwfDataStoresAllBytes) {
    auto gfx = make_gfx(10, 24*256, 1, {{69, {0x00,0x00,0x00,0x00}}});
    auto f = gfx_parse(gfx);
    // Uncompressed: swf_data == input
    EXPECT_EQ(f.swf_data, gfx);
}

// ---------------------------------------------------------------------------
// Multiple frames
// ---------------------------------------------------------------------------

TEST(Gfx, FrameCountPreserved) {
    auto gfx = make_gfx(8, 12*256, 5, { // 5 frames at 12 fps
        {1, {}}, {1, {}}, {1, {}}, {1, {}}, {1, {}},
    });
    auto f = gfx_parse(gfx);
    EXPECT_EQ(f.frame_count, 5u);
    EXPECT_EQ(f.frame_rate, 12 * 256u);
}
