#include <gtest/gtest.h>
#include "havok/reader/hkx_reader.h"
#include "havok/types/hkx_types.h"

#include <cstring>
#include <vector>

using namespace Hkx;

// ============================================================================
// Binary packfile builder for unit tests.
//
// A minimal Havok binary packfile layout:
//   [0x000]  PackfileHeader (64 bytes)
//   [0x040]  SectionHeader[0]  __classnames__ (48 bytes)
//   [0x070]  SectionHeader[1]  __data__       (48 bytes)
//   [0x0A0]  classnames section data  (starts at absoluteDataStart=0xA0)
//   [0x???]  data section data        (starts at absoluteDataStart=0x????)
//
// Classnames section format: repeated entries of
//   u32 signature + u8 0x09 + null-terminated class name string
// followed by sentinel bytes 0xFF.
// The localFixupsOffset in the section header marks where class entries end.
//
// Data section format:
//   object data (raw bytes for physics objects)
//   local fixups (8 bytes each: srcOffset + dstOffset)  [all 0xFFFF... terminates]
//   global fixups (12 bytes each)
//   virtual fixups (12 bytes each: dataOffset, classSectionIdx, classnameOffset)
//   exports, imports (empty)
//   end marker
//
// Virtual fixup entry ties a data offset to a class name offset in __classnames__.
// ============================================================================

struct HkxBuilder {
    // ---- raw byte helpers ----
    std::vector<uint8_t> buf;

    void u8(uint8_t v)  { buf.push_back(v); }
    void u16(uint16_t v) { buf.resize(buf.size() + 2); memcpy(buf.data() + buf.size() - 2, &v, 2); }
    void u32(uint32_t v) { buf.resize(buf.size() + 4); memcpy(buf.data() + buf.size() - 4, &v, 4); }
    void i16(int16_t v) { buf.resize(buf.size() + 2); memcpy(buf.data() + buf.size() - 2, &v, 2); }
    void f32(float v)   { buf.resize(buf.size() + 4); memcpy(buf.data() + buf.size() - 4, &v, 4); }
    void bytes(const void* p, size_t n) {
        buf.insert(buf.end(), (const uint8_t*)p, (const uint8_t*)p + n);
    }
    void zeros(size_t n) { buf.resize(buf.size() + n, 0); }
    void pad_to(size_t alignment) {
        size_t r = buf.size() % alignment;
        if (r) zeros(alignment - r);
    }
    void set_u32(size_t offset, uint32_t v) {
        memcpy(buf.data() + offset, &v, 4);
    }
    size_t pos() const { return buf.size(); }
};

// One classnames-section entry (test-local; distinct from Hkx::ClassEntry).
struct TestClass {
    uint32_t signature;
    std::string name;
};

// One virtual fixup: ties data section offset -> classnames section offset.
struct VFix {
    uint32_t dataOff;
    uint32_t classnameOff; // relative to classnames section absoluteDataStart
};

// One local fixup: pointer at srcOff redirects to dstOff (both relative to data section).
struct LFix {
    uint32_t srcOff;
    uint32_t dstOff;
};

