#include "havok/tagged/hkx_tagged_binary_writer.h"
#include "common/binary_writer/binary_writer.h"

#include <unordered_map>

namespace lu::assets {

namespace {

constexpr uint8_t kBaseTypeMask = 0x0F;
constexpr uint8_t kObjectBaseType = 8;
constexpr uint8_t kStructBaseType = 9;
constexpr uint8_t kTupleFlag = 32; // 0x20

// Canonical (minimal-length) encoder for the tagged-binary varint scheme: 6
// bits + sign in the first byte, 7 bits per byte thereafter, MSB-per-byte
// continuation flag. See hkx_tagged_binary_reader.cpp's read_tagged_varint for
// the decode side and the corpus-wide canonical-encoding verification that
// licenses always emitting the minimal-length form here (every varint in the
// header/type-table region of all 823 real files already used the minimal
// form, so re-encoding this way reproduces the originals exactly).
void write_tagged_varint(BinaryWriter& w, int32_t value) {
    bool negative = value < 0;
    uint32_t uv = negative ? static_cast<uint32_t>(-static_cast<int64_t>(value))
                            : static_cast<uint32_t>(value);
    uint8_t first = (negative ? 1 : 0) | static_cast<uint8_t>((uv & 0x3F) << 1);
    uv >>= 6;
    if (uv == 0) {
        w.write_u8(first);
        return;
    }
    w.write_u8(first | 0x80);
    while (true) {
        uint8_t b = static_cast<uint8_t>(uv & 0x7F);
        uv >>= 7;
        if (uv == 0) {
            w.write_u8(b);
            break;
        }
        w.write_u8(b | 0x80);
    }
}

// Writes a pool string using the same greedy "first occurrence wins" pool
// simulation as the reader: if this exact string was already written
// (including the two pre-seeded empty-string slots), emit a backreference
// varint (-index); otherwise emit the byte length + bytes and register it as
// a new pool entry. This must track the reader's pool state 1:1 -- same
// pre-seed, same append-order -- for the output to match byte-for-byte, since
// 823/823 real files use backreferences pervasively in the type table (every
// file reuses type names as member className values many times over).
void write_pool_string(BinaryWriter& w, const std::string& s,
                        std::unordered_map<std::string, int32_t>& pool_index,
                        int32_t& next_index) {
    auto it = pool_index.find(s);
    if (it != pool_index.end()) {
        write_tagged_varint(w, -it->second);
        return;
    }
    write_tagged_varint(w, static_cast<int32_t>(s.size()));
    w.write_bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    pool_index.emplace(s, next_index);
    ++next_index;
}

void write_member(BinaryWriter& w, const HkxTaggedMember& m,
                   std::unordered_map<std::string, int32_t>& pool_index, int32_t& next_index) {
    write_pool_string(w, m.name, pool_index, next_index);
    write_tagged_varint(w, static_cast<int32_t>(m.type));
    if (m.type & kTupleFlag) {
        write_tagged_varint(w, m.tupleSize);
    }
    uint8_t base = m.type & kBaseTypeMask;
    if (base == kObjectBaseType || base == kStructBaseType) {
        write_pool_string(w, m.className, pool_index, next_index);
    }
}

void write_type(BinaryWriter& w, const HkxTaggedType& t,
                 std::unordered_map<std::string, int32_t>& pool_index, int32_t& next_index) {
    write_pool_string(w, t.name, pool_index, next_index);
    write_tagged_varint(w, t.version);
    write_tagged_varint(w, t.parentIndex);
    write_tagged_varint(w, static_cast<int32_t>(t.members.size()));
    for (const auto& m : t.members) {
        write_member(w, m, pool_index, next_index);
    }
}

} // namespace

std::vector<uint8_t> hkx_tagged_binary_write(const HkxTaggedBinary& file) {
    BinaryWriter w;
    w.write_u32(file.magic0);
    w.write_u32(file.magic1);

    write_tagged_varint(w, 1); // fileinfo tag
    write_tagged_varint(w, file.fileInfoVersion);

    // Pool pre-seeded with two empty strings at indices 0 and 1, matching the
    // reader's m_StringPool init (m_StringPool.push_back("") x2). next_index
    // starts at 2: the first genuinely new string appended lands at pool
    // index 2 (indices 0 and 1 are already taken by the pre-seed), mirroring
    // std::vector::push_back onto a 2-element pool.
    //
    // Both slots decode to "" identically, but real encoders are NOT
    // indifferent between them: index 0 is apparently a reserved/"no string"
    // sentinel slot that real files never reference, while index 1 is the
    // first genuine empty-string pool entry -- confirmed by a corpus check
    // across 1704 tagged files from six additional client version dumps
    // (0.179.12, 0.185.20, 0.190.28, 1.0.8, 1.7.45, 1.9.76; the primary
    // vanilla_unpacked 823-file corpus happens to never reference an empty
    // string in this region at all, so this needed the wider sample to
    // surface): every empty-string backreference found always used index 1,
    // never index 0. `pool_index` therefore maps "" to 1, not 0, so this
    // writer reproduces that observed convention instead of an arbitrary
    // (but equally valid on decode) choice of index 0.
    std::unordered_map<std::string, int32_t> pool_index = {{"", 1}};
    int32_t next_index = 2;

    for (const auto& t : file.types) {
        write_tagged_varint(w, 2); // type-definition tag
        write_type(w, t, pool_index, next_index);
    }

    w.write_bytes(file.objectStream.data(), file.objectStream.size());

    return std::move(w.data());
}

} // namespace lu::assets
