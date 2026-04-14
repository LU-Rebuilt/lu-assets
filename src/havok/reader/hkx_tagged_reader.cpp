// Havok Tagged Binary Format Parser
//
// Parses tagged binary HKX files (magic 0xCAB00D1E) and converts them into
// the shared ParseResult structure. This format is used by a subset of LU
// physics HKX files (the majority use binary packfile format).
//
// The tagged binary stream contains type metadata (tag 2) followed by a
// single root object (tag 3/4) that recursively contains all other objects
// as inline definitions or backreferences. The entire object graph is read
// in one recursive descent from the root hkRootLevelContainer.
//
// Key encoding details (confirmed via Ghidra RE of hkBinaryTagfileReader):
// - Vec4 arrays (type 0x14) have a varint component count prefix before
//   the float data. This is how hkxVertexBufferVertexData::vectorData and
//   hkpConvexVerticesShape::planeEquations are encoded.
// - Vec8/12/16 arrays use a static component count (baseType * 4 - 12).
// - Struct arrays use column-major layout: one shared bitmap, then for
//   each set field, ALL N elements are read before moving to the next field.
// - Non-array vec fields within struct arrays also read a varint component
//   count prefix (one prefix shared across all N elements).
//
// Known limitations:
// - Scene mesh vertex extraction from tagged format is not implemented.
// - Forward references (object referenced before defined) are not handled.
// - hkpMoppCode MOPP acceleration data not extracted.

#include "havok/reader/hkx_tagged_reader.h"

#include <cstring>
#include <cmath>
#include <algorithm>

