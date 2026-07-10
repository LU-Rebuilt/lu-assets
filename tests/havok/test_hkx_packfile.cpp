#include <gtest/gtest.h>
#include "havok/packfile/hkx_packfile_reader.h"
#include "havok/packfile/hkx_packfile_writer.h"
#include "havok/packfile/hkx_packfile_types.h"

#include <cstring>
#include <vector>

using namespace lu::assets;

namespace {

// ============================================================================
// Minimal synthetic binary packfile builder for this test file only. Builds bytes
// matching the real layout confirmed by corpus survey (see
// src/havok/packfile/README.md): 64-byte header, numSections * 48-byte section
// header table, then each section's raw bytes contiguously with no padding. Every
// fixed-size string field (contentsVersion, sectionTag) is padded with zeros except a
// trailing 0xFF sentinel byte, matching every real sampled file.
// ============================================================================

struct RawBuilder {
    std::vector<uint8_t> buf;

    void u8(uint8_t v) { buf.push_back(v); }
    void u16(uint16_t v) { buf.resize(buf.size() + 2); memcpy(buf.data() + buf.size() - 2, &v, 2); }
    void s16(int16_t v) { buf.resize(buf.size() + 2); memcpy(buf.data() + buf.size() - 2, &v, 2); }
    void u32(uint32_t v) { buf.resize(buf.size() + 4); memcpy(buf.data() + buf.size() - 4, &v, 4); }
    void zeros(size_t n) { buf.resize(buf.size() + n, 0); }
    void bytes(const void* p, size_t n) {
        buf.insert(buf.end(), (const uint8_t*)p, (const uint8_t*)p + n);
    }
    // Fixed-size field: string + NUL padding + trailing 0xFF sentinel (real-file pattern).
    void fixed_str(const std::string& s, size_t field_size) {
        bytes(s.data(), s.size());
        zeros(field_size - s.size() - 1);
        u8(0xFF);
    }
    void set_u32(size_t offset, uint32_t v) { memcpy(buf.data() + offset, &v, 4); }
    size_t pos() const { return buf.size(); }
};

// Builds a complete minimal packfile with `num_sections` sections, each given the raw
// bytes supplied in `section_payloads` (used as the section's full [0, endOffset) span,
// i.e. no fixups — localFixupsOffset == globalFixupsOffset == ... == endOffset ==
// payload size). Good enough to exercise header + section-table + byte-slicing logic
// without needing real fixup data.
std::vector<uint8_t> build_packfile(uint32_t fileVersion, const std::string& contentsVersion,
                                     const std::vector<std::pair<std::string, std::vector<uint8_t>>>& section_payloads,
                                     uint32_t contentsSectionIndex = 0) {
    RawBuilder b;
    b.u32(0x57E0E057);
    b.u32(0x10C0C010);
    b.u32(0);            // userTag
    b.u32(fileVersion);
    b.u8(4);              // pointerSize
    b.u8(1);              // littleEndian
    b.u8(0);              // reusePaddingOptimization
    b.u8(1);              // emptyBaseClassOptimization
    b.u32(static_cast<uint32_t>(section_payloads.size()));
    b.u32(contentsSectionIndex);
    b.u32(0);              // contentsSectionOffset
    b.u32(0);              // contentsClassNameSectionIndex
    b.u32(0);              // contentsClassNameSectionOffset
    b.fixed_str(contentsVersion, 16);
    b.u32(0xFFFFFFFF);     // flags
    b.s16(-1);             // maxPredicate
    b.s16(-1);             // predicateArraySizePlusPadding

    size_t table_pos = b.pos();
    for (const auto& [tag, payload] : section_payloads) {
        (void)payload;
        b.fixed_str(tag, 20);
        b.zeros(7 * 4);
    }

    uint32_t running = static_cast<uint32_t>(b.pos());
    std::vector<uint32_t> starts;
    for (const auto& [tag, payload] : section_payloads) {
        (void)tag;
        starts.push_back(running);
        b.bytes(payload.data(), payload.size());
        running += static_cast<uint32_t>(payload.size());
    }

    for (size_t i = 0; i < section_payloads.size(); ++i) {
        size_t entry = table_pos + i * 48 + 20;
        uint32_t sz = static_cast<uint32_t>(section_payloads[i].second.size());
        b.set_u32(entry + 0, starts[i]);   // absoluteDataStart
        b.set_u32(entry + 4, sz);          // localFixupsOffset (no fixups: == end)
        b.set_u32(entry + 8, sz);          // globalFixupsOffset
        b.set_u32(entry + 12, sz);         // virtualFixupsOffset
        b.set_u32(entry + 16, sz);         // exportsOffset
        b.set_u32(entry + 20, sz);         // importsOffset
        b.set_u32(entry + 24, sz);         // endOffset
    }

    return b.buf;
}

std::vector<uint8_t> minimal_three_section_file() {
    return build_packfile(7, "Havok-7.1.0-r1", {
        {"__classnames__", {0x01, 0x02, 0x03, 0x04}},
        {"__types__", {}},
        {"__data__", {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22}},
    }, /*contentsSectionIndex=*/2);
}

} // namespace

