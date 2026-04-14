// Shared domain types for HKX file parsing.
//
// These types represent Havok physics and scene objects extracted from both
// binary packfile and tagged binary formats. They are format-independent:
// both HkxFile (binary) and HkxTaggedReader (tagged) populate them.
//
// References:
//   - HKXDocs (github.com/SimonNitzsche/HKXDocs) — HKX format documentation
//   - Ghidra reverse engineering of the original LEGO Universe client binary
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <memory>

namespace Hkx {

// --- Magic constants ---

// Havok binary packfile magic numbers
constexpr uint32_t BINARY_MAGIC_0 = 0x57E0E057;
constexpr uint32_t BINARY_MAGIC_1 = 0x10C0C010;

// Havok tagged binary format magic
constexpr uint32_t TAGGED_MAGIC_0 = 0xCAB00D1E;
constexpr uint32_t TAGGED_MAGIC_1 = 0xD011FACE;

// --- Class name entry ---

struct ClassEntry {
	uint32_t signature;
	std::string name;
};

// --- Basic math types ---

struct Vector4 {
	float x = 0, y = 0, z = 0, w = 0;
};

struct Quaternion {
	float x = 0, y = 0, z = 0, w = 1;
};

struct Transform {
	// Rotation as 3 column vectors (3x3 matrix stored column-major)
	Vector4 col0{1, 0, 0, 0};
	Vector4 col1{0, 1, 0, 0};
	Vector4 col2{0, 0, 1, 0};
	// Translation
	Vector4 translation{};
};

// --- Shape types ---

enum class ShapeType {
	Unknown,
	Box,
	Sphere,
	Capsule,
	Cylinder,
	ConvexVertices,
	ConvexTransform,
	ConvexTranslate,
	Mopp,
	List,
	Transform,
	Triangle,
	BvTree,
	CompressedMesh,
	ExtendedMesh,
	SimpleContainer,
};

// Full hkpConvexVerticesShape FourVectors (stores 4 vertices transposed)
struct FourTransposedPoints {
	Vector4 xs; // x coordinates of 4 vertices
	Vector4 ys; // y coordinates of 4 vertices
	Vector4 zs; // z coordinates of 4 vertices
};

struct ShapeInfo {
	ShapeType type = ShapeType::Unknown;
	std::string className;
	uint32_t dataOffset = 0;

	// Common fields (from hkpShape)
	uint32_t userData = 0;
	uint32_t shapeTypeEnum = 0;

	// hkpConvexShape base
	float radius = 0.0f;

	// hkpBoxShape
	Vector4 halfExtents{};

	// hkpCapsuleShape / hkpCylinderShape
	Vector4 vertexA{};
	Vector4 vertexB{};

	// hkpCylinderShape specific
	float cylRadius = 0.0f;
	float cylBaseRadiusFactor = 0.0f;
	Vector4 perpendicular1{};
	Vector4 perpendicular2{};

	// hkpConvexVerticesShape
	Vector4 aabbHalfExtents{};
	Vector4 aabbCenter{};
	Vector4 aabbMin{};
	Vector4 aabbMax{};
	int32_t numVertices = 0;
	std::vector<FourTransposedPoints> rotatedVertices;
	std::vector<Vector4> planeEquations;

	// hkpConvexTransformShape
	Hkx::Transform childTransform{};

	// hkpConvexTranslateShape
	Vector4 translation{};

	// hkpMoppBvTreeShape
	uint32_t moppCodeOffset = 0;

	// hkpListShape
	bool disableWelding = false;
	uint8_t collectionType = 0;
	uint16_t listFlags = 0;
	uint16_t numDisabledChildren = 0;
	Vector4 listAabbHalfExtents{};
	Vector4 listAabbCenter{};

	// hkpSimpleMeshShape
	float meshRadius = 0.0f;
	uint8_t weldingType = 0;
	int32_t numTriangles = 0;

	// Triangle indices (for SimpleMeshShape) - each triangle is 3 uint32_t vertex indices
	struct Triangle { uint32_t a, b, c; };
	std::vector<Triangle> triangles;

	// hkpCompressedMeshShape (Havok 5.5)
	//
	// Contains quantized collision mesh data in three forms:
	//   1. Chunks — quantized vertex grids with triangle indices
	//   2. BigTriangles — non-quantized triangles using bigVertices
	//   3. ConvexPieces — convex hulls with quantized vertices
	//
	// Vertex dequantization formula (for chunks and convex pieces):
	//   world_pos = chunk.offset + float3(quantized_u16) * error
	// where error is a global scalar from the shape.

