#include <gtest/gtest.h>
#include "havok/tagged/hkx_tagged_binary_reader.h"
#include "havok/tagged/hkx_tagged_binary_writer.h"
#include "havok/tagged/hkx_tagged_binary_types.h"

#include <cstring>
#include <unordered_map>
#include <vector>

using namespace lu::assets;

namespace {

// ============================================================================
// Minimal synthetic tagged-binary builder for this test file only. Encodes
// the same canonical varint + string-pool scheme confirmed by the corpus
// survey (see src/havok/tagged/README.md): a byte-length-prefixed pool string
// for new strings, and a negative "-index" varint for backreferences, with
// index 1 (not 0) being the real-world convention for referencing an empty
// string (see hkx_tagged_binary_writer.cpp's pool_index pre-seed comment).
// ============================================================================

struct RawBuilder {
    std::vector<uint8_t> buf;
    std::unordered_map<std::string, int32_t> pool_index = {{"", 1}};
    int32_t next_index = 2;

    void u8(uint8_t v) { buf.push_back(v); }
    void u32(uint32_t v) { buf.resize(buf.size() + 4); memcpy(buf.data() + buf.size() - 4, &v, 4); }
    void bytes(const void* p, size_t n) {
        buf.insert(buf.end(), (const uint8_t*)p, (const uint8_t*)p + n);
    }

    // Canonical tagged-binary varint encoder -- mirrors write_tagged_varint in
    // hkx_tagged_binary_writer.cpp exactly (duplicated here rather than
    // shared, matching test_hkx_packfile.cpp's precedent of a self-contained
    // synthetic builder that doesn't reach into the module's internals).
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

    void pool_string(const std::string& s) {
        auto it = pool_index.find(s);
        if (it != pool_index.end()) {
            varint(-it->second);
            return;
        }
        varint(static_cast<int32_t>(s.size()));
        bytes(s.data(), s.size());
        pool_index.emplace(s, next_index);
        ++next_index;
    }

    size_t pos() const { return buf.size(); }
};

// Builds a minimal but well-formed tagged binary file: magic, fileinfo (tag 1,
// given version), a type table (tag 2 per entry, using the given type specs),
// then an arbitrary "object stream" tail (opaque as far as this module cares).
struct MemberSpec {
    std::string name;
    uint8_t type;
    int32_t tupleSize = 0;
    std::string className;
};
struct TypeSpec {
    std::string name;
    int32_t version = 0;
    int32_t parentIndex = 0;
    std::vector<MemberSpec> members;
};

std::vector<uint8_t> build_tagged_file(int32_t fileinfo_version, const std::vector<TypeSpec>& types,
                                       const std::vector<uint8_t>& object_stream_tail) {
    RawBuilder b;
    b.u32(0xCAB00D1E);
    b.u32(0xD011FACE);
    b.varint(1); // fileinfo tag
    b.varint(fileinfo_version);
    for (const auto& t : types) {
        b.varint(2); // type-definition tag
        b.pool_string(t.name);
        b.varint(t.version);
        b.varint(t.parentIndex);
        b.varint(static_cast<int32_t>(t.members.size()));
        for (const auto& m : t.members) {
            b.pool_string(m.name);
            b.varint(static_cast<int32_t>(m.type));
            if (m.type & 0x20) b.varint(m.tupleSize);
            uint8_t base = m.type & 0x0F;
            if (base == 8 || base == 9) b.pool_string(m.className);
        }
    }
    b.bytes(object_stream_tail.data(), object_stream_tail.size());
    return b.buf;
}

std::vector<uint8_t> minimal_file() {
    return build_tagged_file(0, {
        {"hkBaseObject", 0, 0, {}},
        {"hkReferencedObject", 1, 1, {
            {"memSizeAndFlags", 0x00},
            {"referenceCount", 0x00},
        }},
    }, {0x03, 0x01, 0x00, 0xAA, 0xBB, 0xCC}); // arbitrary root-object-tag(3) + opaque tail
}

} // namespace

// ============================================================================
// Header / fileinfo parsing
// ============================================================================

TEST(HkxTaggedBinary, ParsesMagicAndFileInfoVersion) {
    auto data = build_tagged_file(0, {}, {0x03});
    HkxTaggedBinary tf = hkx_tagged_binary_parse(data);
    EXPECT_EQ(tf.magic0, 0xCAB00D1Eu);
    EXPECT_EQ(tf.magic1, 0xD011FACEu);
    EXPECT_EQ(tf.fileInfoVersion, 0);
}