namespace Hkx {

bool HkxTaggedReader::IsPhysicsType(const std::string& name) const {
    if (name.empty()) return false;
    if (name.compare(0, 3, "hkp") == 0) return true;
    if (name.compare(0, 2, "hk") == 0 && name.find("Motion") != std::string::npos) return true;
    if (name.compare(0, 2, "hk") == 0 && name.find("Swept") != std::string::npos) return true;
    if (name == "hkRootLevelContainer") return true;
    if (name == "hkRootLevelContainerNamedVariant") return true;
    return false;
}

// --- Stream reading ---

uint8_t HkxTaggedReader::ReadByte() {
    if (m_Pos >= m_Size) return 0;
    return m_Data[m_Pos++];
}

float HkxTaggedReader::ReadFloat() {
    if (m_Pos + 4 > m_Size) return 0;
    float val;
    std::memcpy(&val, m_Data + m_Pos, 4);
    m_Pos += 4;
    return val;
}

int32_t HkxTaggedReader::ReadVarInt() {
    if (m_Pos >= m_Size) return -1;
    uint8_t byte = ReadByte();
    bool negative = byte & 1;
    uint32_t value = (byte & 0x7E) >> 1;
    size_t bitPos = 6;
    while (byte & 0x80) {
        if (m_Pos >= m_Size) break;
        byte = ReadByte();
        value |= static_cast<uint32_t>(byte & 0x7F) << bitPos;
        bitPos += 7;
        if (bitPos > 32) break;
    }
    return negative ? -static_cast<int32_t>(value) : static_cast<int32_t>(value);
}

std::string HkxTaggedReader::ReadString() {
    int32_t length = ReadVarInt();
    if (length > 0) {
        if (length > 10000 || m_Pos + length > m_Size) {
            m_Pos = m_Size;
            return "";
        }
        std::string s(reinterpret_cast<const char*>(m_Data + m_Pos), length);
        m_Pos += length;
        m_StringPool.push_back(s);
        return s;
    } else {
        int idx = -length;
        if (idx >= 0 && idx < static_cast<int>(m_StringPool.size()))
            return m_StringPool[idx];
        return "";
    }
}

// --- Flatten type hierarchies ---

void HkxTaggedReader::FlattenType(int index, std::vector<const TaggedMember*>& out) {
    if (index <= 0 || index > static_cast<int>(m_Types.size())) return;
    auto& type = m_Types[index - 1];
    if (type.parentIndex > 0 && type.parentIndex != index)
        FlattenType(type.parentIndex, out);
    for (auto& m : type.members)
        out.push_back(&m);
}

void HkxTaggedReader::FlattenTypes() {
    for (size_t i = 0; i < m_Types.size(); i++) {
        m_Types[i].allMembers.clear();
        FlattenType(static_cast<int>(i + 1), m_Types[i].allMembers);
        m_Types[i].totalMemberCount = static_cast<int>(m_Types[i].allMembers.size());
    }
}

// --- Skip helpers ---

void HkxTaggedReader::SkipFieldValue(uint8_t baseType) {
    switch (baseType) {
    case TB_Byte: ReadByte(); break;
    case TB_Int: ReadVarInt(); break;
    case TB_Real: ReadFloat(); break;
    case TB_Vec4: case TB_Vec8: case TB_Vec12: case TB_Vec16: {
        int count = baseType * 4 - 12;
        for (int i = 0; i < count; i++) ReadFloat();
        break;
    }
    case TB_Object:
    case TB_Struct: {
        int32_t tag = ReadVarInt();
        if (tag == 5) { ReadVarInt(); }
        else if (tag == 6 || tag == 0) { }
        else if (tag == 3 || tag == 4) { ReadObjectBody(tag); }
        break;
    }
    case TB_CString: ReadString(); break;
    default: break;
    }
}

void HkxTaggedReader::ReadFileInfo() {
    ReadVarInt(); // version
}

void HkxTaggedReader::ReadMetadata() {
    TaggedType type;
    type.name = ReadString();
    ReadVarInt(); // version
    type.parentIndex = ReadVarInt();

    int32_t memberCount = ReadVarInt();
    if (memberCount < 0 || memberCount > 10000) return;

    type.members.resize(memberCount);
    for (int i = 0; i < memberCount; i++) {
        auto& m = type.members[i];
        m.name = ReadString();
        m.type = static_cast<uint8_t>(ReadVarInt());
        if (m.type & TB_TupleFlag) m.tupleSize = ReadVarInt();
        uint8_t baseType = m.type & 0x0F;
        if (baseType == TB_Object || baseType == TB_Struct) m.className = ReadString();
    }

    m_Types.push_back(std::move(type));
    m_TypeByName[m_Types.back().name] = static_cast<int>(m_Types.size());
}

void HkxTaggedReader::SkipField(const TaggedMember& member) {
    uint8_t baseType = member.type & 0x0F;
    bool isArray = member.type & TB_ArrayFlag;
    bool isTuple = member.type & TB_TupleFlag;

    if (isArray) {
        int32_t arrayLength = ReadVarInt();
        if (arrayLength <= 0) return;
        if (baseType == TB_Byte) {
            m_Pos = std::min(m_Pos + static_cast<size_t>(arrayLength), m_Size);
            return;
        }
        if (baseType == TB_Int) {
            for (int i = 0; i < arrayLength; i++) ReadVarInt();
        } else if (baseType == TB_Real) {
            m_Pos = std::min(m_Pos + static_cast<size_t>(arrayLength) * 4, m_Size);
        } else if (baseType == TB_Vec4) {
            // Vec4 arrays have a varint component count prefix
            int comp = ReadVarInt();
            if (comp < 1 || comp > 16) comp = 4;
            m_Pos = std::min(m_Pos + static_cast<size_t>(arrayLength) * comp * 4, m_Size);
        } else if (baseType == TB_Vec8 || baseType == TB_Vec12 || baseType == TB_Vec16) {
            int comp = baseType * 4 - 12;
            m_Pos = std::min(m_Pos + static_cast<size_t>(arrayLength) * comp * 4, m_Size);
        } else if (baseType == TB_Object) {
            for (int i = 0; i < arrayLength; i++) SkipFieldValue(TB_Object);
        } else if (baseType == TB_Struct) {
            int typeIdx = 0;
            auto it = m_TypeByName.find(member.className);
            if (it != m_TypeByName.end()) typeIdx = it->second;
            if (typeIdx > 0 && typeIdx <= static_cast<int>(m_Types.size())) {
                auto& type = m_Types[typeIdx - 1];
                int total = type.totalMemberCount;
                std::vector<bool> bitmap(total, false);
                int bitmapBytes = (total + 7) / 8;
                for (int i = 0; i < bitmapBytes; i++) {
                    uint8_t b = ReadByte();
                    for (int j = 0; j < 8 && i*8+j < total; j++)
                        bitmap[i*8+j] = (b >> j) & 1;
                }
                for (int f = 0; f < total; f++) {
                    if (!bitmap[f] || f >= static_cast<int>(type.allMembers.size())) continue;
                    const auto* m = type.allMembers[f];
                    bool mTuple = m->type & TB_TupleFlag;
                    bool mArray = m->type & TB_ArrayFlag;
                    uint8_t mBase = m->type & 0x0F;
                    if (mTuple) {
                        for (int e = 0; e < arrayLength; e++)
                            for (int t = 0; t < m->tupleSize; t++)
                                SkipFieldValue(mBase);
                    } else if (mArray) {
                        for (int e = 0; e < arrayLength; e++) {
                            TaggedMember tmp = *m;
                            tmp.type = mBase | TB_ArrayFlag;
                            SkipField(tmp);
                        }
                    } else {
                        for (int e = 0; e < arrayLength; e++)
                            SkipFieldValue(mBase);
                    }
                }
            }
        } else if (baseType == TB_CString) {
            for (int i = 0; i < arrayLength; i++) ReadString();
        }
    } else if (isTuple) {
        if (baseType == TB_Byte) {
            m_Pos = std::min(m_Pos + static_cast<size_t>(member.tupleSize), m_Size);
        } else {
            for (int i = 0; i < member.tupleSize; i++)
                SkipFieldValue(baseType);
        }
    } else {
        SkipFieldValue(baseType);
    }
}

TaggedValue HkxTaggedReader::ReadFieldValue(uint8_t baseType, const std::string& className) {
    TaggedValue val;
    switch (baseType) {
    case TB_Byte: val.data = static_cast<int64_t>(ReadByte()); break;
    case TB_Int: val.data = static_cast<int64_t>(ReadVarInt()); break;
    case TB_Real: val.data = static_cast<double>(ReadFloat()); break;
    case TB_Vec4: case TB_Vec8: case TB_Vec12: case TB_Vec16: {
        int count = baseType * 4 - 12;
        std::vector<float> floats(count);
        for (int i = 0; i < count; i++) floats[i] = ReadFloat();
        val.data = std::move(floats);
        break;
    }
    case TB_Object:
    case TB_Struct: {
        // Both TB_Object and TB_Struct read an inline object via the same
        // readObject dispatch: tag varint first (3=obj, 4=obj+remember,
        // 5=backref, 6=null), then type index + bitmap + fields.
        // (Confirmed via Ghidra RE: readFieldValue cases 8,9 both call readObject)
        int32_t tag = ReadVarInt();
        if (tag == 5) {
            // Backref: the varint is a 0-based index into the remembered
            // object list (only tag-4 objects), NOT a global object ID.
            int32_t refIdx = ReadVarInt();
            if (refIdx >= 0 && refIdx < static_cast<int>(m_RememberedObjects.size()))
                val.data = static_cast<int>(m_RememberedObjects[refIdx]);
            else
                val.data = static_cast<int>(0);
        }
        else if (tag == 6 || tag == 0) { val.data = static_cast<int>(0); }
        else if (tag == 3 || tag == 4) {
            auto obj = ReadObjectBody(tag);
            val.data = static_cast<int>(obj.id);
        }
        else { val.data = static_cast<int>(0); }
        break;
    }
    case TB_CString: val.data = ReadString(); break;
    default: break;
    }
    return val;
}

TaggedValue HkxTaggedReader::ReadField(const TaggedMember& member) {
    uint8_t baseType = member.type & 0x0F;
    bool isArray = member.type & TB_ArrayFlag;
    bool isTuple = member.type & TB_TupleFlag;

    if (isArray) return ReadArrayField(baseType, member.className);

    if (isTuple) {
        if (baseType == TB_Byte) {
            std::vector<uint8_t> bytes(member.tupleSize);
            for (int i = 0; i < member.tupleSize; i++) bytes[i] = ReadByte();
            TaggedValue val; val.data = std::move(bytes); return val;
        }
        TaggedArray arr;
        for (int i = 0; i < member.tupleSize; i++)
            arr.push_back(ReadFieldValue(baseType, member.className));
        TaggedValue val; val.data = std::move(arr); return val;
    }

    return ReadFieldValue(baseType, member.className);
}

TaggedValue HkxTaggedReader::ReadArrayField(uint8_t baseType, const std::string& className) {
    int32_t len = ReadVarInt();
    if (len <= 0 || m_Pos >= m_Size) { TaggedValue v; v.data = TaggedArray{}; return v; }
    if (len > 1000000) len = 0;

    TaggedArray arr;

    if (baseType == TB_Byte) {
        size_t n = std::min(static_cast<size_t>(len), m_Size - m_Pos);
        std::vector<uint8_t> bytes(n);
        for (size_t i = 0; i < n; i++) bytes[i] = ReadByte();
        TaggedValue val; val.data = std::move(bytes); return val;
    }

    switch (baseType) {
    case TB_Int:
        for (int i = 0; i < len; i++) {
            TaggedValue v; v.data = static_cast<int64_t>(ReadVarInt());
            arr.push_back(std::move(v));
        } break;
    case TB_Real:
        for (int i = 0; i < len; i++) {
            TaggedValue v; v.data = static_cast<double>(ReadFloat());
            arr.push_back(std::move(v));
        } break;
    case TB_Vec4: {
        // Vec4 arrays read a varint component count prefix, then N vectors
        // each with that many float components. This is how Havok encodes
        // hkxVertexBufferVertexData::vectorData and similar fields.
        // (Confirmed via Ghidra RE of hkBinaryTagfileReader::readArrayField case 4)
        int comp = ReadVarInt();
        if (comp < 1 || comp > 16) comp = 4;
        for (int i = 0; i < len; i++) {
            std::vector<float> f(comp);
            for (int j = 0; j < comp; j++) f[j] = ReadFloat();
            TaggedValue v; v.data = std::move(f);
            arr.push_back(std::move(v));
        } break;
    }
    case TB_Vec8: case TB_Vec12: case TB_Vec16: {
        // Vec8/12/16 arrays use a static component count derived from the
        // base type (no varint prefix). Formula: baseType * 4 - 12.
        // (Confirmed via Ghidra RE of hkBinaryTagfileReader::readArrayField cases 6,7)
        int comp = baseType * 4 - 12;
        for (int i = 0; i < len; i++) {
            std::vector<float> f(comp);
            for (int j = 0; j < comp; j++) f[j] = ReadFloat();
            TaggedValue v; v.data = std::move(f);
            arr.push_back(std::move(v));
        } break;
    }
    case TB_Object:
        for (int i = 0; i < len; i++)
            arr.push_back(ReadFieldValue(TB_Object, className));
        break;
    case TB_Struct: {
        int typeIdx = 0;
        auto it = m_TypeByName.find(className);
        if (it != m_TypeByName.end()) typeIdx = it->second;
        if (typeIdx > 0 && typeIdx <= static_cast<int>(m_Types.size())) {
            auto& type = m_Types[typeIdx - 1];
            int total = type.totalMemberCount;
            std::vector<bool> bitmap(total, false);
            int bitmapBytes = (total + 7) / 8;
            for (int i = 0; i < bitmapBytes; i++) {
                uint8_t b = ReadByte();
                for (int j = 0; j < 8 && i*8+j < total; j++)
                    bitmap[i*8+j] = (b >> j) & 1;
            }

            int firstId = m_NextObjectId;
            for (int e = 0; e < len; e++) {
                TaggedObject obj;
                obj.typeName = className;
                obj.typeIndex = typeIdx;
                obj.id = m_NextObjectId++;
                m_Objects.push_back(std::move(obj));
            }

            for (int f = 0; f < total; f++) {
                if (!bitmap[f] || f >= static_cast<int>(type.allMembers.size())) continue;
                const auto* m = type.allMembers[f];
                uint8_t mBase = m->type & 0x0F;
                bool mArray = m->type & TB_ArrayFlag;
                bool mTuple = m->type & TB_TupleFlag;

                int vecComponents = 0;
                if (!mArray && !mTuple &&
                    (mBase == TB_Vec4 || mBase == TB_Vec8 || mBase == TB_Vec12 || mBase == TB_Vec16)) {
                    vecComponents = ReadVarInt();
                    if (vecComponents < 1 || vecComponents > 16) vecComponents = mBase * 4 - 12;
                }

                for (int e = 0; e < len; e++) {
                    TaggedValue val;
                    if (vecComponents > 0) {
                        std::vector<float> floats(vecComponents);
                        for (int c = 0; c < vecComponents; c++) floats[c] = ReadFloat();
                        val.data = std::move(floats);
                    } else {
                        val = ReadField(*m);
                    }
                    int objIdx = firstId + e - 1;
                    if (objIdx >= 0 && objIdx < static_cast<int>(m_Objects.size()))
                        m_Objects[objIdx].fields[m->name] = std::move(val);
                }
            }

            for (int e = 0; e < len; e++) {
                TaggedValue v;
                v.data = firstId + e;
                arr.push_back(std::move(v));
            }
        }
        break;
    }
    case TB_CString:
        for (int i = 0; i < len; i++) {
            TaggedValue v; v.data = ReadString();
            arr.push_back(std::move(v));
        } break;
    default: break;
    }

    TaggedValue val; val.data = std::move(arr); return val;
}

TaggedObject HkxTaggedReader::ReadObjectBody(int tag) {
    int32_t typeIdx = ReadVarInt();

    std::string typeName;
    int totalMembers = 0;
    const TaggedType* type = nullptr;

    if (typeIdx > 0 && typeIdx <= static_cast<int>(m_Types.size())) {
        type = &m_Types[typeIdx - 1];
        typeName = type->name;
        totalMembers = type->totalMemberCount;
    }

    // Assign the object ID and register in the remembered list BEFORE
    // reading fields. This matches the Havok runtime behavior (confirmed
    // via Ghidra RE of hkBinaryTagfileReader::readObject): the object is
    // created and added to the remembered array before its members are
    // deserialized. This matters because field reading may create nested
    // objects (inline structs, child objects) that also get remembered —
    // the parent must have a lower remembered index than its children so
    // that later backreferences resolve correctly.
    int objId = m_NextObjectId++;

    // Reserve a slot in m_Objects for this object. We fill in its fields
    // after reading them, but the slot must exist now so that the ID is
    // valid for any self-references during field reading.
    {
        TaggedObject placeholder;
        placeholder.id = objId;
        placeholder.typeIndex = typeIdx;
        placeholder.typeName = typeName;
        m_Objects.push_back(std::move(placeholder));
    }

    // Tag 4 = "remembered" object, eligible for backreference via tag 5.
    // In the Havok tagged binary format, tag 5 backreferences use a 0-based
    // index into only the remembered object list, NOT the global object table.
    // (Confirmed via Ghidra RE: readObject adds to array at this+0x30 before
    // reading the member bitmap and field data.)
    if (tag == 4) m_RememberedObjects.push_back(objId);

    if (totalMembers <= 0 || !type) {
        return m_Objects[objId - 1];
    }

    // Read bitmap
    std::vector<bool> bitmap(totalMembers, false);
    int bitmapBytes = (totalMembers + 7) / 8;
    for (int i = 0; i < bitmapBytes; i++) {
        uint8_t b = ReadByte();
        for (int j = 0; j < 8 && i*8+j < totalMembers; j++)
            bitmap[i*8+j] = (b >> j) & 1;
    }

    // Read all fields indicated by the bitmap.
    // Note: field reading may recursively create more objects (via inline
    // tag 3/4 objects), which will be appended to m_Objects and may
    // invalidate iterators/pointers — but we access by index (objId - 1).
    std::unordered_map<std::string, TaggedValue> fields;
    for (int i = 0; i < totalMembers; i++) {
        if (!bitmap[i]) continue;
        if (i >= static_cast<int>(type->allMembers.size())) continue;
        if (m_Pos >= m_Size) break;
        const auto* member = type->allMembers[i];
        fields[member->name] = ReadField(*member);
    }

    // Store the read fields into the already-allocated object slot.
    m_Objects[objId - 1].fields = std::move(fields);

    return m_Objects[objId - 1];
}

// Not needed with current approach but declared in header
void HkxTaggedReader::ScanAndParsePhysicsObjects(size_t) {}

// --- Main parse ---

bool HkxTaggedReader::Parse(const uint8_t* data, size_t size, ParseResult& result) {
    m_Data = data;
    m_Size = size;
    m_Pos = 0;
    m_StringPool.clear();
    m_Types.clear();
    m_Objects.clear();
    m_Objects.reserve(256);
    m_RememberedObjects.clear();
    m_TypeByName.clear();
    m_NextObjectId = 1;

    m_StringPool.push_back("");
    m_StringPool.push_back("");

    if (size < 8) return false;
    uint32_t magic0, magic1;
    std::memcpy(&magic0, data, 4);
    std::memcpy(&magic1, data + 4, 4);
    if (magic0 != TAGGED_MAGIC_0 || magic1 != TAGGED_MAGIC_1) return false;
    m_Pos = 8;

    // Tag dispatch loop: reads metadata tags (1=fileinfo, 2=type defs) then
    // the root object (tag 3/4). The entire object graph is read recursively
    // from the root — inner objects appear as inline tag 3/4 or backrefs (tag 5).
    // After the root object the stream ends with tag -1 (0xFFFFFFFF).
    // (Confirmed via Ghidra RE of hkBinaryTagfileReader::loadFile)
    while (m_Pos < m_Size) {
        int32_t tag = ReadVarInt();
        if (tag == -1) break;

        switch (tag) {
        case 1: ReadFileInfo(); break;
        case 2:
            ReadMetadata();
            FlattenTypes();
            break;
        case 3: case 4:
            ReadObjectBody(tag);
            break;
        case 5: ReadVarInt(); break; // top-level backref (rare)
        case 6: break;               // null
        default: break;              // unknown tag — skip
        }
    }

    ConvertToParseResult(result);
    return true;
}

} // namespace Hkx