	// Chunk: a spatial region containing quantized vertices and triangle indices.
	// Havok class: hkpCompressedMeshShape::Chunk (80 bytes / 0x50)
	//
	// Serialized layout from Ghidra RE of Havok 5.5 reflection data:
	//   +0x00: offset        hkVector4       (16 bytes) — chunk origin for dequantization
	//   +0x10: vertices      hkArray<u16>    (12 bytes) — quantized vertex coords (x0,y0,z0,x1,y1,z1,...)
	//   +0x1C: indices       hkArray<u16>    (12 bytes) — triangle vertex indices (groups of 3)
	//   +0x28: stripLengths  hkArray<u16>    (12 bytes) — triangle strip lengths (for strip-based encoding)
	//   +0x34: weldingInfo   hkArray<u16>    (12 bytes) — per-triangle welding data
	//   +0x40: materialInfo  u32             (4 bytes)  — material lookup info
	//   +0x44: reference     u16             (2 bytes)  — reference chunk index (0xFFFF if none)
	//   +0x46: transformIndex u16            (2 bytes)  — index into transforms array (0xFFFF if none)
	//   +0x48: padding                       (8 bytes)
	struct CompressedMeshChunk {
		Vector4 offset{};                        // +0x00: dequantization origin
		std::vector<uint16_t> vertices;          // +0x10: quantized (x,y,z) triples as u16
		std::vector<uint16_t> indices;           // +0x1C: triangle vertex indices (groups of 3)
		std::vector<uint16_t> stripLengths;      // +0x28: triangle strip lengths
		std::vector<uint16_t> weldingInfo;       // +0x34: per-triangle welding data
		uint32_t materialInfo = 0;               // +0x40: material lookup
		uint16_t reference = 0xFFFF;             // +0x44: reference chunk index
		uint16_t transformIndex = 0xFFFF;        // +0x46: index into transforms
	};

	// BigTriangle: a full-precision triangle using bigVertices indices.
	// Havok class: hkpCompressedMeshShape::BigTriangle (16 bytes / 0x10)
	//
	// Serialized layout:
	//   +0x00: a              u16     — index into bigVertices
	//   +0x02: b              u16     — index into bigVertices
	//   +0x04: c              u16     — index into bigVertices
	//   +0x06: padding        (2 bytes)
	//   +0x08: material       u32     — material index
	//   +0x0C: weldingInfo    u16     — welding data
	//   +0x0E: padding        (2 bytes)
	struct CompressedMeshBigTriangle {
		uint16_t a = 0, b = 0, c = 0;  // vertex indices into bigVertices
		uint32_t material = 0;
		uint16_t weldingInfo = 0;
	};

	// ConvexPiece: a convex hull with quantized vertices and face topology.
	// Havok class: hkpCompressedMeshShape::ConvexPiece (64 bytes / 0x40)
	//
	// Serialized layout:
	//   +0x00: offset          hkVector4       (16 bytes) — dequantization origin
	//   +0x10: vertices        hkArray<u16>    (12 bytes) — quantized vertex coords (x,y,z triples)
	//   +0x1C: faceVertices    hkArray<u8>     (12 bytes) — face vertex indices
	//   +0x28: faceOffsets     hkArray<u16>    (12 bytes) — offsets into faceVertices per face
	//   +0x34: reference       u16             (2 bytes)  — reference piece index (0xFFFF if none)
	//   +0x36: transformIndex  u16             (2 bytes)  — index into transforms (0xFFFF if none)
	//   +0x38: padding                         (8 bytes)
	struct CompressedMeshConvexPiece {
		Vector4 offset{};                        // dequantization origin
		std::vector<uint16_t> vertices;          // quantized vertex coords
		std::vector<uint8_t> faceVertices;       // face vertex indices
		std::vector<uint16_t> faceOffsets;       // per-face offsets into faceVertices
		uint16_t reference = 0xFFFF;
		uint16_t transformIndex = 0xFFFF;
	};

	// Full hkpCompressedMeshShape data (extracted)
	struct CompressedMeshData {
		// Key parameters
		int32_t bitsPerIndex = 0;
		int32_t bitsPerWIndex = 0;
		int32_t wIndexMask = 0;
		int32_t indexMask = 0;
		float error = 0.0f;                     // quantization scale for dequantization

		// Transforms applied to chunks/convex pieces
		std::vector<Transform> transforms;

		// Full-precision vertices for BigTriangles
		std::vector<Vector4> bigVertices;

		// The three geometry containers
		std::vector<CompressedMeshBigTriangle> bigTriangles;
		std::vector<CompressedMeshChunk> chunks;
		std::vector<CompressedMeshConvexPiece> convexPieces;
	};

	// Populated when type == CompressedMesh
	std::shared_ptr<CompressedMeshData> compressedMesh;

