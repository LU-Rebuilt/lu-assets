// HKX file parser — reads Havok binary packfile, tagged binary, and XML formats.
//
// Dispatches to the correct reader based on file magic bytes:
//   0x57E0E057 / 0x10C0C010 → binary packfile (Havok 5.5, used by LU client)
//   0xCAB00D1E / 0xD011FACE → tagged binary (Havok 2010+)
//   "<?xml" / "<hkpackfile"  → Havok XML format
//
// Usage:
//   Hkx::HkxFile hkx;
//   auto result = hkx.Parse("path/to/file.hkx");
//   if (result.success) {
//       // result.rigidBodies, result.shapes, result.scenes, etc.
//   }
//
// The input data is NOT copied — callers must keep the buffer alive for the
// duration of parsing. The returned ParseResult owns all extracted data.
//
// References:
//   - HKXDocs (github.com/SimonNitzsche/HKXDocs) — HKX format documentation
//   - Ghidra RE of legouniverse.exe — binary offsets and class identification
#pragma once

#include "havok/types/hkx_types.h"

#include <memory>
#include <filesystem>

namespace Hkx {

// Parse any HKX file (binary, tagged, or XML). Auto-detects format from magic bytes.
// Returns ParseResult with success=true on success, or error message on failure.
class HkxFile {
public:
    // Parse from a file on disk. Reads the entire file into memory.
    ParseResult Parse(const std::filesystem::path& filePath);

    // Parse from an in-memory buffer. The data must remain valid during parsing.
    ParseResult Parse(const uint8_t* data, size_t size);

private:
    // Implementation details in hkx_reader.cpp and hkx_binary_reader.cpp.
    // Not part of the public API — see source files for internals.
    struct Impl;
    friend struct Impl;

    bool ParseHeader();
    bool ParseTaggedFormat();
    bool ParseSections();
    bool ParseClassnames();
    bool ParseFixups();
    bool ExtractPhysicsObjects();

    ShapeInfo ReadShape(uint32_t offset, int depth = 0);
    RigidBodyInfo ReadRigidBody(uint32_t offset);
    PhysicsSystemInfo ReadPhysicsSystem(uint32_t offset);
    PhysicsDataInfo ReadPhysicsData(uint32_t offset);
    RootLevelContainerInfo ReadRootLevelContainer(uint32_t offset);
    SceneMesh ReadSceneMesh(uint32_t meshSectionOffset);
    SceneInfo ReadScene(uint32_t sceneOffset);
    SceneNode ReadSceneNode(uint32_t nodeOffset, SceneInfo& scene);
    void ReadShapeCommon(ShapeInfo& info, const uint8_t* objData, size_t remaining);
    void ReadCompressedMeshShape(ShapeInfo& info, uint32_t offset, const uint8_t* objData, size_t remaining);

    uint32_t ReadU32(const uint8_t* ptr) const;
    uint16_t ReadU16(const uint8_t* ptr) const;
    int8_t ReadI8(const uint8_t* ptr) const;
    uint8_t ReadU8(const uint8_t* ptr) const;
    float ReadFloat(const uint8_t* ptr) const;
    Vector4 ReadVector4(const uint8_t* ptr) const;
    Quaternion ReadQuat(const uint8_t* ptr) const;
    Transform ReadTransform(const uint8_t* ptr) const;

    uint32_t ResolveLocalPointer(uint32_t srcOffset) const;
    uint32_t ResolveGlobalPointer(uint32_t srcOffset) const;
    uint32_t ResolvePointer(uint32_t srcOffset) const;
    std::string ResolveString(uint32_t ptrOffset) const;
    std::vector<std::pair<uint32_t, uint32_t>> GetFixupsInRange(uint32_t start, uint32_t end) const;
    ShapeType ClassifyShape(const std::string& className) const;
    std::vector<uint16_t> ReadInlineArrayU16(uint32_t dataOffset) const;
    std::vector<uint8_t> ReadInlineArrayU8(uint32_t dataOffset) const;

    // Binary packfile internal state
    const uint8_t* m_Data = nullptr;
    size_t m_Size = 0;
    bool m_LittleEndian = true;
    uint8_t m_PointerSize = 4;

    // Packfile header (64 bytes)
    struct PackfileHeader {
        uint32_t magic[2];
        uint32_t userTag;
        uint32_t fileVersion;
        uint8_t pointerSize;
        uint8_t littleEndian;
        uint8_t reusePaddingOptimization;
        uint8_t emptyBaseClassOptimization;
        uint32_t numSections;
        uint32_t contentsSectionIndex;
        uint32_t contentsSectionOffset;
        uint32_t contentsClassNameSectionIndex;
        uint32_t contentsClassNameSectionOffset;
        char contentsVersion[16];
        uint32_t flags;
        int16_t maxPredicate;
        int16_t predicateArraySizePlusPadding;
    };
    static_assert(sizeof(PackfileHeader) == 64);

    // Section header (48 bytes)
    struct SectionHeader {
        char sectionTag[20];
        uint32_t absoluteDataStart;
        uint32_t localFixupsOffset;
        uint32_t globalFixupsOffset;
        uint32_t virtualFixupsOffset;
        uint32_t exportsOffset;
        uint32_t importsOffset;
        uint32_t endOffset;
    };
    static_assert(sizeof(SectionHeader) == 48);

    struct VirtualFixup { uint32_t dataOffset, classSectionIndex, classnameOffset; };
    struct GlobalFixup { uint32_t srcOffset, dstSectionIndex, dstOffset; };
    struct LocalFixup { uint32_t srcOffset, dstOffset; };

    PackfileHeader m_Header{};
    std::vector<SectionHeader> m_Sections;
    std::unordered_map<uint32_t, std::string> m_ClassnamesByOffset;
    std::vector<VirtualFixup> m_VirtualFixups;
    std::vector<LocalFixup> m_LocalFixups;
    std::vector<GlobalFixup> m_GlobalFixups;
    std::vector<LocalFixup> m_SortedLocalFixups;
    std::vector<GlobalFixup> m_SortedGlobalFixups;

    int m_DataSectionIndex = -1;
    uint32_t m_DataSectionStart = 0;
    const uint8_t* m_DataSection = nullptr;
    size_t m_DataSectionSize = 0;
    uint32_t m_ClassnamesSectionStart = 0;
    const uint8_t* m_ClassnamesSection = nullptr;

    std::unordered_map<uint32_t, std::string> m_ObjectClasses;
    std::map<uint32_t, uint32_t> m_ObjectSizes;
    ParseResult m_Result;
};

} // namespace Hkx