// ============================================================================
// Header parsing
// ============================================================================

TEST(HkxPackfile, ParsesHeaderFields) {
    auto data = minimal_three_section_file();
    HkxPackfile pf = hkx_packfile_parse(data);

    EXPECT_EQ(pf.header.magic0, 0x57E0E057u);
    EXPECT_EQ(pf.header.magic1, 0x10C0C010u);
    EXPECT_EQ(pf.header.fileVersion, 7u);
    EXPECT_EQ(pf.header.pointerSize, 4);
    EXPECT_EQ(pf.header.littleEndian, 1);
    EXPECT_EQ(pf.header.numSections, 3u);
    EXPECT_EQ(pf.header.contentsSectionIndex, 2u);
    EXPECT_EQ(pf.header.contentsVersion, "Havok-7.1.0-r1");
    EXPECT_EQ(pf.header.flags, 0xFFFFFFFFu);
    EXPECT_EQ(pf.header.maxPredicate, -1);
    EXPECT_EQ(pf.header.predicateArraySizePlusPadding, -1);
}

TEST(HkxPackfile, ParsesDifferentFileVersionsAndContentsVersionStrings) {
    // Real corpus ships fileVersion 4, 5, 6, 7 with matching contents version strings.
    struct Case { uint32_t version; const char* cv; };
    const Case cases[] = {
        {4, "Havok-4.5.0-r1"}, {5, "Havok-5.1.0-r1"},
        {6, "Havok-6.5.0-r1"}, {7, "Havok-7.0.0-r1"}, {7, "Havok-7.1.0-r1"},
    };
    for (const auto& c : cases) {
        auto data = build_packfile(c.version, c.cv, {
            {"__classnames__", {}}, {"__types__", {}}, {"__data__", {1,2,3}},
        }, 2);
        HkxPackfile pf = hkx_packfile_parse(data);
        EXPECT_EQ(pf.header.fileVersion, c.version);
        EXPECT_EQ(pf.header.contentsVersion, c.cv);
    }
}

// ============================================================================
// Section parsing
// ============================================================================

TEST(HkxPackfile, ParsesSectionTagsAndData) {
    auto data = minimal_three_section_file();
    HkxPackfile pf = hkx_packfile_parse(data);

    ASSERT_EQ(pf.sections.size(), 3u);
    EXPECT_EQ(pf.sections[0].sectionTag, "__classnames__");
    EXPECT_EQ(pf.sections[1].sectionTag, "__types__");
    EXPECT_EQ(pf.sections[2].sectionTag, "__data__");

    ASSERT_EQ(pf.section_data.size(), 3u);
    std::vector<uint8_t> expected_classnames = {0x01, 0x02, 0x03, 0x04};
    EXPECT_EQ(pf.section_data[0].data, expected_classnames);
    EXPECT_TRUE(pf.section_data[1].data.empty());
    std::vector<uint8_t> expected_data = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};
    EXPECT_EQ(pf.section_data[2].data, expected_data);
}

TEST(HkxPackfile, SectionOrderCanVary) {
    // Real corpus has two observed orderings: classnames,types,data (common) and
    // classnames,data,types (rarer, but present in ~2% of files). Confirm both parse.
    auto data = build_packfile(5, "Havok-5.1.0-r1", {
        {"__classnames__", {0x09}},
        {"__data__", {0x10, 0x20}},
        {"__types__", {0x30}},
    }, /*contentsSectionIndex=*/1);
    HkxPackfile pf = hkx_packfile_parse(data);
    EXPECT_EQ(pf.sections[1].sectionTag, "__data__");
    EXPECT_EQ(pf.sections[2].sectionTag, "__types__");
}

