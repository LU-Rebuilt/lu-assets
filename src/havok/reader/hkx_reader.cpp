// Havok Binary Packfile Parser
//
// Known limitations / TODO:
// - Scene mesh extraction (hkxVertexBuffer, hkxIndexBuffer, hkxMeshSection):
//   vertex data and index data are not yet extracted from binary packfile format.
//   Needs reading hkxVertexDescription ElementDecl array to determine vertex layout,
//   then extracting position floats from vectorData/floatData based on stride.
// - hkpRigidBody layout uses heuristic scanning for the motion state transform.
//   Should be replaced with fixed offsets derived from Ghidra RE of the client.
//   Current heuristic fails for some rigid bodies with unusual padding.
// - hkpCompressedMeshShape: only extracts child shapes via fixup scanning,
//   doesn't read the compressed mesh data (quantized vertices, material info).
// - hkpExtendedMeshShape / hkpStorageExtendedMeshShape: subparts (triangle and
//   shape subparts) not extracted, only child shapes found via fixups.
// - Havok 5.x __types__ section contains full class definitions with member info.
//   Currently ignored; could be used to auto-detect field offsets per version.
// - Constraint data (hkpConstraintData subtypes) not parsed.
// - hkpPhantom objects not parsed (trigger volumes).
// - No support for 64-bit pointer packfiles (pointerSize == 8).

#include "havok/reader/hkx_reader.h"
#include "havok/reader/hkx_tagged_reader.h"
#include <string>
#include "havok/reader/hkx_xml_reader.h"

#include <cstring>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <filesystem>