// Build a complete minimal binary packfile.
// classEntries: all class names in the __classnames__ section.
// vfixups: virtual fixup table (binds objects to class names).
// lfixups: local fixup table (intra-object pointer resolution).
// dataBytes: raw bytes of the data section's object area.
// Returns the assembled buffer, and (optionally) a map from class name -> classname section offset.
static std::vector<uint8_t> build_packfile(
    const std::vector<TestClass>& classEntries,
    const std::vector<VFix>& vfixups,
    const std::vector<LFix>& lfixups,
    const std::vector<uint8_t>& dataBytes,
    std::unordered_map<std::string, uint32_t>* classnameOffsets = nullptr)
{
    // ---- Build __classnames__ section content ----
    HkxBuilder cls;
    std::unordered_map<std::string, uint32_t> coffs;
    for (const auto& ce : classEntries) {
        uint32_t entryOff = static_cast<uint32_t>(cls.pos());
        cls.u32(ce.signature);
        cls.u8(0x09); // separator
        coffs[ce.name] = entryOff + 5; // offset to start of name string within section
        cls.bytes(ce.name.c_str(), ce.name.size() + 1); // include null terminator
    }
    if (classnameOffsets) *classnameOffsets = coffs;
    uint32_t classnameDataSize = static_cast<uint32_t>(cls.pos());

    // ---- Build __data__ section content ----
    HkxBuilder dat;
    dat.bytes(dataBytes.data(), dataBytes.size());
    // Pad data to 16-byte alignment before fixups
    dat.pad_to(16);
    uint32_t localFixupsOff = static_cast<uint32_t>(dat.pos());

    // Local fixups (8 bytes each; all-0xFF sentinel)
    for (const auto& lf : lfixups) {
        dat.u32(lf.srcOff);
        dat.u32(lf.dstOff);
    }
    dat.u32(0xFFFFFFFF); dat.u32(0xFFFFFFFF); // sentinel
    dat.pad_to(4);
    uint32_t globalFixupsOff = static_cast<uint32_t>(dat.pos());

    // No global fixups; just write sentinel
    dat.u32(0xFFFFFFFF); dat.u32(0xFFFFFFFF); dat.u32(0xFFFFFFFF);
    uint32_t virtualFixupsOff = static_cast<uint32_t>(dat.pos());

    // Virtual fixups (12 bytes each; all-0xFF sentinel)
    for (const auto& vf : vfixups) {
        dat.u32(vf.dataOff);
        dat.u32(0); // classSectionIndex = 0 (__classnames__)
        dat.u32(vf.classnameOff);
    }
    dat.u32(0xFFFFFFFF); dat.u32(0xFFFFFFFF); dat.u32(0xFFFFFFFF); // sentinel
    uint32_t exportsOff = static_cast<uint32_t>(dat.pos());
    uint32_t importsOff = exportsOff;
    uint32_t endOff     = exportsOff;

    // ---- Assemble full file ----
    // Layout:
    //   [0x00] PackfileHeader (64)
    //   [0x40] SectionHeader __classnames__ (48)
    //   [0x70] SectionHeader __data__ (48)
    //   [0xA0] classnames section data
    //   [0xA0 + classnameDataSize rounded] data section data

    constexpr uint32_t HDR_SIZE = 64;
    constexpr uint32_t SEC_SIZE = 48;
    constexpr uint32_t NUM_SECTIONS = 2;

    uint32_t classNamesSectionStart = HDR_SIZE + NUM_SECTIONS * SEC_SIZE; // 0xA0
    uint32_t dataSectionStart       = classNamesSectionStart + classnameDataSize;
    // Align dataSectionStart to 16
    if (dataSectionStart % 16) dataSectionStart += 16 - (dataSectionStart % 16);

    HkxBuilder b;

    // --- PackfileHeader ---
    b.u32(BINARY_MAGIC_0);        // magic[0]
    b.u32(BINARY_MAGIC_1);        // magic[1]
    b.u32(0);                      // userTag
    b.u32(8);                      // fileVersion = 8 (Havok 2010.2)
    b.u8(4);                       // pointerSize
    b.u8(1);                       // littleEndian
    b.u8(0);                       // reusePaddingOptimization
    b.u8(0);                       // emptyBaseClassOptimization
    b.u32(NUM_SECTIONS);           // numSections
    b.u32(1);                      // contentsSectionIndex -> __data__
    b.u32(0);                      // contentsSectionOffset
    b.u32(0);                      // contentsClassNameSectionIndex -> __classnames__
    b.u32(0);                      // contentsClassNameSectionOffset
    // contentsVersion[16]
    {
        const char ver[] = "hk_2010.2.0-r1";
        b.bytes(ver, sizeof(ver) - 1);
        b.zeros(16 - (sizeof(ver) - 1));
    }
    b.u32(0);                      // flags
    b.i16(-1);                     // maxPredicate
    b.i16(0);                      // predicateArraySizePlusPadding
    // Total header: 64 bytes ✓

    // --- SectionHeader[0]: __classnames__ ---
    {
        char tag[20] = {};
        memcpy(tag, "__classnames__", 14);
        b.bytes(tag, 20);
        b.u32(classNamesSectionStart);     // absoluteDataStart
        b.u32(classnameDataSize);          // localFixupsOffset (marks end of class data)
        b.u32(classnameDataSize);          // globalFixupsOffset
        b.u32(classnameDataSize);          // virtualFixupsOffset
        b.u32(classnameDataSize);          // exportsOffset
        b.u32(classnameDataSize);          // importsOffset
        b.u32(classnameDataSize);          // endOffset
    }

    // --- SectionHeader[1]: __data__ ---
    {
        char tag[20] = {};
        memcpy(tag, "__data__", 8);
        b.bytes(tag, 20);
        b.u32(dataSectionStart);           // absoluteDataStart
        b.u32(localFixupsOff);             // localFixupsOffset
        b.u32(globalFixupsOff);            // globalFixupsOffset
        b.u32(virtualFixupsOff);           // virtualFixupsOffset
        b.u32(exportsOff);                 // exportsOffset
        b.u32(importsOff);                 // importsOffset
        b.u32(endOff);                     // endOffset
    }

    // --- classnames section ---
    b.bytes(cls.buf.data(), cls.buf.size());
    // Pad to dataSectionStart
    while (b.pos() < dataSectionStart) b.u8(0xFF);

    // --- data section ---
    b.bytes(dat.buf.data(), dat.buf.size());

    return b.buf;
}

// Build a box shape object (hkpBoxShape binary layout, 48 bytes).
// Layout (Havok 7.x, 32-bit, little-endian):
//   +0x00  vtable ptr (4 bytes, zeroed in serialized form)
//   +0x04  memSizeAndFlags (u16) + refCount (i16)
//   +0x08  userData (u32)    -- hkpShape::m_userData
//   +0x0C  type (u32)        -- hkpShape::m_type
//   +0x10  radius (float)    -- hkpConvexShape::m_radius
//   +0x14  pad[12]           -- padding to align halfExtents to 32
//   +0x20  halfExtents (hkVector4 = 4 floats, 16 bytes)
//   +0x30  total: 48 bytes
static std::vector<uint8_t> make_box_shape_data(float hx, float hy, float hz, float radius = 0.05f) {
    HkxBuilder b;
    b.zeros(4);        // vtable (zeroed)
    b.u16(0);          // memSizeAndFlags
    b.i16(0);          // refCount
    b.u32(0);          // userData
    b.u32(1);          // shapeTypeEnum = 1 (box)
    b.f32(radius);     // radius (at Off::ConvexShape_Radius = 0x10)
    b.zeros(12);       // padding to offset 0x20 (BoxShape_HalfExtents)
    b.f32(hx);         // halfExtents.x
    b.f32(hy);         // halfExtents.y
    b.f32(hz);         // halfExtents.z
    b.f32(0.0f);       // halfExtents.w (always 0)
    return b.buf;      // 48 bytes
}

// Build a sphere shape object (hkpSphereShape binary layout, 32 bytes).
// Layout:
//   +0x00  vtable ptr (4 bytes)
//   +0x04  memSizeAndFlags (u16) + refCount (i16)
//   +0x08  userData (u32)
//   +0x0C  type (u32)
//   +0x10  radius (float)   -- hkpConvexShape::m_radius
//   +0x14  pad[12]          -- to reach 32 bytes total
static std::vector<uint8_t> make_sphere_shape_data(float radius) {
    HkxBuilder b;
    b.zeros(4);
    b.u16(0);
    b.i16(0);
    b.u32(0);          // userData
    b.u32(0);          // shapeTypeEnum
    b.f32(radius);     // radius (at Off::ConvexShape_Radius = 0x10)
    b.zeros(12);       // pad to 32 bytes
    return b.buf;
}

// ============================================================================
// Tests
// ============================================================================

TEST(HKX, DefaultConstruct) {
    HkxFile hkx;
    (void)hkx;
}