TEST(HkxPackfile, SplitsFixupSubRegionsByOffsetFields) {
    // Build one section with a non-trivial data/localFixups/globalFixups split by hand
    // (not going through build_packfile's "all equal" shortcut) to verify hkx_packfile_parse
    // slices sub-regions using the section header's own offset fields.
    RawBuilder b;
    b.u32(0x57E0E057);
    b.u32(0x10C0C010);
    b.u32(0);
    b.u32(7);
    b.u8(4); b.u8(1); b.u8(0); b.u8(1);
    b.u32(1);           // numSections = 1
    b.u32(0);            // contentsSectionIndex
    b.u32(0);
    b.u32(0);
    b.u32(0);
    b.fixed_str("Havok-7.1.0-r1", 16);
    b.u32(0xFFFFFFFF);
    b.s16(-1);
    b.s16(-1);

    size_t table_pos = b.pos();
    b.fixed_str("__data__", 20);
    b.zeros(7 * 4);

    uint32_t sec_start = static_cast<uint32_t>(b.pos());
    // data payload: 4 bytes
    b.bytes("\xDE\xAD\xBE\xEF", 4);
    // local fixups: one 8-byte entry
    b.u32(0x1111); b.u32(0x2222);
    // global fixups: one 12-byte entry
    b.u32(0x3333); b.u32(0x4444); b.u32(0x5555);
    // virtual fixups: empty
    // exports: empty
    // imports: one byte (arbitrary, just to prove imports is sliced too)
    b.u8(0x99);

    size_t entry = table_pos + 20;
    b.set_u32(entry + 0, sec_start);   // absoluteDataStart
    b.set_u32(entry + 4, 4);            // localFixupsOffset (after 4-byte data)
    b.set_u32(entry + 8, 4 + 8);        // globalFixupsOffset (after local fixups)
    b.set_u32(entry + 12, 4 + 8 + 12);  // virtualFixupsOffset (after global fixups)
    b.set_u32(entry + 16, 4 + 8 + 12);  // exportsOffset (virtual fixups empty)
    b.set_u32(entry + 20, 4 + 8 + 12);  // importsOffset (exports empty)
    b.set_u32(entry + 24, 4 + 8 + 12 + 1); // endOffset (imports has 1 byte)

    HkxPackfile pf = hkx_packfile_parse(b.buf);
    ASSERT_EQ(pf.section_data.size(), 1u);
    const HkxSectionData& sd = pf.section_data[0];
    EXPECT_EQ(sd.data, (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
    EXPECT_EQ(sd.localFixups.size(), 8u);
    EXPECT_EQ(sd.globalFixups.size(), 12u);
    EXPECT_TRUE(sd.virtualFixups.empty());
    EXPECT_TRUE(sd.exports.empty());
    EXPECT_EQ(sd.imports, (std::vector<uint8_t>{0x99}));
}

// ============================================================================
// Round-trip
// ============================================================================

TEST(HkxPackfile, RoundTripsSyntheticFileByteForByte) {
    auto data = minimal_three_section_file();
    HkxPackfile pf = hkx_packfile_parse(data);
    auto out = hkx_packfile_write(pf);
    EXPECT_EQ(out, data);
}

TEST(HkxPackfile, RoundTripsWithFixupSubRegions) {
    // Reuses the hand-built multi-region section from SplitsFixupSubRegionsByOffsetFields,
    // this time asserting full-file byte equality after parse -> write.
    RawBuilder b;
    b.u32(0x57E0E057);
    b.u32(0x10C0C010);
    b.u32(0);
    b.u32(6);
    b.u8(4); b.u8(1); b.u8(0); b.u8(1);
    b.u32(1);
    b.u32(0);
    b.u32(0);
    b.u32(0);
    b.u32(0);
    b.fixed_str("Havok-6.5.0-r1", 16);
    b.u32(0xFFFFFFFF);
    b.s16(-1);
    b.s16(-1);

    size_t table_pos = b.pos();
    b.fixed_str("__data__", 20);
    b.zeros(7 * 4);

    uint32_t sec_start = static_cast<uint32_t>(b.pos());
    b.bytes("\xDE\xAD\xBE\xEF", 4);
    b.u32(0x1111); b.u32(0x2222);
    b.u32(0x3333); b.u32(0x4444); b.u32(0x5555);
    b.u8(0x99);

    size_t entry = table_pos + 20;
    b.set_u32(entry + 0, sec_start);
    b.set_u32(entry + 4, 4);
    b.set_u32(entry + 8, 4 + 8);
    b.set_u32(entry + 12, 4 + 8 + 12);
    b.set_u32(entry + 16, 4 + 8 + 12);
    b.set_u32(entry + 20, 4 + 8 + 12);
    b.set_u32(entry + 24, 4 + 8 + 12 + 1);

    HkxPackfile pf = hkx_packfile_parse(b.buf);
    auto out = hkx_packfile_write(pf);
    EXPECT_EQ(out, b.buf);
}

TEST(HkxPackfile, RoundTripPreservesTrailingSentinelByte) {
    // The contentsVersion/sectionTag fixed-size fields end in a 0xFF sentinel byte in
    // every real file (not zero padding) -- confirm the writer reproduces it exactly,
    // not just a same-length-different-content field.
    auto data = minimal_three_section_file();
    HkxPackfile pf = hkx_packfile_parse(data);
    auto out = hkx_packfile_write(pf);
    ASSERT_EQ(out.size(), data.size());
    EXPECT_EQ(out[40 + 15], 0xFF); // contentsVersion field's last byte
    EXPECT_EQ(out, data);
}

// ============================================================================
// Malformed-input error cases
// ============================================================================

TEST(HkxPackfile, RejectsBadMagic) {
    std::vector<uint8_t> data(64, 0);
    data[0] = 0xDE; data[1] = 0xAD; data[2] = 0xBE; data[3] = 0xEF;
    EXPECT_THROW(hkx_packfile_parse(data), HkxPackfileError);
}

TEST(HkxPackfile, RejectsTaggedBinaryMagic) {
    // 0xCAB00D1E is a real, valid HKX magic (tagged binary) but out of scope for this
    // module -- must be rejected, not silently misparsed as a packfile.
    std::vector<uint8_t> data(64, 0);
    uint32_t m0 = 0xCAB00D1E, m1 = 0xD011FACE;
    memcpy(data.data(), &m0, 4);
    memcpy(data.data() + 4, &m1, 4);
    EXPECT_THROW(hkx_packfile_parse(data), HkxPackfileError);
}

TEST(HkxPackfile, RejectsTruncatedHeader) {
    std::vector<uint8_t> data = {0x57, 0xE0, 0xE0, 0x57, 0x10, 0xC0, 0xC0, 0x10};
    EXPECT_THROW(hkx_packfile_parse(data), HkxPackfileError);
}

TEST(HkxPackfile, RejectsHostileSectionCount) {
    // numSections = 0xFFFFFFFF would try to resize a vector to billions of entries
    // without the bounded_size() guard. Build a header-only buffer with a huge count.
    RawBuilder b;
    b.u32(0x57E0E057);
    b.u32(0x10C0C010);
    b.u32(0);
    b.u32(7);
    b.u8(4); b.u8(1); b.u8(0); b.u8(1);
    b.u32(0xFFFFFFFFu); // numSections -- hostile
    b.u32(0); b.u32(0); b.u32(0); b.u32(0);
    b.fixed_str("Havok-7.1.0-r1", 16);
    b.u32(0xFFFFFFFF);
    b.s16(-1);
    b.s16(-1);
    EXPECT_THROW(hkx_packfile_parse(b.buf), HkxPackfileError);
}

TEST(HkxPackfile, RejectsSectionExtendingPastEndOfFile) {
    auto data = build_packfile(7, "Havok-7.1.0-r1", {
        {"__classnames__", {}}, {"__types__", {}}, {"__data__", {1, 2, 3, 4}},
    }, 2);
    // Corrupt the last section's endOffset to claim far more data than the file has.
    size_t table_pos = 64;
    size_t entry = table_pos + 2 * 48 + 20;
    uint32_t huge = 0x7FFFFFFF;
    memcpy(data.data() + entry + 24, &huge, 4); // endOffset
    EXPECT_THROW(hkx_packfile_parse(data), HkxPackfileError);
}

TEST(HkxPackfile, RejectsOutOfOrderSectionOffsets) {
    auto data = minimal_three_section_file();
    // Corrupt section 2 (__data__)'s globalFixupsOffset to be less than localFixupsOffset.
    size_t table_pos = 64;
    size_t entry = table_pos + 2 * 48 + 20;
    uint32_t local_off;
    memcpy(&local_off, data.data() + entry + 4, 4);
    uint32_t bad_global = 0; // less than local_off (which is 8, the __data__ payload size)
    memcpy(data.data() + entry + 8, &bad_global, 4);
    EXPECT_THROW(hkx_packfile_parse(data), HkxPackfileError);
}

TEST(HkxPackfile, WriteRejectsMismatchedSectionArrays) {
    HkxPackfile pf;
    pf.header.fileVersion = 7;
    pf.header.contentsVersion = "Havok-7.1.0-r1";
    pf.sections.resize(2);
    pf.section_data.resize(1); // mismatched on purpose
    EXPECT_THROW(hkx_packfile_write(pf), HkxPackfileError);
}