TEST(HkxTaggedBinary, ParsesNonZeroFileInfoVersion) {
    // Real corpus always has version 0 (823/823 files), but the field must
    // still be treated as data, not a hardcoded constant -- confirm a
    // different value round-trips.
    auto data = build_tagged_file(5, {}, {0x03});
    HkxTaggedBinary tf = hkx_tagged_binary_parse(data);
    EXPECT_EQ(tf.fileInfoVersion, 5);
}

// ============================================================================
// Type table parsing
// ============================================================================

TEST(HkxTaggedBinary, ParsesTypeTableFieldsAndMembers) {
    auto data = minimal_file();
    HkxTaggedBinary tf = hkx_tagged_binary_parse(data);

    ASSERT_EQ(tf.types.size(), 2u);
    EXPECT_EQ(tf.types[0].name, "hkBaseObject");
    EXPECT_EQ(tf.types[0].version, 0);
    EXPECT_EQ(tf.types[0].parentIndex, 0);
    EXPECT_TRUE(tf.types[0].members.empty());

    EXPECT_EQ(tf.types[1].name, "hkReferencedObject");
    EXPECT_EQ(tf.types[1].version, 1);
    EXPECT_EQ(tf.types[1].parentIndex, 1);
    ASSERT_EQ(tf.types[1].members.size(), 2u);
    EXPECT_EQ(tf.types[1].members[0].name, "memSizeAndFlags");
    EXPECT_EQ(tf.types[1].members[0].type, 0x00);
    EXPECT_EQ(tf.types[1].members[1].name, "referenceCount");
}

TEST(HkxTaggedBinary, ParsesTupleFlaggedMember) {
    auto data = build_tagged_file(0, {
        {"hkVector4f", 0, 0, {
            {"data", 0x20 | 0x03 /* TupleFlag|Real */, 4, ""},
        }},
    }, {0x03});
    HkxTaggedBinary tf = hkx_tagged_binary_parse(data);
    ASSERT_EQ(tf.types.size(), 1u);
    ASSERT_EQ(tf.types[0].members.size(), 1u);
    EXPECT_EQ(tf.types[0].members[0].tupleSize, 4);
}

TEST(HkxTaggedBinary, ParsesObjectAndStructMemberClassName) {
    auto data = build_tagged_file(0, {
        {"hkpRigidBody", 0, 0, {
            {"motion", 0x09 /* Struct */, 0, "hkpMotion"},
            {"localFrame", 0x08 /* Object */, 0, "hkLocalFrame"},
        }},
    }, {0x03});
    HkxTaggedBinary tf = hkx_tagged_binary_parse(data);
    ASSERT_EQ(tf.types[0].members.size(), 2u);
    EXPECT_EQ(tf.types[0].members[0].className, "hkpMotion");
    EXPECT_EQ(tf.types[0].members[1].className, "hkLocalFrame");
}

TEST(HkxTaggedBinary, StringPoolBackreferenceResolvesRepeatedClassName) {
    // Real files reuse type/class names heavily via backreferences (101,502
    // backreferences across the 823-file corpus' type tables) -- confirm a
    // member referencing an earlier-seen name by backreference resolves to
    // the same string, not a fresh/garbage one.
    RawBuilder b;
    b.u32(0xCAB00D1E);
    b.u32(0xD011FACE);
    b.varint(1);
    b.varint(0);
    b.varint(2);
    b.pool_string("hkReferencedObject"); // first occurrence: encoded as a fresh string
    b.varint(0);
    b.varint(0);
    b.varint(1);
    b.pool_string("variant");
    b.varint(0x08);
    b.pool_string("hkReferencedObject"); // second occurrence: must backreference
    b.varint(2);
    b.pool_string("hkxNode");
    b.varint(0);
    b.varint(0);
    b.varint(1);
    b.pool_string("owner");
    b.varint(0x08);
    b.pool_string("hkReferencedObject"); // third occurrence, different type: still backreferences
    b.varint(3); // root object tag, arbitrary tail

    HkxTaggedBinary tf = hkx_tagged_binary_parse(b.buf);
    ASSERT_EQ(tf.types.size(), 2u);
    EXPECT_EQ(tf.types[0].members[0].className, "hkReferencedObject");
    EXPECT_EQ(tf.types[1].members[0].className, "hkReferencedObject");
}