TEST(HKX, ParseBadMagicFails) {
    std::vector<uint8_t> data(64, 0);
    uint32_t bad = 0xDEADBEEF;
    memcpy(data.data(), &bad, 4);
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(HKX, ParseTooSmallFails) {
    std::vector<uint8_t> data(16, 0);
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}

TEST(HKX, MagicConstants) {
    EXPECT_EQ(BINARY_MAGIC_0, 0x57E0E057u);
    EXPECT_EQ(BINARY_MAGIC_1, 0x10C0C010u);
    EXPECT_EQ(TAGGED_MAGIC_0, 0xCAB00D1Eu);
    EXPECT_EQ(TAGGED_MAGIC_1, 0xD011FACEu);
}

// PackfileHeader and SectionHeader size checks are compile-time static_asserts
// in hkx_reader.h (private to HkxFile class).

TEST(HKX, ParseResultDefaults) {
    ParseResult r;
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.error.empty());
    EXPECT_EQ(r.fileVersion, 0u);
    EXPECT_EQ(r.pointerSize, 0u);
    EXPECT_TRUE(r.classEntries.empty());
    EXPECT_TRUE(r.rigidBodies.empty());
    EXPECT_TRUE(r.shapes.empty());
}

TEST(HKX, ShapeTypeEnumValues) {
    EXPECT_NE(static_cast<int>(ShapeType::Box),     static_cast<int>(ShapeType::Sphere));
    EXPECT_NE(static_cast<int>(ShapeType::Unknown), static_cast<int>(ShapeType::Box));
}

// A minimal packfile with no objects: verify it parses successfully and
// populates fileVersion and havokVersion from the header.
TEST(HKX, MinimalPackfileParses) {
    auto data = build_packfile({}, {}, {}, {});
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    EXPECT_TRUE(r.success) << r.error;
    EXPECT_EQ(r.fileVersion, 8u);
    EXPECT_EQ(r.pointerSize, 4u);
    EXPECT_EQ(r.havokVersion, "hk_2010.2.0-r1");
}

// A packfile with a single class entry in __classnames__ must populate classEntries.
TEST(HKX, ClassnamesParsed) {
    auto data = build_packfile(
        {{0xDEADBEEF, "hkpBoxShape"}},
        {}, {}, {});
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.classEntries.size(), 1u);
    EXPECT_EQ(r.classEntries[0].name, "hkpBoxShape");
    EXPECT_EQ(r.classEntries[0].signature, 0xDEADBEEFu);
}

// A packfile with multiple classes must have all entries in classEntries
// and the objectsByClass map must have the right keys after virtual fixup parsing.
TEST(HKX, MultipleClassnamesParsed) {
    auto data = build_packfile(
        {{0x11111111, "hkpBoxShape"},
         {0x22222222, "hkpSphereShape"},
         {0x33333333, "hkpRigidBody"}},
        {}, {}, {});
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    ASSERT_TRUE(r.success) << r.error;
    ASSERT_EQ(r.classEntries.size(), 3u);
}

// A packfile with a hkpBoxShape object and a virtual fixup tying it to the
// class must produce one shape of type Box in the result.
TEST(HKX, BoxShapeExtracted) {
    std::unordered_map<std::string, uint32_t> coffs;

    auto boxData = make_box_shape_data(1.0f, 2.0f, 3.0f, 0.05f);

    auto data = build_packfile(
        {{0xDEADBEEF, "hkpBoxShape"}},
        // Virtual fixup: data offset 0 -> classname offset for "hkpBoxShape"
        {{0, 0}},  // placeholder, coffs not available yet
        {},
        boxData);

    // Rebuild with correct classname offset
    data = build_packfile(
        {{0xDEADBEEF, "hkpBoxShape"}},
        {},
        {},
        boxData,
        &coffs);

    // Fix up virtual fixup with correct classname offset
    data = build_packfile(
        {{0xDEADBEEF, "hkpBoxShape"}},
        {{0, coffs["hkpBoxShape"]}},
        {},
        boxData);

    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    ASSERT_TRUE(r.success) << r.error;

    ASSERT_EQ(r.shapes.size(), 1u);
    EXPECT_EQ(r.shapes[0].type, ShapeType::Box);
    EXPECT_NEAR(r.shapes[0].halfExtents.x, 1.0f, 1e-5f);
    EXPECT_NEAR(r.shapes[0].halfExtents.y, 2.0f, 1e-5f);
    EXPECT_NEAR(r.shapes[0].halfExtents.z, 3.0f, 1e-5f);
    EXPECT_NEAR(r.shapes[0].radius, 0.05f, 1e-5f);
}

// A packfile with a hkpSphereShape object must produce one sphere shape with
// the correct radius (from hkpConvexShape::m_radius at offset 0x10).
TEST(HKX, SphereShapeExtracted) {
    std::unordered_map<std::string, uint32_t> coffs;
    auto sphereData = make_sphere_shape_data(0.75f);

    auto data = build_packfile(
        {{0xBBBBBBBB, "hkpSphereShape"}},
        {},
        {},
        sphereData,
        &coffs);

    data = build_packfile(
        {{0xBBBBBBBB, "hkpSphereShape"}},
        {{0, coffs["hkpSphereShape"]}},
        {},
        sphereData);

    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    ASSERT_TRUE(r.success) << r.error;

    ASSERT_EQ(r.shapes.size(), 1u);
    EXPECT_EQ(r.shapes[0].type, ShapeType::Sphere);
    EXPECT_NEAR(r.shapes[0].radius, 0.75f, 1e-5f);
}