	// For container shapes
	std::vector<ShapeInfo> children;
	std::vector<uint32_t> childCollisionFilterInfos; // per-child filter info for ListShape
};

// --- Motion types from Havok ---

enum class MotionType : uint8_t {
	Invalid = 0,
	Dynamic = 1,
	SphereInertia = 2,
	BoxInertia = 3,
	Keyframed = 4,
	Fixed = 5,
	ThinBoxInertia = 6,
	Character = 7,
};

// --- Rigid body ---

struct MotionState {
	Transform transform;       // position + rotation matrix
	// SweptTransform
	Vector4 centerOfMass0{};
	Vector4 centerOfMass1{};
	Quaternion rotation0;
	Quaternion rotation1;
	Vector4 centerOfMassLocal{};
	Vector4 deltaAngle{};
	float objectRadius = 0.0f;
	float linearDamping = 0.0f;
	float angularDamping = 0.0f;
	float maxLinearVelocity = 0.0f;
	float maxAngularVelocity = 0.0f;
	uint8_t deactivationClass = 0;
};

struct MotionInfo {
	MotionType type = MotionType::Invalid;
	uint8_t deactivationIntegrateCounter = 0;
	uint16_t deactivationNumInactiveFrames[2] = {};
	MotionState motionState;
	Vector4 inertiaAndMassInv{};  // .xyz = inv inertia diagonal, .w = inv mass
	Vector4 linearVelocity{};
	Vector4 angularVelocity{};
	Vector4 deactivationRefPosition[2] = {};
	float gravityFactor = 1.0f;
};

struct MaterialInfo {
	int8_t responseType = 0;
	float friction = 0.5f;
	float restitution = 0.4f;
	float rollingFrictionMultiplier = 0.0f;
};

struct RigidBodyInfo {
	std::string name;
	uint32_t dataOffset = 0;

	// Collidable info
	uint32_t shapeOffset = 0;
	uint32_t shapeKey = 0xFFFFFFFF;
	uint32_t collisionFilterInfo = 0;
	int8_t qualityType = 0;
	float allowedPenetrationDepth = 0.0f;

	// User data
	uint32_t userData = 0;

	// Material
	MaterialInfo material;

	// Damage
	float damageMultiplier = 1.0f;

	// Entity info
	uint16_t contactPointCallbackDelay = 0xFFFF;
	int8_t autoRemoveLevel = 0;
	uint8_t numShapeKeysInContactPointProperties = 0;
	uint8_t responseModifierFlags = 0;
	uint32_t uid = 0xFFFFFFFF;

	// Motion
	MotionInfo motion;

	// Extracted convenience fields
	Vector4 position{};      // from motion.motionState.transform.translation
	Quaternion rotation;     // from motion.motionState.sweptTransform.rotation1
	float mass = 0.0f;      // 1.0 / motion.inertiaAndMassInv.w
	float friction = 0.5f;  // from material
	float restitution = 0.4f;

	ShapeInfo shape;
};

// --- Physics system ---

struct PhysicsSystemInfo {
	std::string name;
	uint32_t userData = 0;
	bool active = true;
	std::vector<uint32_t> rigidBodyOffsets;
	std::vector<uint32_t> constraintOffsets;
	std::vector<uint32_t> actionOffsets;
	std::vector<uint32_t> phantomOffsets;
	std::vector<RigidBodyInfo> rigidBodies; // resolved
};

// --- Physics data ---

struct PhysicsDataInfo {
	uint32_t worldCinfoOffset = 0;
	std::vector<uint32_t> systemOffsets;
	std::vector<PhysicsSystemInfo> systems; // resolved
};

// --- Scene data (hkxScene, hkxMesh, hkxNode) ---

struct SceneMesh {
	std::string name;
	int lodLevel = 0;                    // LOD index (0 = highest detail)
	std::vector<Vector4> vertices;       // position data
	std::vector<Vector4> normals;        // normal data (optional)
	std::vector<uint8_t> sceneIDs;       // per-vertex scene ID (from terrain sceneMap)
	struct Triangle { uint32_t a, b, c; };
	std::vector<Triangle> triangles;
};

struct SceneNode {
	std::string name;
	Transform transform;                 // local transform
	int meshIndex = -1;                  // index into SceneInfo::meshes, -1 if no mesh
	std::vector<int> childIndices;       // indices into SceneInfo::nodes
};

struct SceneInfo {
	std::string modeller;                // authoring tool
	std::vector<SceneMesh> meshes;
	std::vector<SceneNode> nodes;
	int rootNodeIndex = -1;
};

// --- Root level container ---

struct NamedVariant {
	std::string name;
	std::string className;
	uint32_t objectOffset = 0;
};

struct RootLevelContainerInfo {
	std::vector<NamedVariant> namedVariants;
};

// --- Parse result ---

struct ParseResult {
	bool success = false;
	std::string error;
	std::string havokVersion;
	uint32_t fileVersion = 0;
	uint8_t pointerSize = 0;

	std::vector<ClassEntry> classEntries;
	std::vector<RootLevelContainerInfo> rootContainers;
	std::vector<PhysicsDataInfo> physicsData;
	std::vector<PhysicsSystemInfo> physicsSystems;
	std::vector<RigidBodyInfo> rigidBodies;
	std::vector<ShapeInfo> shapes;
	std::vector<SceneInfo> scenes;

	// All objects found via virtual fixups (classname -> list of data offsets)
	std::unordered_map<std::string, std::vector<uint32_t>> objectsByClass;
};

} // namespace Hkx