// ============================================================================
// Object stream (opaque blob) handling
// ============================================================================

TEST(HkxTaggedBinary, ObjectStreamCapturesEverythingAfterTypeTable) {
    std::vector<uint8_t> tail = {0x03, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02};
    auto data = build_tagged_file(0, {{"hkBaseObject", 0, 0, {}}}, tail);
    HkxTaggedBinary tf = hkx_tagged_binary_parse(data);
    EXPECT_EQ(tf.objectStream, tail);
}

TEST(HkxTaggedBinary, EmptyObjectStreamRoundTrips) {
    auto data = build_tagged_file(0, {{"hkBaseObject", 0, 0, {}}}, {});
    HkxTaggedBinary tf = hkx_tagged_binary_parse(data);
    EXPECT_TRUE(tf.objectStream.empty());
    auto out = hkx_tagged_binary_write(tf);
    EXPECT_EQ(out, data);
}

// ============================================================================
// Round-trip
// ============================================================================

TEST(HkxTaggedBinary, RoundTripsSyntheticFileByteForByte) {
    auto data = minimal_file();
    HkxTaggedBinary tf = hkx_tagged_binary_parse(data);
    auto out = hkx_tagged_binary_write(tf);
    EXPECT_EQ(out, data);
}

TEST(HkxTaggedBinary, RoundTripsWithBackreferencedClassNames) {
    RawBuilder b;
    b.u32(0xCAB00D1E);
    b.u32(0xD011FACE);
    b.varint(1);
    b.varint(0);
    b.varint(2);
    b.pool_string("hkReferencedObject");
    b.varint(0);
    b.varint(0);
    b.varint(1);
    b.pool_string("variant");
    b.varint(0x08);
    b.pool_string("hkReferencedObject");
    b.varint(2);
    b.pool_string("hkxNode");
    b.varint(0);
    b.varint(0);
    b.varint(1);
    b.pool_string("owner");
    b.varint(0x08);
    b.pool_string("hkReferencedObject");
    b.varint(3);
    b.bytes("\xAA\xBB\xCC", 3);

    HkxTaggedBinary tf = hkx_tagged_binary_parse(b.buf);
    auto out = hkx_tagged_binary_write(tf);
    EXPECT_EQ(out, b.buf);
}

TEST(HkxTaggedBinary, RoundTripsEmptyStringBackreferencedAtPoolIndexOne) {
    // Confirmed via a corpus check across 1704 tagged files from six
    // additional client version dumps: real encoders reference index 1 (not
    // index 0) when backreferencing an empty string, even though both
    // pre-seeded pool slots decode identically. This must reproduce that
    // exact index, not just any index that decodes to "".
    RawBuilder b;
    b.u32(0xCAB00D1E);
    b.u32(0xD011FACE);
    b.varint(1);
    b.varint(0);
    b.varint(2);
    b.pool_string("hkxScene");
    b.varint(0);
    b.varint(0);
    b.varint(1);
    b.pool_string("variant");
    b.varint(0x08); // Object, has className
    b.varint(-1);   // className backreference straight to pool index 1 (empty string)
    b.varint(3);
    b.bytes("\x00\x00", 2);

    HkxTaggedBinary tf = hkx_tagged_binary_parse(b.buf);
    ASSERT_EQ(tf.types.size(), 1u);
    EXPECT_EQ(tf.types[0].members[0].className, "");
    auto out = hkx_tagged_binary_write(tf);
    EXPECT_EQ(out, b.buf);
}

TEST(HkxTaggedBinary, RoundTripsTupleAndArrayFlaggedMembers) {
    auto data = build_tagged_file(0, {
        {"hkTest", 2, 0, {
            {"tupleField", 0x20 | 0x03, 4, ""},   // TupleFlag | Real
            {"arrayField", 0x10 | 0x02, 0, ""},    // ArrayFlag | Int
            {"objField", 0x08, 0, "hkReferencedObject"},
        }},
    }, {0x03, 0x11, 0x22, 0x33});
    HkxTaggedBinary tf = hkx_tagged_binary_parse(data);
    auto out = hkx_tagged_binary_write(tf);
    EXPECT_EQ(out, data);
}