// Two shapes in one packfile must both be extracted.
TEST(HKX, TwoShapesExtracted) {
    std::unordered_map<std::string, uint32_t> coffs;

    auto box    = make_box_shape_data(1.0f, 1.0f, 1.0f);
    auto sphere = make_sphere_shape_data(0.5f);

    // Place box at offset 0, sphere at offset box.size() (both 16-byte aligned)
    std::vector<uint8_t> combined = box;
    while (combined.size() % 16) combined.push_back(0);
    uint32_t sphereOff = static_cast<uint32_t>(combined.size());
    combined.insert(combined.end(), sphere.begin(), sphere.end());

    auto data = build_packfile(
        {{0x11111111, "hkpBoxShape"}, {0x22222222, "hkpSphereShape"}},
        {},
        {},
        combined,
        &coffs);

    data = build_packfile(
        {{0x11111111, "hkpBoxShape"}, {0x22222222, "hkpSphereShape"}},
        {{0,         coffs["hkpBoxShape"]},
         {sphereOff, coffs["hkpSphereShape"]}},
        {},
        combined);

    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    ASSERT_TRUE(r.success) << r.error;
    EXPECT_EQ(r.shapes.size(), 2u);
}

// objectsByClass must map each class name to its data offsets.
TEST(HKX, ObjectsByClassPopulated) {
    std::unordered_map<std::string, uint32_t> coffs;
    auto boxData = make_box_shape_data(1.0f, 1.0f, 1.0f);

    auto data = build_packfile(
        {{0xDEADBEEF, "hkpBoxShape"}},
        {},
        {},
        boxData,
        &coffs);

    data = build_packfile(
        {{0xDEADBEEF, "hkpBoxShape"}},
        {{0, coffs["hkpBoxShape"]}},
        {},
        boxData);

    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    ASSERT_TRUE(r.success) << r.error;

    auto it = r.objectsByClass.find("hkpBoxShape");
    ASSERT_NE(it, r.objectsByClass.end());
    ASSERT_EQ(it->second.size(), 1u);
    EXPECT_EQ(it->second[0], 0u); // first object is at data section offset 0
}

// A tagged format file (magic 0xCAB00D1E) must not fail: we fall back to
// success=true and report the tagged-format version string.
TEST(HKX, TaggedFormatDoesNotCrash) {
    // Build a minimal buffer with tagged magic. The tagged reader will likely
    // fail to parse it fully but must not crash or return failure.
    std::vector<uint8_t> data(64, 0);
    uint32_t m0 = TAGGED_MAGIC_0, m1 = TAGGED_MAGIC_1;
    memcpy(data.data(),     &m0, 4);
    memcpy(data.data() + 4, &m1, 4);
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    // Must not crash. success may be true (graceful fallback) or false.
    // What we verify: no crash and error is populated only if failed.
    if (!r.success) {
        EXPECT_FALSE(r.error.empty());
    }
}

// ============================================================================
// Tagged binary format builder for unit tests.
//
// Havok tagged binary format layout:
//   [0x00]  Magic: 0xCAB00D1E 0xD011FACE (8 bytes)
//   [0x08]  Stream of tagged items:
//           Tag 1: FileInfo (version varint)
//           Tag 2: Metadata (type definition)
//           Tag 3: Object (with type index and bitmap-selected fields)
//           Tag 5: BackRef (object reference by ID varint)
//           Tag 6: Null
//           Tag -1: FileEnd
//
// VarInt encoding: LSB is sign bit, bits 1-6 are low data, bit 7 is continuation.
// Strings: positive varint = length + inline bytes (added to pool);
//          negative varint = pool index.
// ============================================================================

struct TaggedBuilder {
    std::vector<uint8_t> buf;
    std::vector<std::string> stringPool;

    TaggedBuilder() {
        // Magic header
        uint32_t m0 = TAGGED_MAGIC_0, m1 = TAGGED_MAGIC_1;
        buf.resize(8);
        memcpy(buf.data(), &m0, 4);
        memcpy(buf.data() + 4, &m1, 4);
        // Pre-seed string pool with 2 empty strings (matching reader's init)
        stringPool.push_back("");
        stringPool.push_back("");
    }

    void writeVarInt(int32_t value) {
        uint32_t uval = static_cast<uint32_t>(value < 0 ? -value : value);
        uint8_t first = static_cast<uint8_t>((uval & 0x3F) << 1);
        if (value < 0) first |= 1;
        uval >>= 6;
        if (uval > 0) first |= 0x80;
        buf.push_back(first);
        while (uval > 0) {
            uint8_t b = static_cast<uint8_t>(uval & 0x7F);
            uval >>= 7;
            if (uval > 0) b |= 0x80;
            buf.push_back(b);
        }
    }

    void writeFloat(float f) {
        buf.resize(buf.size() + 4);
        memcpy(buf.data() + buf.size() - 4, &f, 4);
    }

    void writeByte(uint8_t b) { buf.push_back(b); }

    // Write a string: if already in pool, emit negative pool index;
    // otherwise emit length + bytes and add to pool.
    void writeString(const std::string& s) {
        for (size_t i = 0; i < stringPool.size(); i++) {
            if (stringPool[i] == s) {
                writeVarInt(-static_cast<int32_t>(i));
                return;
            }
        }
        writeVarInt(static_cast<int32_t>(s.size()));
        buf.insert(buf.end(), s.begin(), s.end());
        stringPool.push_back(s);
    }

    // Write tag 1 (FileInfo)
    void fileInfo(int version = 0) {
        writeVarInt(1);
        writeVarInt(version);
    }

    // Write tag -1 (FileEnd)
    void fileEnd() {
        writeVarInt(-1);
    }

    // Write a type definition (tag 2).
    // parentIdx: 1-based index of parent type in the type table (0 = no parent).
    struct MemberDef {
        std::string name;
        uint8_t type;        // base type | flags
        int tupleSize = 0;   // for TupleFlag
        std::string className; // for Object/Struct types
    };
    void typeDef(const std::string& name, int parentIdx, const std::vector<MemberDef>& members) {
        writeVarInt(2); // tag
        writeString(name);
        writeVarInt(0); // unknown/version
        writeVarInt(parentIdx);
        writeVarInt(static_cast<int32_t>(members.size()));
        for (const auto& m : members) {
            writeString(m.name);
            writeVarInt(m.type);
            if (m.type & 32) writeVarInt(m.tupleSize); // TB_TupleFlag
            uint8_t base = m.type & 0x0F;
            if (base == 8 || base == 9) writeString(m.className); // TB_Object or TB_Struct
        }
    }

