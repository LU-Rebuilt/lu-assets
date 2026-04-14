#pragma once

#include "havok/types/hkx_types.h"
#include <filesystem>
#include <vector>
#include <string>
#include <unordered_map>

namespace Hkx {

struct WriteOptions {
    std::string havokVersion = "Havok-7.1.0-r1";
    uint32_t fileVersion = 7;
    uint8_t pointerSize = 4;
};

class HkxWriter {
public:
    // Write a ParseResult back to an HKX binary packfile
    bool Write(const std::filesystem::path& outputPath,
               const ParseResult& result,
               const WriteOptions& options = {});

    // Write from individual components
    bool Write(const std::filesystem::path& outputPath,
               const std::vector<RigidBodyInfo>& rigidBodies,
               const WriteOptions& options = {});

    const std::string& GetError() const { return m_Error; }

private:
    void Reset();

    // Serialization helpers
    void WriteU8(uint8_t v);
    void WriteU16(uint16_t v);
    void WriteU32(uint32_t v);
    void WriteFloat(float v);
    void WriteVector4(const Vector4& v);
    void WriteQuaternion(const Quaternion& q);
    void WriteTransform(const Transform& t);
    void WriteBytes(const void* data, size_t size);
    void Pad(size_t alignment);
    void PadTo(size_t target);
    uint32_t Pos() const;

    // Register a class for the classnames section
    uint32_t RegisterClass(const std::string& name);

    // Track fixups
    void AddLocalFixup(uint32_t srcOffset, uint32_t dstOffset);
    void AddGlobalFixup(uint32_t srcOffset, uint32_t dstSectionIndex, uint32_t dstOffset);
    void AddVirtualFixup(uint32_t dataOffset, const std::string& className);

    // Object serializers - return the data offset where the object was written
    uint32_t WriteShape(const ShapeInfo& shape);
    uint32_t WriteRigidBody(const RigidBodyInfo& rb);
    uint32_t WritePhysicsSystem(const PhysicsSystemInfo& sys);
    uint32_t WritePhysicsData(const PhysicsDataInfo& data);
    uint32_t WriteRootLevelContainer(const std::vector<NamedVariant>& variants);

    // Write an hkArray: pointer + size + capacityAndFlags
    // Returns the offset of the pointer field (for fixup registration)
    uint32_t WriteArray(uint32_t count);

    // Build the final file from all accumulated data
    std::vector<uint8_t> BuildFile(const WriteOptions& options);

    // Data section buffer
    std::vector<uint8_t> m_DataSection;

    // Classnames
    struct ClassInfo {
        uint32_t signature;
        uint32_t nameOffset; // offset within classnames section (of the string, after signature+tab)
    };
    std::vector<uint8_t> m_ClassnamesSection;
    std::unordered_map<std::string, ClassInfo> m_RegisteredClasses;

    // Fixup tracking
    struct LocalFixupEntry { uint32_t src, dst; };
    struct GlobalFixupEntry { uint32_t src, dstSection, dst; };
    struct VirtualFixupEntry { uint32_t dataOffset; std::string className; };
    std::vector<LocalFixupEntry> m_LocalFixups;
    std::vector<GlobalFixupEntry> m_GlobalFixups;
    std::vector<VirtualFixupEntry> m_VirtualFixups;

    // Map from ShapeInfo pointer to written data offset (for dedup)
    std::unordered_map<uint32_t, uint32_t> m_WrittenShapes;

    std::string m_Error;

    // Known Havok 7.1.0 class signatures (from LU client)
    static const std::unordered_map<std::string, uint32_t> s_ClassSignatures;
};

} // namespace Hkx
