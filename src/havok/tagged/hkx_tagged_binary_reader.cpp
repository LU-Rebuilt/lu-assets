#include "havok/tagged/hkx_tagged_binary_reader.h"
#include "common/binary_reader/binary_reader.h"

#include <unordered_set>

namespace lu::assets {

namespace {

// Havok tagged binary format type codes (low 4 bits of a member's `type`
// byte). Matches Hkx::TaggedBaseType (havok/reader/hkx_tagged_reader.h)
// exactly -- both modules describe the same wire format -- but is redefined
// here so this module has no compile-time dependency on havok/reader/.
constexpr uint8_t kBaseTypeMask = 0x0F;
constexpr uint8_t kObjectBaseType = 8;
constexpr uint8_t kStructBaseType = 9;
constexpr uint8_t kArrayFlag = 16; // 0x10
constexpr uint8_t kTupleFlag = 32; // 0x20

// Sanity bound for count-like fields (member count, tuple size, string
// length) read before a resize()/reserve(). The real corpus's largest
// observed values are tiny (max member count 54, max string length 57 -- see
// README.md's corpus survey), so these bounds are generous rather than tight,
// just enough to reject a hostile/corrupt file's garbage count before it
// causes a multi-gigabyte allocation attempt.
constexpr int32_t kMaxReasonableCount = 100000;

// Reads a Havok tagged-binary varint: a little-endian, 6-bits-per-first-byte
// then 7-bits-per-subsequent-byte encoding with an explicit sign bit in bit 0
// of the first byte and a continuation bit in bit 7 of every byte. This is
// NOT the same shape as protobuf-style varints (sign is a dedicated low bit,
// not zigzag) -- confirmed via Ghidra RE of hkBinaryTagfileReader, and mirrors
// Hkx::HkxTaggedReader::ReadVarInt (havok/reader/hkx_tagged_reader.cpp)
// exactly, since this is a hard constraint of the wire format itself, not a
// design choice either module could vary independently.
//
// A corpus-wide check (see README.md) found this region of every real file
// (964,859 varints total, across all 823 files) uses ONLY the canonical
// (minimal-length) encoding for every value -- i.e. re-encoding the decoded
// integer with the writer's canonical encoder always reproduces the original
// bytes. That is what licenses this module to store plain int32_t fields
// (rather than raw bytes) for the header/type-table region and still
// round-trip exactly.
int32_t read_tagged_varint(BinaryReader& r) {
    uint8_t byte = r.read_u8();
    bool negative = byte & 1;
    uint32_t value = (byte & 0x7E) >> 1;
    unsigned bitpos = 6;
    while (byte & 0x80) {
        byte = r.read_u8();
        value |= static_cast<uint32_t>(byte & 0x7F) << bitpos;
        bitpos += 7;
        if (bitpos > 32) break; // matches the reader's own overflow guard
    }
    return negative ? -static_cast<int32_t>(value) : static_cast<int32_t>(value);
}

// Reads a tagged-binary pool string: a varint that is either a positive byte
// length (new string, appended to the pool) or <= 0, i.e. -index into the
// pool of previously-seen strings (backreference). Mirrors
// Hkx::HkxTaggedReader::ReadString exactly (including a length upper bound,
// since a hostile file could otherwise claim a multi-gigabyte string).
std::string read_pool_string(BinaryReader& r, std::vector<std::string>& pool) {
    int32_t length = read_tagged_varint(r);
    if (length > 0) {
        if (length > kMaxReasonableCount) {
            throw HkxTaggedBinaryError("HKX tagged binary: string length " +
                                        std::to_string(length) + " exceeds sanity bound");
        }
        auto bytes = r.read_bytes(static_cast<size_t>(length));
        std::string s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        pool.push_back(s);
        return s;
    }
    // length <= 0: pool backreference. The reader pre-seeds the pool with two
    // empty strings at indices 0 and 1 (see Parse()'s m_StringPool init in
    // hkx_tagged_reader.cpp) -- this module replicates that exactly so index
    // arithmetic matches.
    size_t idx = static_cast<size_t>(-length);
    if (idx >= pool.size()) {
        throw HkxTaggedBinaryError("HKX tagged binary: string pool backreference " +
                                    std::to_string(idx) + " out of range (pool size " +
                                    std::to_string(pool.size()) + ")");
    }
    return pool[idx];
}

HkxTaggedMember read_member(BinaryReader& r, std::vector<std::string>& pool) {
    HkxTaggedMember m;
    m.name = read_pool_string(r, pool);
    int32_t raw_type = read_tagged_varint(r);
    m.type = static_cast<uint8_t>(raw_type & 0xFF);
    if (m.type & kTupleFlag) {
        m.tupleSize = read_tagged_varint(r);
    }
    uint8_t base = m.type & kBaseTypeMask;
    if (base == kObjectBaseType || base == kStructBaseType) {
        m.className = read_pool_string(r, pool);
    }
    return m;
}

HkxTaggedType read_type(BinaryReader& r, std::vector<std::string>& pool) {
    HkxTaggedType t;
    t.name = read_pool_string(r, pool);
    t.version = read_tagged_varint(r);
    t.parentIndex = read_tagged_varint(r);
    int32_t member_count = read_tagged_varint(r);
    if (member_count < 0 || member_count > kMaxReasonableCount) {
        throw HkxTaggedBinaryError("HKX tagged binary: type '" + t.name + "' has invalid member count " +
                                    std::to_string(member_count));
    }
    t.members.reserve(static_cast<size_t>(member_count));
    for (int32_t i = 0; i < member_count; ++i) {
        t.members.push_back(read_member(r, pool));
    }
    return t;
}

} // namespace

HkxTaggedBinary hkx_tagged_binary_parse(std::span<const uint8_t> data) {
    if (data.size() < 8) {
        throw HkxTaggedBinaryError("HKX tagged binary: file smaller than the 8-byte magic");
    }

    BinaryReader r(data);
    uint32_t magic0 = r.read_u32();
    uint32_t magic1 = r.read_u32();
    if (magic0 != 0xCAB00D1E || magic1 != 0xD011FACE) {
        throw HkxTaggedBinaryError("HKX tagged binary: bad magic (not a tagged binary file)");
    }

    HkxTaggedBinary file;
    file.magic0 = magic0;
    file.magic1 = magic1;

    // String pool: pre-seeded with two empty strings, matching
    // Hkx::HkxTaggedReader::Parse()'s m_StringPool.push_back("") x2 -- see
    // read_pool_string's comment for why this matters for round-trip.
    std::vector<std::string> pool = {"", ""};

    // Fileinfo tag (wire tag 1): every one of the 823 real files has exactly
    // one of these, immediately after the magic, with version always 0 (see
    // README.md's corpus survey) -- but the value is still stored and
    // replayed rather than hardcoded, since "always 0 so far" is not the same
    // guarantee as "always 0 by format definition".
    int32_t fileinfo_tag = read_tagged_varint(r);
    if (fileinfo_tag != 1) {
        throw HkxTaggedBinaryError("HKX tagged binary: expected fileinfo tag (1) at offset 8, got " +
                                    std::to_string(fileinfo_tag));
    }
    file.fileInfoVersion = read_tagged_varint(r);

    // Type table: zero or more wire-tag-2 records, terminated by the first
    // tag that isn't 2 (which is peeked via seek-back rather than consumed,
    // since it belongs to the object stream that follows). Every real file
    // has a root object (and thus at least one more byte) after its type
    // table, but a file that legitimately ends right there (zero-length
    // object stream) is accepted rather than treated as truncated -- running
    // out of bytes while peeking the next tag just means "no object stream",
    // not "malformed".
    while (r.pos() < r.size()) {
        size_t tag_pos = r.pos();
        int32_t tag = read_tagged_varint(r);
        if (tag != 2) {
            r.seek(tag_pos);
            break;
        }
        file.types.push_back(read_type(r, pool));
    }

    // Everything from here to end of file -- starting with the root object's
    // own tag byte -- is preserved as one opaque blob. See the module-level
    // comment in hkx_tagged_binary_types.h for why: walking the object graph
    // with the same semantics as the pre-existing lossy reader leaves
    // trailing bytes unaccounted for in 716/823 real files (87%), so this
    // module does not attempt to parse it structurally at all.
    auto remaining = r.data().subspan(r.pos());
    file.objectStream.assign(remaining.begin(), remaining.end());

    return file;
}

} // namespace lu::assets