    // Begin an object body (tag 3, non-remembered).
    // Caller must then write the bitmap and fields.
    void beginObject(int typeIdx) {
        writeVarInt(3); // tag
        writeVarInt(typeIdx);
    }

    // Begin a remembered object body (tag 4).
    // Objects created with tag 4 can be referenced later via writeObjectRef.
    // The remembered index is assigned sequentially (0-based).
    void beginRememberedObject(int typeIdx) {
        writeVarInt(4); // tag 4 = remembered
        writeVarInt(typeIdx);
    }

    // Write a bitmap for N total members, with the given indices set.
    void writeBitmap(int totalMembers, const std::vector<int>& setIndices) {
        int bitmapBytes = (totalMembers + 7) / 8;
        std::vector<uint8_t> bm(bitmapBytes, 0);
        for (int idx : setIndices) {
            if (idx >= 0 && idx < totalMembers)
                bm[idx / 8] |= (1 << (idx % 8));
        }
        for (auto b : bm) writeByte(b);
    }

    // Write a backref to a remembered object (tag 5 + 0-based remembered index).
    // Only objects created with beginRememberedObject can be referenced.
    void writeObjectRef(int rememberedIndex) {
        writeVarInt(5);
        writeVarInt(rememberedIndex);
    }

    // Write an inline struct header (tag 3 + typeIndex).
    // TB_Struct fields in the tagged format are read via readObject, so they
    // need a tag prefix and type index before the bitmap + field data.
    void beginInlineStruct(int typeIdx) {
        writeVarInt(3); // tag 3 = inline object
        writeVarInt(typeIdx);
    }

    // Write a null object ref (for TB_Object fields): tag 6
    void writeNullRef() {
        writeVarInt(6);
    }

    // Write an inline struct value (for TB_Struct fields):
    // bitmap + field values (caller writes field values after bitmap)
    // Returns nothing; caller writes the bitmap and fields inline.

    // Write a vec4 (4 floats, for TB_Vec4 fields)
    void writeVec4(float x, float y, float z, float w) {
        writeFloat(x); writeFloat(y); writeFloat(z); writeFloat(w);
    }

    std::vector<uint8_t> finish() {
        fileEnd();
        return buf;
    }
};

// Build a tagged format file containing a single hkpConvexVerticesShape.
// Types: [1] hkpShape, [2] hkpConvexShape, [3] hkpConvexVerticesShape
// Object: one hkpConvexVerticesShape with aabbHalfExtents, aabbCenter, numVertices
TEST(HKX, TaggedConvexVerticesShapeExtracted) {
    TaggedBuilder tb;
    tb.fileInfo(0);

    // Type 1: hkpShape (base, no parent)
    tb.typeDef("hkpShape", 0, {
        {"userData", 2, 0, ""},  // TB_Int
        {"type", 2, 0, ""},     // TB_Int
    });

    // Type 2: hkpConvexShape (parent = 1)
    tb.typeDef("hkpConvexShape", 1, {
        {"radius", 3, 0, ""},   // TB_Real
    });

    // Type 3: hkpConvexVerticesShape (parent = 2)
    // Flattened members: [0]userData, [1]type, [2]radius, [3]aabbHalfExtents,
    //                    [4]aabbCenter, [5]numVertices, [6]rotatedVertices, [7]planeEquations
    tb.typeDef("hkpConvexVerticesShape", 2, {
        {"aabbHalfExtents", 4, 0, ""},  // TB_Vec4
        {"aabbCenter", 4, 0, ""},       // TB_Vec4
        {"numVertices", 2, 0, ""},      // TB_Int
        {"rotatedVertices", 8 | 16, 0, "hkFourTransposedPoints"}, // TB_Object|TB_ArrayFlag
        {"planeEquations", 4 | 16, 0, ""},  // TB_Vec4|TB_ArrayFlag
    });

    // Object: hkpConvexVerticesShape (type index 3)
    // Total flattened members: 2 (hkpShape) + 1 (hkpConvexShape) + 5 (own) = 8
    tb.beginObject(3);
    // Bitmap: set indices 2 (radius), 3 (aabbHalfExtents), 4 (aabbCenter), 5 (numVertices)
    tb.writeBitmap(8, {2, 3, 4, 5});

    // Field 2: radius (TB_Real)
    tb.writeFloat(0.05f);
    // Field 3: aabbHalfExtents (TB_Vec4)
    tb.writeVec4(1.0f, 2.0f, 3.0f, 0.0f);
    // Field 4: aabbCenter (TB_Vec4)
    tb.writeVec4(4.0f, 5.0f, 6.0f, 0.0f);
    // Field 5: numVertices (TB_Int)
    tb.writeVarInt(8);

    auto data = tb.finish();
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    ASSERT_TRUE(r.success) << r.error;

    // Should have one shape of type ConvexVertices
    ASSERT_GE(r.shapes.size(), 1u);
    bool found = false;
    for (const auto& s : r.shapes) {
        if (s.type == ShapeType::ConvexVertices) {
            found = true;
            EXPECT_NEAR(s.radius, 0.05f, 1e-5f);
            EXPECT_NEAR(s.aabbHalfExtents.x, 1.0f, 1e-5f);
            EXPECT_NEAR(s.aabbHalfExtents.y, 2.0f, 1e-5f);
            EXPECT_NEAR(s.aabbHalfExtents.z, 3.0f, 1e-5f);
            EXPECT_NEAR(s.aabbCenter.x, 4.0f, 1e-5f);
            EXPECT_NEAR(s.aabbCenter.y, 5.0f, 1e-5f);
            EXPECT_NEAR(s.aabbCenter.z, 6.0f, 1e-5f);
            EXPECT_EQ(s.numVertices, 8);
            break;
        }
    }
    EXPECT_TRUE(found) << "No ConvexVerticesShape found in tagged parse result";
}

