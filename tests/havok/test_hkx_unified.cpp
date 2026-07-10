#include <gtest/gtest.h>
#include "havok/unified/hkx_reader.h"
#include "havok/unified/hkx_writer.h"

#include <cstring>
#include <vector>

using namespace lu::assets;

namespace {

// Minimal synthetic-file builders, self-contained copies of the ones in
// test_hkx_packfile.cpp / test_hkx_tagged.cpp (kept independent since those are in
// anonymous namespaces and not shareable across translation units) — just enough to
// exercise hkx_parse()'s magic-based dispatch, not full format coverage (that's
// already covered by the format-specific test files).

struct RawBuilder {
    std::vector<uint8_t> buf;
    void u8(uint8_t v) { buf.push_back(v); }
    void u32(uint32_t v) { buf.resize(buf.size() + 4); memcpy(buf.data() + buf.size() - 4, &v, 4); }
    void s16(int16_t v) { buf.resize(buf.size() + 2); memcpy(buf.data() + buf.size() - 2, &v, 2); }
    void zeros(size_t n) { buf.resize(buf.size() + n, 0); }
    void bytes(const void* p, size_t n) {
        buf.insert(buf.end(), (const uint8_t*)p, (const uint8_t*)p + n);
    }
    void fixed_str(const std::string& s, size_t field_size) {
        bytes(s.data(), s.size());
        zeros(field_size - s.size() - 1);
        u8(0xFF);
    }
    void set_u32(size_t offset, uint32_t v) { memcpy(buf.data() + offset, &v, 4); }
    size_t pos() const { return buf.size(); }

    // Canonical tagged-binary varint encoder — mirrors write_tagged_varint in
    // hkx_tagged_binary_writer.cpp (see test_hkx_tagged.cpp's RawBuilder for the same
    // duplicated-rather-than-shared helper, matching this project's precedent of
    // self-contained synthetic-file builders per test file).
    void varint(int32_t value) {
        bool negative = value < 0;
        uint32_t uv = negative ? static_cast<uint32_t>(-static_cast<int64_t>(value))
                                : static_cast<uint32_t>(value);
        uint8_t first = (negative ? 1 : 0) | static_cast<uint8_t>((uv & 0x3F) << 1);
        uv >>= 6;
        if (uv == 0) { u8(first); return; }
        u8(first | 0x80);
        while (true) {
            uint8_t b = static_cast<uint8_t>(uv & 0x7F);
            uv >>= 7;
            if (uv == 0) { u8(b); break; }
            u8(b | 0x80);
        }
    }
};

// A minimal valid packfile: 1 section, no fixups. Mirrors build_packfile() in
// test_hkx_packfile.cpp.
std::vector<uint8_t> minimal_packfile() {
    RawBuilder b;
    b.u32(0x57E0E057);
    b.u32(0x10C0C010);
    b.u32(0);              // userTag
    b.u32(7);               // fileVersion
    b.u8(4);                // pointerSize
    b.u8(1);                // littleEndian
    b.u8(0);                // reusePaddingOptimization
    b.u8(1);                // emptyBaseClassOptimization
    b.u32(1);                // numSections
    b.u32(0);                // contentsSectionIndex
    b.u32(0);                // contentsSectionOffset
    b.u32(0);                // contentsClassNameSectionIndex
    b.u32(0);                // contentsClassNameSectionOffset
    b.fixed_str("Havok-7.1.0-r1", 16);
    b.u32(0xFFFFFFFF);       // flags
    b.s16(-1);               // maxPredicate
    b.s16(-1);               // predicateArraySizePlusPadding

    size_t table_pos = b.pos();
    b.fixed_str("__data__", 20);
    b.zeros(7 * 4);

    uint32_t start = static_cast<uint32_t>(b.pos());
    std::vector<uint8_t> payload = {0xAA, 0xBB, 0xCC};
    b.bytes(payload.data(), payload.size());

    size_t entry = table_pos + 20;
    uint32_t sz = static_cast<uint32_t>(payload.size());
    b.set_u32(entry + 0, start);
    b.set_u32(entry + 4, sz);
    b.set_u32(entry + 8, sz);
    b.set_u32(entry + 12, sz);
    b.set_u32(entry + 16, sz);
    b.set_u32(entry + 20, sz);
    b.set_u32(entry + 24, sz);

    return b.buf;
}

// A minimal valid tagged-binary file: fileinfo (version 0), zero types, then a
// single-byte "object stream" (the reader only requires tags/versions to be
// varint-encoded; it never validates the object stream's own tag value — see
// hkx_tagged_binary_reader.cpp's ParseTagged()). Mirrors minimal_file() in
// test_hkx_tagged.cpp.
std::vector<uint8_t> minimal_tagged_binary() {
    RawBuilder b;
    b.u32(0xCAB00D1E);
    b.u32(0xD011FACE);
    b.varint(1);  // tag 1: fileinfo
    b.varint(0);  // fileinfo version = 0
    b.varint(3);  // root object's own tag byte -- first byte of the opaque object stream
    return b.buf;
}

} // namespace