TEST(HkxTaggedBinary, RoundTripsMultipleTypesWithVaryingVersions) {
    // hkxScene-shaped real files have per-type version varying (e.g. version
    // 1 vs the more common 0) -- confirm this isn't assumed constant.
    auto data = build_tagged_file(0, {
        {"hkBaseObject", 0, 0, {}},
        {"hkReferencedObject", 0, 1, {}},
        {"hkxScene", 1, 2, {
            {"modeller", 0x0A, 0, ""},
        }},
    }, {0x03, 0x00});
    HkxTaggedBinary tf = hkx_tagged_binary_parse(data);
    ASSERT_EQ(tf.types.size(), 3u);
    EXPECT_EQ(tf.types[2].version, 1);
    auto out = hkx_tagged_binary_write(tf);
    EXPECT_EQ(out, data);
}

// ============================================================================
// Malformed-input error cases
// ============================================================================

TEST(HkxTaggedBinary, RejectsBadMagic) {
    std::vector<uint8_t> data(16, 0);
    data[0] = 0xDE; data[1] = 0xAD; data[2] = 0xBE; data[3] = 0xEF;
    EXPECT_THROW(hkx_tagged_binary_parse(data), HkxTaggedBinaryError);
}

TEST(HkxTaggedBinary, RejectsPackfileMagic) {
    // 0x57E0E057 is a real, valid HKX magic (binary packfile) but out of
    // scope for this module -- must be rejected, not silently misparsed.
    std::vector<uint8_t> data(16, 0);
    uint32_t m0 = 0x57E0E057, m1 = 0x10C0C010;
    memcpy(data.data(), &m0, 4);
    memcpy(data.data() + 4, &m1, 4);
    EXPECT_THROW(hkx_tagged_binary_parse(data), HkxTaggedBinaryError);
}

TEST(HkxTaggedBinary, RejectsTruncatedMagic) {
    std::vector<uint8_t> data = {0x1E, 0x0D, 0xB0, 0xCA};
    EXPECT_THROW(hkx_tagged_binary_parse(data), HkxTaggedBinaryError);
}

TEST(HkxTaggedBinary, RejectsMissingFileInfoTag) {
    RawBuilder b;
    b.u32(0xCAB00D1E);
    b.u32(0xD011FACE);
    b.varint(2); // type tag instead of fileinfo(1) -- malformed
    EXPECT_THROW(hkx_tagged_binary_parse(b.buf), HkxTaggedBinaryError);
}

TEST(HkxTaggedBinary, RejectsHostileMemberCount) {
    RawBuilder b;
    b.u32(0xCAB00D1E);
    b.u32(0xD011FACE);
    b.varint(1);
    b.varint(0);
    b.varint(2);
    b.pool_string("hkEvil");
    b.varint(0);
    b.varint(0);
    b.varint(2000000000); // member count -- hostile, would try to reserve billions of entries
    EXPECT_THROW(hkx_tagged_binary_parse(b.buf), HkxTaggedBinaryError);
}

TEST(HkxTaggedBinary, RejectsHostileStringLength) {
    RawBuilder b;
    b.u32(0xCAB00D1E);
    b.u32(0xD011FACE);
    b.varint(1);
    b.varint(0);
    b.varint(2);
    b.varint(2000000000); // type name string length -- hostile
    EXPECT_THROW(hkx_tagged_binary_parse(b.buf), HkxTaggedBinaryError);
}

TEST(HkxTaggedBinary, RejectsOutOfRangeStringBackreference) {
    RawBuilder b;
    b.u32(0xCAB00D1E);
    b.u32(0xD011FACE);
    b.varint(1);
    b.varint(0);
    b.varint(2);
    b.varint(-500); // backreference far beyond the pool's current size (2)
    EXPECT_THROW(hkx_tagged_binary_parse(b.buf), HkxTaggedBinaryError);
}

TEST(HkxTaggedBinary, RejectsTruncatedTypeTable) {
    RawBuilder b;
    b.u32(0xCAB00D1E);
    b.u32(0xD011FACE);
    b.varint(1);
    b.varint(0);
    b.varint(2);
    b.pool_string("hkTruncated");
    b.varint(0);
    b.varint(0);
    b.varint(3); // claims 3 members but the buffer ends here
    EXPECT_THROW(hkx_tagged_binary_parse(b.buf), std::exception);
}

TEST(HkxTaggedBinary, RejectsFileSmallerThanMagic) {
    std::vector<uint8_t> data = {0x1E, 0x0D, 0xB0};
    EXPECT_THROW(hkx_tagged_binary_parse(data), HkxTaggedBinaryError);
}