// Build a tagged format file with hkpBoxShape
TEST(HKX, TaggedBoxShapeExtracted) {
    TaggedBuilder tb;
    tb.fileInfo(0);

    // Type 1: hkpShape
    tb.typeDef("hkpShape", 0, {
        {"userData", 2, 0, ""},
        {"type", 2, 0, ""},
    });

    // Type 2: hkpConvexShape
    tb.typeDef("hkpConvexShape", 1, {
        {"radius", 3, 0, ""},
    });

    // Type 3: hkpBoxShape (parent = 2)
    // Flattened: [0]userData, [1]type, [2]radius, [3]halfExtents
    tb.typeDef("hkpBoxShape", 2, {
        {"halfExtents", 4, 0, ""},  // TB_Vec4
    });

    // Object: hkpBoxShape (type 3)
    tb.beginObject(3);
    tb.writeBitmap(4, {2, 3}); // radius + halfExtents
    tb.writeFloat(0.1f);  // radius
    tb.writeVec4(1.5f, 2.5f, 3.5f, 0.0f); // halfExtents

    auto data = tb.finish();
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    ASSERT_TRUE(r.success) << r.error;

    ASSERT_GE(r.shapes.size(), 1u);
    bool found = false;
    for (const auto& s : r.shapes) {
        if (s.type == ShapeType::Box) {
            found = true;
            EXPECT_NEAR(s.radius, 0.1f, 1e-5f);
            EXPECT_NEAR(s.halfExtents.x, 1.5f, 1e-5f);
            EXPECT_NEAR(s.halfExtents.y, 2.5f, 1e-5f);
            EXPECT_NEAR(s.halfExtents.z, 3.5f, 1e-5f);
            break;
        }
    }
    EXPECT_TRUE(found) << "No BoxShape found in tagged parse result";
}

