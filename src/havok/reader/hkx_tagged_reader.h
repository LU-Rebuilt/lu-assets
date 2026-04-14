// Internal implementation detail of HkxFile — not part of the public API.
// This is included only by HkxFile.cpp to handle tagged binary format files.
// External code should use HkxFile::Parse() which dispatches to this automatically.
#pragma once

#include "havok/types/hkx_types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <variant>

namespace Hkx {

// Havok tagged binary format type codes
enum TaggedBaseType : uint8_t {
    TB_Void = 0,
    TB_Byte = 1,
    TB_Int = 2,
    TB_Real = 3,
    TB_Vec4 = 4,
    TB_Vec8 = 5,
    TB_Vec12 = 6,
    TB_Vec16 = 7,
    TB_Object = 8,
    TB_Struct = 9,
    TB_CString = 10,
};

constexpr uint8_t TB_ArrayFlag = 16;
constexpr uint8_t TB_TupleFlag = 32;

struct TaggedMember {
    std::string name;
    uint8_t type = 0;        // base type | flags
    int tupleSize = 0;       // for TupleFlag
    std::string className;   // for Object/Struct types
};

struct TaggedType {
    std::string name;
    int parentIndex = 0;     // 0 = no parent
    std::vector<TaggedMember> members;

    // Flattened members including inherited (computed after all types loaded)
    std::vector<const TaggedMember*> allMembers;
    int totalMemberCount = 0;
};

// Generic value container for tagged object fields
struct TaggedValue;
using TaggedArray = std::vector<TaggedValue>;
struct TaggedObject;

struct TaggedValue {
    std::variant<
        std::monostate,           // void/null
        int64_t,                  // int
        double,                   // real (stored as double for precision)
        std::string,              // cstring
        std::vector<float>,       // vec4/vec8/vec12/vec16
        std::vector<uint8_t>,     // raw bytes
        TaggedArray,              // array
        int                       // object reference (index into object table)
    > data;
};

struct TaggedObject {
    int typeIndex = 0;
    std::string typeName;
    std::unordered_map<std::string, TaggedValue> fields;
    int id = 0; // sequential ID for references
};

class HkxTaggedReader {
public:
    // Parse a tagged binary format HKX file and populate a ParseResult
    bool Parse(const uint8_t* data, size_t size, ParseResult& result);

private:
    // Stream reading
    int32_t ReadVarInt();
    std::string ReadString();
    float ReadFloat();
    uint8_t ReadByte();

    // Tag handlers
    void ReadFileInfo();
    void ReadMetadata();
    TaggedObject ReadObjectBody(int tag);
    TaggedValue ReadField(const TaggedMember& member);
    TaggedValue ReadFieldValue(uint8_t baseType, const std::string& className);
    TaggedValue ReadArrayField(uint8_t baseType, const std::string& className);
    void SkipField(const TaggedMember& member);
    void ScanAndParsePhysicsObjects(size_t startAfter);
    void SkipFieldValue(uint8_t baseType);

    // Flatten type hierarchies (compute allMembers)
    void FlattenTypes();
    void FlattenType(int index, std::vector<const TaggedMember*>& out);

    // Convert parsed tagged objects into ParseResult
    void ConvertToParseResult(ParseResult& result);

    // Object conversion helpers
    ShapeInfo ConvertShape(const TaggedObject& obj);
    RigidBodyInfo ConvertRigidBody(const TaggedObject& obj);
    PhysicsSystemInfo ConvertPhysicsSystem(const TaggedObject& obj);
    PhysicsDataInfo ConvertPhysicsData(const TaggedObject& obj);

    // Scene conversion
    void ConvertScenes(ParseResult& result);
    SceneMesh ConvertMeshSection(const TaggedObject& meshSectionObj);
    SceneNode ConvertNode(const TaggedObject& nodeObj, SceneInfo& scene);

    // Resolve an object reference
    const TaggedObject* ResolveObject(int refId) const;

    // Get a field value with type checking
    template<typename T>
    const T* GetField(const TaggedObject& obj, const std::string& name) const {
        auto it = obj.fields.find(name);
        if (it == obj.fields.end()) return nullptr;
        return std::get_if<T>(&it->second.data);
    }

    float GetFloatField(const TaggedObject& obj, const std::string& name, float def = 0) const;
    int64_t GetIntField(const TaggedObject& obj, const std::string& name, int64_t def = 0) const;
    std::string GetStringField(const TaggedObject& obj, const std::string& name) const;
    std::vector<float> GetVecField(const TaggedObject& obj, const std::string& name) const;
    int GetObjectRef(const TaggedObject& obj, const std::string& name) const;
    const TaggedArray* GetArrayField(const TaggedObject& obj, const std::string& name) const;

    // Check if a type is physics-relevant (worth storing fields for)
    bool IsPhysicsType(const std::string& name) const;

    // Reader state
    const uint8_t* m_Data = nullptr;
    size_t m_Size = 0;
    size_t m_Pos = 0;
    bool m_LittleEndian = true;

    // String pool
    std::vector<std::string> m_StringPool;

    // Type table
    std::vector<TaggedType> m_Types;

    // Object table (1-indexed: object with id=N is at m_Objects[N-1])
    std::vector<TaggedObject> m_Objects;
public:
    const std::vector<TaggedObject>& Objects() const { return m_Objects; }
    const TaggedObject* ResolveObjectPublic(int refId) const { return ResolveObject(refId); }
private:
    int m_NextObjectId = 1;

    // Remembered object IDs (tag 4 objects only).
    // Havok tagged format backreferences (tag 5) index into this list,
    // NOT into the full object table. Tag 3 objects are not remembered
    // and cannot be referenced later in the stream.
    // (Confirmed via Ghidra RE of hkBinaryTagfileReader::readObject)
    std::vector<int> m_RememberedObjects;

    // Map type name -> type index for quick lookup
    std::unordered_map<std::string, int> m_TypeByName;
};

} // namespace Hkx