namespace Hkx {

ParseResult HkxFile::Parse(const std::filesystem::path& filePath) {
	m_Result = ParseResult{};

	std::ifstream file(filePath, std::ios::binary | std::ios::ate);
	if (!file.is_open()) {
		m_Result.error = "Failed to open file: " + filePath.string();
		return m_Result;
	}

	auto fileSize = file.tellg();
	file.seekg(0);

	std::vector<uint8_t> buffer(fileSize);
	file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

	return Parse(buffer.data(), buffer.size());
}

ParseResult HkxFile::Parse(const uint8_t* data, size_t size) {
	m_Result = ParseResult{};
	m_Data = data;
	m_Size = size;

	// Detect XML format (starts with "<?xml" or "<hkpackfile")
	if (size >= 5 && (std::memcmp(data, "<?xml", 5) == 0 ||
	                   std::memcmp(data, "<hkpa", 5) == 0)) {
		HkxXmlReader xmlReader;
		std::string xmlContent(reinterpret_cast<const char*>(data), size);
		m_Result = xmlReader.Parse(xmlContent);
		return m_Result;
	}

	if (size < sizeof(PackfileHeader)) {
		m_Result.error = "File too small for packfile header";
		return m_Result;
	}

	if (!ParseHeader()) return m_Result;
	if (!ParseSections()) return m_Result;
	if (!ParseClassnames()) return m_Result;
	if (!ParseFixups()) return m_Result;
	if (!ExtractPhysicsObjects()) return m_Result;

	m_Result.success = true;
	return m_Result;
}

bool HkxFile::ParseHeader() {
	std::memcpy(&m_Header, m_Data, sizeof(PackfileHeader));

	if (m_Header.magic[0] != BINARY_MAGIC_0 || m_Header.magic[1] != BINARY_MAGIC_1) {
		if (m_Header.magic[0] == TAGGED_MAGIC_0) {
			ParseTaggedFormat();
			return false;
		}
		m_Result.error = "Not a Havok binary packfile (bad magic)";
		return false;
	}

	m_LittleEndian = m_Header.littleEndian != 0;
	m_PointerSize = m_Header.pointerSize;

	m_Result.fileVersion = m_Header.fileVersion;
	m_Result.pointerSize = m_PointerSize;
	m_Result.havokVersion = std::string(m_Header.contentsVersion,
		strnlen(m_Header.contentsVersion, sizeof(m_Header.contentsVersion)));

	if (m_Header.fileVersion < 4 || m_Header.fileVersion > 11) {
		m_Result.error = "Unsupported packfile version: " + std::to_string(m_Header.fileVersion);
		return false;
	}

	return true;
}

bool HkxFile::ParseTaggedFormat() {
	// Use the proper tagged binary format parser
	HkxTaggedReader tagReader;
	try {
		if (tagReader.Parse(m_Data, m_Size, m_Result)) {
			m_Result.success = true;
			m_Result.fileVersion = 0;
			m_Result.pointerSize = 0;
			return true;
		}
	} catch (const std::exception& e) {
		std::cerr << "Tagged format parse exception: " << e.what() << "\n";
	} catch (...) {
		std::cerr << "Tagged format parse exception (unknown)\n";
	}

	// Fallback: at least report it's a tagged format
	m_Result.havokVersion = "(tagged binary format - parse failed)";
	m_Result.success = true;
	return true;
}

bool HkxFile::ParseSections() {
	size_t offset = sizeof(PackfileHeader);

	for (uint32_t i = 0; i < m_Header.numSections; i++) {
		if (offset + sizeof(SectionHeader) > m_Size) {
			m_Result.error = "Truncated section header";
			return false;
		}

		SectionHeader section;
		std::memcpy(&section, m_Data + offset, sizeof(SectionHeader));
		m_Sections.push_back(section);
		offset += sizeof(SectionHeader);
	}

	for (size_t i = 0; i < m_Sections.size(); i++) {
		std::string tag(m_Sections[i].sectionTag, strnlen(m_Sections[i].sectionTag, 20));

		if (tag == "__classnames__") {
			m_ClassnamesSectionStart = m_Sections[i].absoluteDataStart;
			m_ClassnamesSection = m_Data + m_ClassnamesSectionStart;
		} else if (tag == "__data__") {
			m_DataSectionIndex = static_cast<int>(i);
			m_DataSectionStart = m_Sections[i].absoluteDataStart;
			m_DataSection = m_Data + m_DataSectionStart;
			m_DataSectionSize = m_Sections[i].localFixupsOffset; // data ends at first fixup
		}
	}

	if (!m_DataSection) {
		m_Result.error = "No __data__ section found";
		return false;
	}

	if (!m_ClassnamesSection) {
		m_Result.error = "No __classnames__ section found";
		return false;
	}

	return true;
}

bool HkxFile::ParseClassnames() {
	int classnamesIdx = -1;
	for (size_t i = 0; i < m_Sections.size(); i++) {
		std::string tag(m_Sections[i].sectionTag, strnlen(m_Sections[i].sectionTag, 20));
		if (tag == "__classnames__") {
			classnamesIdx = static_cast<int>(i);
			break;
		}
	}

	if (classnamesIdx < 0) return false;

	const auto& sec = m_Sections[classnamesIdx];
	const uint8_t* ptr = m_Data + sec.absoluteDataStart;
	const uint8_t* end = ptr + sec.localFixupsOffset;

	uint32_t relOffset = 0;
	while (ptr + 5 < end) {
		uint32_t signature = ReadU32(ptr);
		uint8_t separator = ptr[4];

		if (separator != 0x09) break;

		const char* name = reinterpret_cast<const char*>(ptr + 5);
		size_t nameLen = strnlen(name, end - ptr - 5);
		if (nameLen == 0) break;

		ClassEntry entry;
		entry.signature = signature;
		entry.name = std::string(name, nameLen);
		m_ClassnamesByOffset[relOffset + 5] = entry.name;
		m_Result.classEntries.push_back(entry);

		uint32_t advance = 5 + static_cast<uint32_t>(nameLen) + 1;
		ptr += advance;
		relOffset += advance;
	}

	return true;
}

bool HkxFile::ParseFixups() {
	int dataSectionIdx = -1;
	for (size_t i = 0; i < m_Sections.size(); i++) {
		std::string tag(m_Sections[i].sectionTag, strnlen(m_Sections[i].sectionTag, 20));
		if (tag == "__data__") {
			dataSectionIdx = static_cast<int>(i);
			break;
		}
	}

	if (dataSectionIdx < 0) return false;

	const auto& sec = m_Sections[dataSectionIdx];

	// Parse local fixups
	{
		const uint8_t* ptr = m_Data + sec.absoluteDataStart + sec.localFixupsOffset;
		const uint8_t* end = m_Data + sec.absoluteDataStart + sec.globalFixupsOffset;

		while (ptr + 8 <= end) {
			LocalFixup fixup;
			fixup.srcOffset = ReadU32(ptr);
			fixup.dstOffset = ReadU32(ptr + 4);
			if (fixup.srcOffset != 0xFFFFFFFF) {
				m_LocalFixups.push_back(fixup);
			}
			ptr += 8;
		}
	}

	// Create sorted copy for binary search
	m_SortedLocalFixups = m_LocalFixups;
	std::sort(m_SortedLocalFixups.begin(), m_SortedLocalFixups.end(),
		[](const LocalFixup& a, const LocalFixup& b) { return a.srcOffset < b.srcOffset; });

	// Parse global fixups
	{
		const uint8_t* ptr = m_Data + sec.absoluteDataStart + sec.globalFixupsOffset;
		const uint8_t* end = m_Data + sec.absoluteDataStart + sec.virtualFixupsOffset;

		while (ptr + 12 <= end) {
			GlobalFixup fixup;
			fixup.srcOffset = ReadU32(ptr);
			fixup.dstSectionIndex = ReadU32(ptr + 4);
			fixup.dstOffset = ReadU32(ptr + 8);
			if (fixup.srcOffset != 0xFFFFFFFF) {
				m_GlobalFixups.push_back(fixup);
			}
			ptr += 12;
		}
	}

	// Create sorted global fixups for binary search
	m_SortedGlobalFixups = m_GlobalFixups;
	std::sort(m_SortedGlobalFixups.begin(), m_SortedGlobalFixups.end(),
		[](const GlobalFixup& a, const GlobalFixup& b) { return a.srcOffset < b.srcOffset; });

	// Parse virtual fixups
	{
		const uint8_t* ptr = m_Data + sec.absoluteDataStart + sec.virtualFixupsOffset;
		const uint8_t* end = m_Data + sec.absoluteDataStart + sec.exportsOffset;

		while (ptr + 12 <= end) {
			VirtualFixup fixup;
			fixup.dataOffset = ReadU32(ptr);
			fixup.classSectionIndex = ReadU32(ptr + 4);
			fixup.classnameOffset = ReadU32(ptr + 8);
			if (fixup.dataOffset != 0xFFFFFFFF) {
				m_VirtualFixups.push_back(fixup);
				auto it = m_ClassnamesByOffset.find(fixup.classnameOffset);
				if (it != m_ClassnamesByOffset.end()) {
					m_ObjectClasses[fixup.dataOffset] = it->second;
					m_Result.objectsByClass[it->second].push_back(fixup.dataOffset);
				}
			}
			ptr += 12;
		}
	}

	// Compute object sizes from sorted offsets
	std::vector<uint32_t> sortedOffsets;
	for (const auto& [off, _] : m_ObjectClasses) {
		sortedOffsets.push_back(off);
	}
	std::sort(sortedOffsets.begin(), sortedOffsets.end());
	for (size_t i = 0; i < sortedOffsets.size(); i++) {
		uint32_t next = (i + 1 < sortedOffsets.size()) ? sortedOffsets[i + 1] : static_cast<uint32_t>(m_DataSectionSize);
		m_ObjectSizes[sortedOffsets[i]] = next - sortedOffsets[i];
	}

	return true;
}

bool HkxFile::ExtractPhysicsObjects() {
	// Extract shapes
	for (const auto& [className, offsets] : m_Result.objectsByClass) {
		ShapeType stype = ClassifyShape(className);
		if (stype != ShapeType::Unknown) {
			for (uint32_t off : offsets) {
				ShapeInfo info = ReadShape(off);
				if (info.type != ShapeType::Unknown) {
					m_Result.shapes.push_back(info);
				}
			}
		}
	}

	// Extract rigid bodies
	auto rbIt = m_Result.objectsByClass.find("hkpRigidBody");
	if (rbIt != m_Result.objectsByClass.end()) {
		for (uint32_t off : rbIt->second) {
			m_Result.rigidBodies.push_back(ReadRigidBody(off));
		}
	}

	// Extract physics systems
	auto psIt = m_Result.objectsByClass.find("hkpPhysicsSystem");
	if (psIt != m_Result.objectsByClass.end()) {
		for (uint32_t off : psIt->second) {
			m_Result.physicsSystems.push_back(ReadPhysicsSystem(off));
		}
	}

	// Extract physics data
	auto pdIt = m_Result.objectsByClass.find("hkpPhysicsData");
	if (pdIt != m_Result.objectsByClass.end()) {
		for (uint32_t off : pdIt->second) {
			m_Result.physicsData.push_back(ReadPhysicsData(off));
		}
	}

	// Extract root level containers
	auto rlcIt = m_Result.objectsByClass.find("hkRootLevelContainer");
	if (rlcIt != m_Result.objectsByClass.end()) {
		for (uint32_t off : rlcIt->second) {
			m_Result.rootContainers.push_back(ReadRootLevelContainer(off));
		}
	}

	// Extract scene hierarchy from hkxScene objects (with node tree + transforms)
	auto sceneIt = m_Result.objectsByClass.find("hkxScene");
	if (sceneIt != m_Result.objectsByClass.end()) {
		for (uint32_t off : sceneIt->second) {
			SceneInfo scene = ReadScene(off);
			if (!scene.meshes.empty() || !scene.nodes.empty()) {
				m_Result.scenes.push_back(std::move(scene));
			}
		}
	}

	// Fallback: if no scenes were found via hkxScene, collect flat mesh sections
	if (m_Result.scenes.empty()) {
		auto msIt = m_Result.objectsByClass.find("hkxMeshSection");
		if (msIt != m_Result.objectsByClass.end()) {
			SceneInfo scene;
			for (uint32_t off : msIt->second) {
				SceneMesh mesh = ReadSceneMesh(off);
				if (!mesh.vertices.empty()) {
					scene.meshes.push_back(std::move(mesh));
				}
			}
			if (!scene.meshes.empty()) {
				m_Result.scenes.push_back(std::move(scene));
			}
		}
	}

	return true;
}

// ReadShapeCommon, ReadShape, ReadRigidBody, ReadPhysicsSystem,
// ReadPhysicsData, and ReadRootLevelContainer are in HkxBinaryExtract.cpp

uint32_t HkxFile::ResolveLocalPointer(uint32_t srcOffset) const {
	// Binary search in sorted fixups
	auto it = std::lower_bound(m_SortedLocalFixups.begin(), m_SortedLocalFixups.end(),
		srcOffset, [](const LocalFixup& f, uint32_t val) { return f.srcOffset < val; });
	if (it != m_SortedLocalFixups.end() && it->srcOffset == srcOffset) {
		return it->dstOffset;
	}
	return 0xFFFFFFFF;
}

uint32_t HkxFile::ResolveGlobalPointer(uint32_t srcOffset) const {
	auto it = std::lower_bound(m_SortedGlobalFixups.begin(), m_SortedGlobalFixups.end(),
		srcOffset, [](const GlobalFixup& f, uint32_t val) { return f.srcOffset < val; });
	if (it != m_SortedGlobalFixups.end() && it->srcOffset == srcOffset) {
		// For same-section pointers (data -> data), return the destination offset directly
		if (static_cast<int>(it->dstSectionIndex) == m_DataSectionIndex) {
			return it->dstOffset;
		}
		// For cross-section pointers, we could resolve them but for now
		// we only handle data-section-to-data-section
	}
	return 0xFFFFFFFF;
}

uint32_t HkxFile::ResolvePointer(uint32_t srcOffset) const {
	// Try local fixups first (intra-object pointers like arrays)
	uint32_t result = ResolveLocalPointer(srcOffset);
	if (result != 0xFFFFFFFF) return result;

	// Then try global fixups (inter-object pointers like shape refs)
	return ResolveGlobalPointer(srcOffset);
}

std::string HkxFile::ResolveString(uint32_t ptrOffset) const {
	uint32_t strOff = ResolvePointer(ptrOffset);
	if (strOff != 0xFFFFFFFF && strOff < m_DataSectionSize) {
		const char* str = reinterpret_cast<const char*>(m_DataSection + strOff);
		size_t len = strnlen(str, m_DataSectionSize - strOff);
		if (len > 0 && len < 256) {
			return std::string(str, len);
		}
	}
	return "";
}

std::vector<std::pair<uint32_t, uint32_t>> HkxFile::GetFixupsInRange(uint32_t start, uint32_t end) const {
	std::vector<std::pair<uint32_t, uint32_t>> result;

	// Local fixups
	auto lit = std::lower_bound(m_SortedLocalFixups.begin(), m_SortedLocalFixups.end(),
		start, [](const LocalFixup& f, uint32_t val) { return f.srcOffset < val; });
	while (lit != m_SortedLocalFixups.end() && lit->srcOffset < end) {
		result.emplace_back(lit->srcOffset, lit->dstOffset);
		++lit;
	}

	// Global fixups (same-section only)
	auto git = std::lower_bound(m_SortedGlobalFixups.begin(), m_SortedGlobalFixups.end(),
		start, [](const GlobalFixup& f, uint32_t val) { return f.srcOffset < val; });
	while (git != m_SortedGlobalFixups.end() && git->srcOffset < end) {
		if (static_cast<int>(git->dstSectionIndex) == m_DataSectionIndex) {
			result.emplace_back(git->srcOffset, git->dstOffset);
		}
		++git;
	}

	// Sort by source offset for consistent ordering
	std::sort(result.begin(), result.end());
	return result;
}

ShapeType HkxFile::ClassifyShape(const std::string& className) const {
	if (className == "hkpBoxShape") return ShapeType::Box;
	if (className == "hkpSphereShape") return ShapeType::Sphere;
	if (className == "hkpCapsuleShape") return ShapeType::Capsule;
	if (className == "hkpCylinderShape") return ShapeType::Cylinder;
	if (className == "hkpConvexVerticesShape") return ShapeType::ConvexVertices;
	if (className == "hkpConvexTransformShape") return ShapeType::ConvexTransform;
	if (className == "hkpConvexTranslateShape") return ShapeType::ConvexTranslate;
	if (className == "hkpMoppBvTreeShape") return ShapeType::Mopp;
	if (className == "hkpListShape") return ShapeType::List;
	if (className == "hkpTransformShape") return ShapeType::Transform;
	if (className == "hkpTriangleShape") return ShapeType::Triangle;
	if (className == "hkpBvTreeShape") return ShapeType::BvTree;
	if (className == "hkpCompressedMeshShape") return ShapeType::CompressedMesh;
	if (className == "hkpExtendedMeshShape") return ShapeType::ExtendedMesh;
	if (className == "hkpStorageExtendedMeshShape") return ShapeType::ExtendedMesh;
	if (className == "hkpSimpleMeshShape") return ShapeType::SimpleContainer;
	return ShapeType::Unknown;
}

uint32_t HkxFile::ReadU32(const uint8_t* ptr) const {
	uint32_t val;
	std::memcpy(&val, ptr, 4);
	return val;
}

uint16_t HkxFile::ReadU16(const uint8_t* ptr) const {
	uint16_t val;
	std::memcpy(&val, ptr, 2);
	return val;
}

int8_t HkxFile::ReadI8(const uint8_t* ptr) const {
	return static_cast<int8_t>(*ptr);
}

uint8_t HkxFile::ReadU8(const uint8_t* ptr) const {
	return *ptr;
}

float HkxFile::ReadFloat(const uint8_t* ptr) const {
	float val;
	std::memcpy(&val, ptr, 4);
	return val;
}

Vector4 HkxFile::ReadVector4(const uint8_t* ptr) const {
	Vector4 v;
	v.x = ReadFloat(ptr);
	v.y = ReadFloat(ptr + 4);
	v.z = ReadFloat(ptr + 8);
	v.w = ReadFloat(ptr + 12);
	return v;
}

Quaternion HkxFile::ReadQuat(const uint8_t* ptr) const {
	Quaternion q;
	q.x = ReadFloat(ptr);
	q.y = ReadFloat(ptr + 4);
	q.z = ReadFloat(ptr + 8);
	q.w = ReadFloat(ptr + 12);
	return q;
}

Transform HkxFile::ReadTransform(const uint8_t* ptr) const {
	Transform t;
	t.col0 = ReadVector4(ptr);
	t.col1 = ReadVector4(ptr + 16);
	t.col2 = ReadVector4(ptr + 32);
	t.translation = ReadVector4(ptr + 48);
	return t;
}

} // namespace Hkx