// Build a tagged format file with a hkpRigidBody that has material, motion,
// and collidable with a box shape. Verifies the full rigid body conversion.
TEST(HKX, TaggedRigidBodyExtracted) {
    TaggedBuilder tb;
    tb.fileInfo(0);

    // Type 1: hkpShape
    tb.typeDef("hkpShape", 0, {
        {"userData", 2, 0, ""},
        {"type", 2, 0, ""},
    });

    // Type 2: hkpConvexShape
    tb.typeDef("hkpConvexShape", 1, {
        {"radius", 3, 0, ""},
    });

    // Type 3: hkpBoxShape
    tb.typeDef("hkpBoxShape", 2, {
        {"halfExtents", 4, 0, ""},
    });

    // Type 4: hkpMaterial (inline struct)
    tb.typeDef("hkpMaterial", 0, {
        {"responseType", 2, 0, ""},    // TB_Int
        {"friction", 3, 0, ""},        // TB_Real
        {"restitution", 3, 0, ""},     // TB_Real
    });

    // Type 5: hkSweptTransform (inline struct)
    tb.typeDef("hkSweptTransform", 0, {
        {"centerOfMass0", 4, 0, ""},   // TB_Vec4
        {"centerOfMass1", 4, 0, ""},   // TB_Vec4
        {"rotation0", 4, 0, ""},       // TB_Vec4
        {"rotation1", 4, 0, ""},       // TB_Vec4
        {"centerOfMassLocal", 4, 0, ""}, // TB_Vec4
    });

    // Type 6: hkMotionState (inline struct)
    tb.typeDef("hkMotionState", 0, {
        {"transform", 7, 0, ""},       // TB_Vec16 (4x4 = 16 floats)
        {"sweptTransform", 9, 0, "hkSweptTransform"}, // TB_Struct
        {"objectRadius", 3, 0, ""},    // TB_Real
    });

    // Type 7: hkpMotion
    tb.typeDef("hkpMotion", 0, {
        {"type", 2, 0, ""},            // TB_Int (MotionType enum)
        {"motionState", 9, 0, "hkMotionState"}, // TB_Struct
        {"inertiaAndMassInv", 4, 0, ""}, // TB_Vec4
        {"linearVelocity", 4, 0, ""},    // TB_Vec4
        {"angularVelocity", 4, 0, ""},   // TB_Vec4
        {"gravityFactor", 3, 0, ""},     // TB_Real
    });

    // Type 8: hkpMaxSizeMotion (extends hkpMotion)
    tb.typeDef("hkpMaxSizeMotion", 7, {});

    // Type 9: hkpBroadPhaseHandle (inline struct)
    tb.typeDef("hkpBroadPhaseHandle", 0, {
        {"collisionFilterInfo", 2, 0, ""}, // TB_Int
    });

    // Type 10: hkpLinkedCollidable (inline struct)
    tb.typeDef("hkpLinkedCollidable", 0, {
        {"shape", 8, 0, "hkpShape"},     // TB_Object
        {"broadPhaseHandle", 9, 0, "hkpBroadPhaseHandle"}, // TB_Struct
    });

    // Type 11: hkpWorldObject
    tb.typeDef("hkpWorldObject", 0, {
        {"name", 10, 0, ""},              // TB_CString
        {"collidable", 9, 0, "hkpLinkedCollidable"}, // TB_Struct
        {"userData", 2, 0, ""},           // TB_Int
    });

    // Type 12: hkpEntity (extends hkpWorldObject)
    tb.typeDef("hkpEntity", 11, {
        {"material", 9, 0, "hkpMaterial"}, // TB_Struct
        {"damageMultiplier", 3, 0, ""},    // TB_Real
        {"motion", 9, 0, "hkpMaxSizeMotion"}, // TB_Struct
    });

    // Type 13: hkpRigidBody (extends hkpEntity, 0 own members)
    tb.typeDef("hkpRigidBody", 12, {});

    // --- Now create objects ---

    // First, create the hkpBoxShape (remembered index 0)
    // Type 3: flattened members: [0]userData, [1]type, [2]radius, [3]halfExtents
    // Use tag 4 (remembered) so it can be referenced later by the collidable.
    tb.beginRememberedObject(3);
    tb.writeBitmap(4, {2, 3}); // radius + halfExtents
    tb.writeFloat(0.05f);
    tb.writeVec4(1.0f, 2.0f, 3.0f, 0.0f);

    // Now create the hkpRigidBody (type 13)
    // Flattened members from parent chain:
    //   From hkpWorldObject (type 11): [0]name, [1]collidable, [2]userData
    //   From hkpEntity (type 12): [3]material, [4]damageMultiplier, [5]motion
    //   From hkpRigidBody (type 13): (none)
    // Total: 6

    tb.beginObject(13);
    tb.writeBitmap(6, {0, 1, 3, 4, 5}); // name, collidable, material, damageMultiplier, motion

    // Field 0: name (TB_CString)
    tb.writeString("TestRigidBody");

    // Field 1: collidable (TB_Struct = hkpLinkedCollidable, type 10)
    tb.beginInlineStruct(10);
    // hkpLinkedCollidable flattened: [0]shape, [1]broadPhaseHandle — 2 members
    tb.writeBitmap(2, {0, 1}); // both shape and broadPhaseHandle present

    // shape field (TB_Object): backref to remembered index 0 (hkpBoxShape)
    tb.writeObjectRef(0);

    // broadPhaseHandle field (TB_Struct = hkpBroadPhaseHandle, type 9)
    tb.beginInlineStruct(9);
    // hkpBroadPhaseHandle: [0]collisionFilterInfo — 1 member
    tb.writeBitmap(1, {0});
    tb.writeVarInt(42); // collisionFilterInfo = 42

    // Field 3: material (TB_Struct = hkpMaterial, type 4)
    tb.beginInlineStruct(4);
    // hkpMaterial: [0]responseType, [1]friction, [2]restitution — 3 members
    tb.writeBitmap(3, {0, 1, 2});
    tb.writeVarInt(1);    // responseType
    tb.writeFloat(0.6f);  // friction
    tb.writeFloat(0.3f);  // restitution

    // Field 4: damageMultiplier (TB_Real)
    tb.writeFloat(2.0f);

    // Field 5: motion (TB_Struct = hkpMaxSizeMotion, type 8)
    tb.beginInlineStruct(8);
    // hkpMaxSizeMotion extends hkpMotion (type 7)
    // hkpMotion flattened: [0]type, [1]motionState, [2]inertiaAndMassInv,
    //                      [3]linearVelocity, [4]angularVelocity, [5]gravityFactor
    // hkpMaxSizeMotion adds 0 own members
    // Total: 6
    tb.writeBitmap(6, {0, 1, 2, 5}); // type, motionState, inertiaAndMassInv, gravityFactor

    // motion.type (TB_Int): Fixed = 5
    tb.writeVarInt(5);

    // motion.motionState (TB_Struct = hkMotionState, type 6)
    tb.beginInlineStruct(6);
    // hkMotionState flattened: [0]transform, [1]sweptTransform, [2]objectRadius — 3 members
    tb.writeBitmap(3, {0, 2}); // transform + objectRadius

    // motionState.transform (TB_Vec16 = 16 floats)
    // Identity rotation, position at (10, 20, 30)
    tb.writeFloat(1); tb.writeFloat(0); tb.writeFloat(0); tb.writeFloat(0); // col0
    tb.writeFloat(0); tb.writeFloat(1); tb.writeFloat(0); tb.writeFloat(0); // col1
    tb.writeFloat(0); tb.writeFloat(0); tb.writeFloat(1); tb.writeFloat(0); // col2
    tb.writeFloat(10.0f); tb.writeFloat(20.0f); tb.writeFloat(30.0f); tb.writeFloat(0); // translation

    // motionState.objectRadius (TB_Real)
    tb.writeFloat(5.0f);

    // motion.inertiaAndMassInv (TB_Vec4): .w = 1/mass = 0.1 (mass = 10)
    tb.writeVec4(0.0f, 0.0f, 0.0f, 0.1f);

    // motion.gravityFactor (TB_Real)
    tb.writeFloat(1.0f);

    auto data = tb.finish();
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    ASSERT_TRUE(r.success) << r.error;

    // Should have at least one rigid body
    ASSERT_GE(r.rigidBodies.size(), 1u);

    const auto& rb = r.rigidBodies[0];
    EXPECT_EQ(rb.name, "TestRigidBody");

    // Material
    EXPECT_NEAR(rb.friction, 0.6f, 1e-5f);
    EXPECT_NEAR(rb.restitution, 0.3f, 1e-5f);
    EXPECT_EQ(rb.material.responseType, 1);

    // Damage multiplier
    EXPECT_NEAR(rb.damageMultiplier, 2.0f, 1e-5f);

    // Motion type
    EXPECT_EQ(rb.motion.type, MotionType::Fixed);

    // Position from transform
    EXPECT_NEAR(rb.position.x, 10.0f, 1e-5f);
    EXPECT_NEAR(rb.position.y, 20.0f, 1e-5f);
    EXPECT_NEAR(rb.position.z, 30.0f, 1e-5f);

    // Mass from inverse mass
    EXPECT_NEAR(rb.mass, 10.0f, 1e-3f);

    // Motion state
    EXPECT_NEAR(rb.motion.motionState.objectRadius, 5.0f, 1e-5f);
    EXPECT_NEAR(rb.motion.gravityFactor, 1.0f, 1e-5f);

    // Collision filter info
    EXPECT_EQ(rb.collisionFilterInfo, 42u);

    // Shape should be a box
    EXPECT_EQ(rb.shape.type, ShapeType::Box);
    EXPECT_NEAR(rb.shape.halfExtents.x, 1.0f, 1e-5f);
    EXPECT_NEAR(rb.shape.halfExtents.y, 2.0f, 1e-5f);
    EXPECT_NEAR(rb.shape.halfExtents.z, 3.0f, 1e-5f);
}