TEST(HkxUnified, DetectsPackfileFormat) {
    auto data = minimal_packfile();
    HkxAny hkx = hkx_parse(data);
    EXPECT_TRUE(std::holds_alternative<HkxPackfile>(hkx));
    EXPECT_FALSE(std::holds_alternative<HkxTaggedBinary>(hkx));
}

TEST(HkxUnified, DetectsTaggedBinaryFormat) {
    auto data = minimal_tagged_binary();
    HkxAny hkx = hkx_parse(data);
    EXPECT_TRUE(std::holds_alternative<HkxTaggedBinary>(hkx));
    EXPECT_FALSE(std::holds_alternative<HkxPackfile>(hkx));
}

TEST(HkxUnified, RoundTripsPackfileByteForByte) {
    auto data = minimal_packfile();
    HkxAny hkx = hkx_parse(data);
    auto out = hkx_write(hkx);
    EXPECT_EQ(out, data);
}

TEST(HkxUnified, RoundTripsTaggedBinaryByteForByte) {
    auto data = minimal_tagged_binary();
    HkxAny hkx = hkx_parse(data);
    auto out = hkx_write(hkx);
    EXPECT_EQ(out, data);
}

TEST(HkxUnified, RejectsXmlMagic) {
    std::string xml = "<?xml version=\"1.0\"?><hkpackfile></hkpackfile>";
    std::vector<uint8_t> data(xml.begin(), xml.end());
    EXPECT_THROW(hkx_parse(data), HkxFormatError);
}

TEST(HkxUnified, RejectsUnrelatedData) {
    std::vector<uint8_t> data = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    EXPECT_THROW(hkx_parse(data), HkxFormatError);
}

TEST(HkxUnified, RejectsTooShortData) {
    std::vector<uint8_t> data = {0x57, 0xE0, 0xE0};
    EXPECT_THROW(hkx_parse(data), HkxFormatError);
}

TEST(HkxUnified, PropagatesUnderlyingParserErrorsForTruncatedPackfile) {
    // Right magic, but truncated before the rest of the 64-byte header — should throw
    // the packfile parser's own error type (a subclass of std::exception), not
    // HkxFormatError (which is reserved for "didn't even match a known magic").
    std::vector<uint8_t> data = {0x57, 0xE0, 0xE0, 0x57, 0x10, 0xC0, 0xC0, 0x10, 0x00};
    EXPECT_THROW(hkx_parse(data), std::exception);
    EXPECT_THROW(hkx_parse(data), HkxPackfileError);
}

TEST(HkxUnified, PropagatesUnderlyingParserErrorsForTruncatedTaggedBinary) {
    // Right magic, but the fileinfo tag that must immediately follow is missing
    // entirely (zero bytes after the 8-byte magic). Bounds failures this deep in the
    // tagged-binary reader propagate the underlying BinaryReader exception unwrapped
    // rather than HkxTaggedBinaryError -- matching test_hkx_tagged.cpp's own
    // RejectsTruncatedTypeTable, which expects std::exception for the same reason.
    // The important assertion for this dispatcher is simply that it's NOT
    // HkxFormatError (i.e. the magic was correctly recognized as tagged-binary and
    // dispatched, rather than falling through to "unrecognized format").
    std::vector<uint8_t> data = {0x1E, 0x0D, 0xB0, 0xCA, 0xCE, 0xFA, 0x11, 0xD0};
    EXPECT_THROW(hkx_parse(data), std::exception);
    EXPECT_THROW(hkx_parse(data), std::out_of_range);
}