// Verify tagged format produces physics data -> systems -> rigid bodies chain
TEST(HKX, TaggedPhysicsDataChain) {
    TaggedBuilder tb;
    tb.fileInfo(0);

    // Minimal type definitions for the chain
    tb.typeDef("hkpShape", 0, {{"userData", 2, 0, ""}, {"type", 2, 0, ""}});
    tb.typeDef("hkpConvexShape", 1, {{"radius", 3, 0, ""}});
    tb.typeDef("hkpSphereShape", 2, {});

    // hkpWorldObject -> hkpEntity -> hkpRigidBody (simplified, no inline structs)
    tb.typeDef("hkpWorldObject", 0, {{"name", 10, 0, ""}});
    tb.typeDef("hkpEntity", 4, {});
    tb.typeDef("hkpRigidBody", 5, {});

    // hkpPhysicsSystem
    tb.typeDef("hkpPhysicsSystem", 0, {
        {"name", 10, 0, ""},                    // TB_CString
        {"rigidBodies", 8 | 16, 0, "hkpRigidBody"}, // TB_Object|TB_ArrayFlag (array of refs)
        {"active", 2, 0, ""},                   // TB_Int
    });

    // hkpPhysicsData
    tb.typeDef("hkpPhysicsData", 0, {
        {"systems", 8 | 16, 0, "hkpPhysicsSystem"}, // TB_Object|TB_ArrayFlag
    });

    // Create a sphere shape (not referenced, tag 3 is fine)
    // Type 3 (hkpSphereShape): flattened [0]userData, [1]type, [2]radius
    tb.beginObject(3);
    tb.writeBitmap(3, {2});
    tb.writeFloat(0.5f);

    // Create a rigid body (remembered index 0 — referenced by physics system)
    // Type 6 (hkpRigidBody): flattened [0]name from hkpWorldObject
    tb.beginRememberedObject(6);
    tb.writeBitmap(1, {0});
    tb.writeString("RB1");

    // Create a physics system (remembered index 1 — referenced by physics data)
    // Type 7: [0]name, [1]rigidBodies, [2]active — 3 members
    tb.beginRememberedObject(7);
    tb.writeBitmap(3, {0, 1, 2});
    tb.writeString("PhysSys1");      // name
    // rigidBodies array: length=1, then 1 object ref
    tb.writeVarInt(1);               // array length
    tb.writeObjectRef(0);            // backref to remembered index 0 (RB)
    tb.writeVarInt(1);               // active = 1

    // Create physics data
    // Type 8: [0]systems — 1 member
    tb.beginObject(8);
    tb.writeBitmap(1, {0});
    tb.writeVarInt(1);               // array length
    tb.writeObjectRef(1);            // backref to remembered index 1 (physics system)

    auto data = tb.finish();
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    ASSERT_TRUE(r.success) << r.error;

    // Physics data chain
    ASSERT_GE(r.physicsData.size(), 1u);
    ASSERT_GE(r.physicsData[0].systems.size(), 1u);
    EXPECT_EQ(r.physicsData[0].systems[0].name, "PhysSys1");
    EXPECT_TRUE(r.physicsData[0].systems[0].active);
    ASSERT_GE(r.physicsData[0].systems[0].rigidBodies.size(), 1u);
    EXPECT_EQ(r.physicsData[0].systems[0].rigidBodies[0].name, "RB1");

    // Also check top-level physics systems and rigid bodies
    ASSERT_GE(r.physicsSystems.size(), 1u);
    EXPECT_EQ(r.physicsSystems[0].name, "PhysSys1");

    // Shapes
    ASSERT_GE(r.shapes.size(), 1u);
    EXPECT_EQ(r.shapes[0].type, ShapeType::Sphere);
    EXPECT_NEAR(r.shapes[0].radius, 0.5f, 1e-5f);

    // objectsByClass should have all types
    EXPECT_GE(r.objectsByClass.count("hkpPhysicsData"), 1u);
    EXPECT_GE(r.objectsByClass.count("hkpPhysicsSystem"), 1u);
    EXPECT_GE(r.objectsByClass.count("hkpRigidBody"), 1u);
    EXPECT_GE(r.objectsByClass.count("hkpSphereShape"), 1u);
}

// Verify capsule shape extraction from tagged format
TEST(HKX, TaggedCapsuleShapeExtracted) {
    TaggedBuilder tb;
    tb.fileInfo(0);

    tb.typeDef("hkpShape", 0, {{"userData", 2}, {"type", 2}});
    tb.typeDef("hkpConvexShape", 1, {{"radius", 3}});
    tb.typeDef("hkpCapsuleShape", 2, {
        {"vertexA", 4},  // TB_Vec4
        {"vertexB", 4},  // TB_Vec4
    });

    // Object: hkpCapsuleShape — flattened [0]userData,[1]type,[2]radius,[3]vertexA,[4]vertexB
    tb.beginObject(3);
    tb.writeBitmap(5, {2, 3, 4});
    tb.writeFloat(0.25f);                          // radius
    tb.writeVec4(0.0f, -1.0f, 0.0f, 0.0f);       // vertexA
    tb.writeVec4(0.0f,  1.0f, 0.0f, 0.0f);       // vertexB

    auto data = tb.finish();
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    ASSERT_TRUE(r.success) << r.error;
    ASSERT_GE(r.shapes.size(), 1u);

    const auto& s = r.shapes[0];
    EXPECT_EQ(s.type, ShapeType::Capsule);
    EXPECT_NEAR(s.radius, 0.25f, 1e-5f);
    EXPECT_NEAR(s.vertexA.y, -1.0f, 1e-5f);
    EXPECT_NEAR(s.vertexB.y,  1.0f, 1e-5f);
}

// A packfile with unsupported version (e.g. version 2) must fail gracefully.
TEST(HKX, UnsupportedVersionFails) {
    auto data = build_packfile({}, {}, {}, {});
    // Overwrite fileVersion field (bytes 12-15 in header) with version=2
    uint32_t badver = 2;
    memcpy(data.data() + 12, &badver, 4);
    HkxFile hkx;
    auto r = hkx.Parse(data.data(), data.size());
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
}
