// Binary packfile physics object extraction.
//
// Reads Havok physics shapes, rigid bodies, physics systems, physics data,
// and root level containers from the raw data section of a binary packfile.
// Uses the field offsets defined in HkxOffsets.h and pointer resolution
// (local/global fixups) provided by HkxFile.

#include "havok/reader/hkx_reader.h"
#include "havok/reader/hkx_binary_offsets.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace Hkx {

void HkxFile::ReadShapeCommon(ShapeInfo& info, const uint8_t* objData, size_t remaining) {
	if (remaining >= 16) {
		info.userData = ReadU32(objData + Off::Shape_UserData);
		info.shapeTypeEnum = ReadU32(objData + Off::Shape_Type);
	}
	if (remaining >= 20) {
		info.radius = ReadFloat(objData + Off::ConvexShape_Radius);
	}
}

ShapeInfo HkxFile::ReadShape(uint32_t offset, int depth) {
	ShapeInfo info;
	info.dataOffset = offset;

	if (depth > 10) return info;

	auto classIt = m_ObjectClasses.find(offset);
	if (classIt == m_ObjectClasses.end()) return info;

	info.className = classIt->second;
	info.type = ClassifyShape(info.className);
	if (info.type == ShapeType::Unknown) return info;

	if (offset >= m_DataSectionSize) return info;

	const uint8_t* objData = m_DataSection + offset;
	size_t remaining = m_DataSectionSize - offset;

	ReadShapeCommon(info, objData, remaining);

	switch (info.type) {
	case ShapeType::Box:
		if (remaining >= Off::BoxShape_HalfExtents + 16) {
			info.halfExtents = ReadVector4(objData + Off::BoxShape_HalfExtents);
		}
		break;

	case ShapeType::Sphere:
		// Radius already read in ReadShapeCommon
		break;

	case ShapeType::Capsule:
		if (remaining >= Off::CapsuleShape_VertexB + 16) {
			info.vertexA = ReadVector4(objData + Off::CapsuleShape_VertexA);
			info.vertexB = ReadVector4(objData + Off::CapsuleShape_VertexB);
		}
		break;

	case ShapeType::Cylinder:
		if (remaining >= Off::CylinderShape_Perp2 + 16) {
			info.cylRadius = ReadFloat(objData + Off::CylinderShape_CylRadius);
			info.cylBaseRadiusFactor = ReadFloat(objData + Off::CylinderShape_CylBaseFactor);
			info.vertexA = ReadVector4(objData + Off::CylinderShape_VertexA);
			info.vertexB = ReadVector4(objData + Off::CylinderShape_VertexB);
			info.perpendicular1 = ReadVector4(objData + Off::CylinderShape_Perp1);
			info.perpendicular2 = ReadVector4(objData + Off::CylinderShape_Perp2);
		}
		break;

	case ShapeType::ConvexVertices: {
		if (remaining >= Off::CVS_Connectivity(m_Header.fileVersion) + 4) {
			info.aabbHalfExtents = ReadVector4(objData + Off::CVS_AabbHalfExtents);
			info.aabbCenter = ReadVector4(objData + Off::CVS_AabbCenter);
			info.halfExtents = info.aabbHalfExtents;
			info.aabbMin = { info.aabbCenter.x - info.aabbHalfExtents.x,
							 info.aabbCenter.y - info.aabbHalfExtents.y,
							 info.aabbCenter.z - info.aabbHalfExtents.z, 0 };
			info.aabbMax = { info.aabbCenter.x + info.aabbHalfExtents.x,
							 info.aabbCenter.y + info.aabbHalfExtents.y,
							 info.aabbCenter.z + info.aabbHalfExtents.z, 0 };
			info.numVertices = static_cast<int32_t>(ReadU32(objData + Off::CVS_NumVertices));

			// Read rotated vertices array via fixup
			uint32_t vertArraySize = ReadU32(objData + Off::CVS_RotatedVertices + 4);
			uint32_t vertArrayDst = ResolveLocalPointer(offset + Off::CVS_RotatedVertices);
			if (vertArrayDst != 0xFFFFFFFF && vertArraySize > 0 &&
				vertArrayDst + vertArraySize * 48 <= m_DataSectionSize) {
				for (uint32_t i = 0; i < vertArraySize; i++) {
					const uint8_t* fvp = m_DataSection + vertArrayDst + i * 48;
					FourTransposedPoints ftp;
					ftp.xs = ReadVector4(fvp);
					ftp.ys = ReadVector4(fvp + 16);
					ftp.zs = ReadVector4(fvp + 32);
					info.rotatedVertices.push_back(ftp);
				}
			}

			// Read plane equations array via fixup (offset differs by version)
			uint32_t peOff = Off::CVS_PlaneEquations(m_Header.fileVersion);
			uint32_t planeArraySize = ReadU32(objData + peOff + 4);
			uint32_t planeArrayDst = ResolveLocalPointer(offset + peOff);
			if (planeArrayDst != 0xFFFFFFFF && planeArraySize > 0 &&
				planeArrayDst + planeArraySize * 16 <= m_DataSectionSize) {
				for (uint32_t i = 0; i < planeArraySize; i++) {
					info.planeEquations.push_back(
						ReadVector4(m_DataSection + planeArrayDst + i * 16));
				}
			}
		}
		break;
	}

	case ShapeType::ConvexTransform: {
		// Child shape via fixup (global fixup - inter-object pointer)
		uint32_t childOffset = ResolvePointer(offset + Off::CTSB_ChildShape);
		if (childOffset != 0xFFFFFFFF) {
			info.children.push_back(ReadShape(childOffset, depth + 1));
		}
		// Full 4x4 transform
		if (remaining >= Off::CTS_Transform + 64) {
			info.childTransform = ReadTransform(objData + Off::CTS_Transform);
		}
		break;
	}

	case ShapeType::ConvexTranslate: {
		uint32_t childOffset = ResolvePointer(offset + Off::CTSB_ChildShape);
		if (childOffset != 0xFFFFFFFF) {
			info.children.push_back(ReadShape(childOffset, depth + 1));
		}
		if (remaining >= Off::CTrS_Translation + 16) {
			info.translation = ReadVector4(objData + Off::CTrS_Translation);
		}
		break;
	}

	case ShapeType::Mopp: {
		if (remaining >= Off::Mopp_Child + 4) {
			info.moppCodeOffset = ResolvePointer(offset + Off::Mopp_Code);
			uint32_t childOffset = ResolvePointer(offset + Off::Mopp_Child);
			if (childOffset != 0xFFFFFFFF) {
				info.children.push_back(ReadShape(childOffset, depth + 1));
			}
		}
		break;
	}

	case ShapeType::List: {
		if (remaining >= Off::List_AabbCenter + 16) {
			info.disableWelding = ReadU8(objData + Off::ShapeCollection_DisableWelding) != 0;
			info.collectionType = ReadU8(objData + Off::ShapeCollection_CollectionType);
			info.listFlags = ReadU16(objData + Off::List_Flags);
			info.numDisabledChildren = ReadU16(objData + Off::List_NumDisabledChildren);
			info.listAabbHalfExtents = ReadVector4(objData + Off::List_AabbHalfExtents);
			info.listAabbCenter = ReadVector4(objData + Off::List_AabbCenter);

			// Read child info array
			uint32_t childInfoSize = ReadU32(objData + Off::List_ChildInfo + 4);
			uint32_t childInfoDst = ResolveLocalPointer(offset + Off::List_ChildInfo);

			if (childInfoDst != 0xFFFFFFFF && childInfoSize > 0) {
				// Each serialized ChildInfo is 16 bytes:
				//   shape ptr (4) + collisionFilterInfo (4) + numChildShapeKeys (4) + shapeKey (4)
				for (uint32_t i = 0; i < childInfoSize; i++) {
					uint32_t childEntryOff = childInfoDst + i * 16;
					// The shape ptr in the child info is a global fixup (inter-object pointer)
					uint32_t childShapeOff = ResolvePointer(childEntryOff);
					if (childShapeOff != 0xFFFFFFFF) {
						info.children.push_back(ReadShape(childShapeOff, depth + 1));
					}
					if (childEntryOff + 8 <= m_DataSectionSize) {
						info.childCollisionFilterInfos.push_back(
							ReadU32(m_DataSection + childEntryOff + 4));
					}
				}
			}
		}
		break;
	}

	case ShapeType::Transform: {
		uint32_t childOffset = ResolvePointer(offset + Off::TransformShape_Child);
		if (childOffset != 0xFFFFFFFF) {
			info.children.push_back(ReadShape(childOffset, depth + 1));
		}
		if (remaining >= Off::TransformShape_Transform + 64) {
			info.childTransform = ReadTransform(objData + Off::TransformShape_Transform);
		}
		break;
	}

	case ShapeType::SimpleContainer: {
		info.disableWelding = ReadU8(objData + Off::ShapeCollection_DisableWelding) != 0;
		info.collectionType = ReadU8(objData + Off::ShapeCollection_CollectionType);

		if (remaining >= Off::SimpleMesh_WeldingType + 1) {
			info.meshRadius = ReadFloat(objData + Off::SimpleMesh_Radius);
			info.weldingType = ReadU8(objData + Off::SimpleMesh_WeldingType);

			uint32_t numTris = ReadU32(objData + Off::SimpleMesh_Triangles + 4);
			info.numTriangles = static_cast<int32_t>(numTris);

			// Read vertex count
			uint32_t numVerts = ReadU32(objData + Off::SimpleMesh_Vertices + 4);
			info.numVertices = static_cast<int32_t>(numVerts);

			// Read actual vertices via fixup
			uint32_t vertDst = ResolveLocalPointer(offset + Off::SimpleMesh_Vertices);
			if (vertDst != 0xFFFFFFFF && numVerts > 0 &&
				vertDst + numVerts * 16 <= m_DataSectionSize) {
				for (uint32_t i = 0; i < numVerts; i++) {
					info.planeEquations.push_back( // reuse planeEquations as vertex storage
						ReadVector4(m_DataSection + vertDst + i * 16));
				}
			}

			// Read triangle indices via fixup
			// Each triangle is 3x uint32_t (vertex indices) + 1x uint32_t (welding info) = 16 bytes
			uint32_t triDst = ResolveLocalPointer(offset + Off::SimpleMesh_Triangles);
			if (triDst != 0xFFFFFFFF && numTris > 0 &&
				triDst + numTris * 16 <= m_DataSectionSize) {
				for (uint32_t i = 0; i < numTris; i++) {
					const uint8_t* triData = m_DataSection + triDst + i * 16;
					ShapeInfo::Triangle tri;
					tri.a = ReadU32(triData);
					tri.b = ReadU32(triData + 4);
					tri.c = ReadU32(triData + 8);
					info.triangles.push_back(tri);
				}
			}
		}
		break;
	}

	case ShapeType::ExtendedMesh: {
		// hkpStorageExtendedMeshShape / hkpExtendedMeshShape
		// Read mesh subpart storage objects to extract vertices and triangles.
		// The shape has meshSubparts array; each subpart points to a storage object
		// containing vertex and index arrays.
		info.disableWelding = ReadU8(objData + Off::ShapeCollection_DisableWelding) != 0;
		info.collectionType = ReadU8(objData + Off::ShapeCollection_CollectionType);

		// Scan fixups in a wide range and also follow one level of indirection
		// (ExtendedMeshShape -> subpart array -> storage objects)
		auto fixups = GetFixupsInRange(offset, offset + 512);
		// Also follow destinations to find storage objects
		std::vector<std::pair<uint32_t, uint32_t>> extraFixups;
		for (const auto& [src, dst] : fixups) {
			auto indirect = GetFixupsInRange(dst, dst + 256);
			extraFixups.insert(extraFixups.end(), indirect.begin(), indirect.end());
		}
		fixups.insert(fixups.end(), extraFixups.begin(), extraFixups.end());

		for (const auto& [src, dst] : fixups) {
			auto childClassIt = m_ObjectClasses.find(dst);
			if (childClassIt == m_ObjectClasses.end()) continue;
			const auto& childClass = childClassIt->second;

			if (childClass.find("MeshSubpartStorage") != std::string::npos) {
				// hkpStorageExtendedMeshShapeMeshSubpartStorage
				// Layout: vtable(4) + memSizeAndFlags(2) + refCount(2) = 8 bytes base
				// +0x08: vertices hkArray<hkVector4> (ptr, size, cap) - 16 bytes per vertex
				// +0x14: indices8 hkArray<uint8>
				// +0x20: indices16 hkArray<uint16>
				// +0x2C: indices32 hkArray<uint32> - 4 uint32 per triangle (a,b,c,0)
				if (dst + 0x38 > m_DataSectionSize) continue;
				const uint8_t* storData = m_DataSection + dst;

				uint32_t numVerts = ReadU32(storData + 0x0C);
				uint32_t numIdx32 = ReadU32(storData + 0x30);

				uint32_t vertDst = ResolveLocalPointer(dst + 0x08);
				uint32_t idx32Dst = ResolveLocalPointer(dst + 0x2C);

				// Track vertex base for this subpart (indices are local to subpart)
				uint32_t vertexBase = static_cast<uint32_t>(info.planeEquations.size());

				// Read vertices (hkVector4: x, y, z, w)
				if (vertDst != 0xFFFFFFFF && numVerts > 0 && numVerts < 100000 &&
					vertDst + numVerts * 16 <= m_DataSectionSize) {
					for (uint32_t v = 0; v < numVerts; v++) {
						info.planeEquations.push_back(
							ReadVector4(m_DataSection + vertDst + v * 16));
					}
				}

				// Read triangle indices (every 4th uint32 is padding/material)
				// Offset indices by vertexBase for multi-subpart meshes
				bool gotTriangles = false;
				if (idx32Dst != 0xFFFFFFFF && numIdx32 > 0 && numIdx32 < 1000000 &&
					idx32Dst + numIdx32 * 4 <= m_DataSectionSize) {
					// Stride: 4 uint32 per triangle (a, b, c, materialIndex/padding)
					uint32_t numTris = numIdx32 / 4;
					for (uint32_t t = 0; t < numTris; t++) {
						const uint8_t* triData = m_DataSection + idx32Dst + t * 16;
						ShapeInfo::Triangle tri;
						tri.a = ReadU32(triData) + vertexBase;
						tri.b = ReadU32(triData + 4) + vertexBase;
						tri.c = ReadU32(triData + 8) + vertexBase;
						info.triangles.push_back(tri);
					}
					gotTriangles = true;
				}

				// Also check indices16 if indices32 didn't have data
				if (!gotTriangles) {
					uint32_t numIdx16 = ReadU32(storData + 0x24);
					uint32_t idx16Dst = ResolveLocalPointer(dst + 0x20);
					if (idx16Dst != 0xFFFFFFFF && numIdx16 >= 3 &&
						idx16Dst + numIdx16 * 2 <= m_DataSectionSize) {
						uint32_t numTris = numIdx16 / 3;
						for (uint32_t t = 0; t < numTris; t++) {
							ShapeInfo::Triangle tri;
							tri.a = ReadU16(m_DataSection + idx16Dst + t * 6) + vertexBase;
							tri.b = ReadU16(m_DataSection + idx16Dst + t * 6 + 2) + vertexBase;
							tri.c = ReadU16(m_DataSection + idx16Dst + t * 6 + 4) + vertexBase;
							info.triangles.push_back(tri);
						}
					}
				}

				info.numVertices = static_cast<int32_t>(info.planeEquations.size());
				info.numTriangles = static_cast<int32_t>(info.triangles.size());
			}
		}

		// Change type to SimpleContainer so the renderer draws it as a mesh
		if (!info.planeEquations.empty()) {
			info.type = ShapeType::SimpleContainer;
		}
		break;
	}

	case ShapeType::CompressedMesh: {
		ReadCompressedMeshShape(info, offset, objData, remaining);
		break;
	}

	case ShapeType::BvTree:
	case ShapeType::Triangle:
		// BvTree/Triangle shapes: follow fixups to find child shapes
		{
			auto fixups = GetFixupsInRange(offset, offset + 256);
			for (const auto& [src, dst] : fixups) {
				auto childClassIt = m_ObjectClasses.find(dst);
				if (childClassIt != m_ObjectClasses.end()) {
					ShapeType childType = ClassifyShape(childClassIt->second);
					if (childType != ShapeType::Unknown) {
						info.children.push_back(ReadShape(dst, depth + 1));
					}
				}
			}
		}
		break;

	default:
		break;
	}

	return info;
}

RigidBodyInfo HkxFile::ReadRigidBody(uint32_t offset) {
	RigidBodyInfo rb;
	rb.dataOffset = offset;

	if (offset >= m_DataSectionSize) return rb;

	const uint8_t* objData = m_DataSection + offset;
	auto sizeIt = m_ObjectSizes.find(offset);
	size_t objSize = (sizeIt != m_ObjectSizes.end()) ? sizeIt->second : (m_DataSectionSize - offset);
	size_t remaining = std::min(objSize, m_DataSectionSize - offset);

	// hkpRigidBody layout — exact offsets from Havok reflection metadata.
	//
	// All offsets verified via hkClass/hkClassMember tables extracted from
	// legouniverse.exe (Havok 5.5, 32-bit). Binary packfiles store objects
	// at their full runtime size (0x220 bytes) with nosave fields zeroed.
	// See HkxOffsets.h for the complete field map.

	// --- Shape pointer (at +0x10, hkpCdBody::m_shape) ---
	uint32_t shapeOff = ResolvePointer(offset + Off::RB_Shape);
	if (shapeOff != 0xFFFFFFFF) {
		auto classIt = m_ObjectClasses.find(shapeOff);
		if (classIt != m_ObjectClasses.end() && ClassifyShape(classIt->second) != ShapeType::Unknown) {
			rb.shapeOffset = shapeOff;
			rb.shape = ReadShape(shapeOff);
		}
	}

	// Fallback: search first few fixups if shape not found at canonical offset
	if (rb.shapeOffset == 0) {
		auto fixups = GetFixupsInRange(offset, offset + 64);
		for (const auto& [src, dst] : fixups) {
			auto classIt = m_ObjectClasses.find(dst);
			if (classIt != m_ObjectClasses.end() && ClassifyShape(classIt->second) != ShapeType::Unknown) {
				rb.shapeOffset = dst;
				rb.shape = ReadShape(dst);
				break;
			}
		}
	}

	// --- Scalar fields at known offsets ---

	if (remaining >= Off::RB_ShapeKey + 4) {
		rb.shapeKey = ReadU32(objData + Off::RB_ShapeKey);
	}

	if (remaining >= Off::RB_UserData + 4) {
		rb.userData = ReadU32(objData + Off::RB_UserData);
	}

	if (remaining >= Off::RB_CollFilterInfo + 4) {
		rb.collisionFilterInfo = ReadU32(objData + Off::RB_CollFilterInfo);
	}

	if (remaining >= Off::RB_QualityType + 1) {
		rb.qualityType = ReadI8(objData + Off::RB_QualityType);
	}

	if (remaining >= Off::RB_AllowedPenDepth + 4) {
		rb.allowedPenetrationDepth = ReadFloat(objData + Off::RB_AllowedPenDepth);
	}

	// --- Material (hkpMaterial at +0x8C, 12 bytes) ---

	if (remaining >= Off::RB_Restitution + 4) {
		rb.material.responseType = ReadI8(objData + Off::RB_ResponseType);
		rb.material.friction = ReadFloat(objData + Off::RB_Friction);
		rb.material.restitution = ReadFloat(objData + Off::RB_Restitution);
		rb.friction = rb.material.friction;
		rb.restitution = rb.material.restitution;
	}

	// --- Entity fields ---

	if (remaining >= Off::RB_DamageMultiplier + 4) {
		rb.damageMultiplier = ReadFloat(objData + Off::RB_DamageMultiplier);
	}

	if (remaining >= Off::RB_ContactCBDelay + 2) {
		rb.contactPointCallbackDelay = ReadU16(objData + Off::RB_ContactCBDelay);
	}

	if (remaining >= Off::RB_RespModFlags + 1) {
		rb.autoRemoveLevel = ReadI8(objData + Off::RB_AutoRemoveLevel);
		rb.numShapeKeysInContactPointProperties = ReadU8(objData + Off::RB_NumShapeKeysCPP);
		rb.responseModifierFlags = ReadU8(objData + Off::RB_RespModFlags);
	}

	if (remaining >= Off::RB_UID + 4) {
		rb.uid = ReadU32(objData + Off::RB_UID);
	}

	// --- Motion (hkpMotion at +0xE0, 0x120 bytes) ---

	if (remaining >= Off::RB_MotionType + 1) {
		rb.motion.type = static_cast<MotionType>(ReadU8(objData + Off::RB_MotionType));
	}

	if (remaining >= Off::RB_DeactIntCounter + 1) {
		rb.motion.deactivationIntegrateCounter = ReadU8(objData + Off::RB_DeactIntCounter);
	}

	if (remaining >= Off::RB_DeactNumFrames + 4) {
		rb.motion.deactivationNumInactiveFrames[0] = ReadU16(objData + Off::RB_DeactNumFrames);
		rb.motion.deactivationNumInactiveFrames[1] = ReadU16(objData + Off::RB_DeactNumFrames + 2);
	}

	// --- MotionState transform (hkTransform at +0xF0, 64 bytes) ---

	if (remaining >= Off::RB_Translation + 16) {
		rb.motion.motionState.transform.col0 = ReadVector4(objData + Off::RB_TransformCol0);
		rb.motion.motionState.transform.col1 = ReadVector4(objData + Off::RB_TransformCol1);
		rb.motion.motionState.transform.col2 = ReadVector4(objData + Off::RB_TransformCol2);
		rb.motion.motionState.transform.translation = ReadVector4(objData + Off::RB_Translation);
		rb.position = rb.motion.motionState.transform.translation;
	}

	// --- SweptTransform (5 hkVector4s at +0x130) ---

	if (remaining >= Off::RB_COMLocal + 16) {
		rb.motion.motionState.centerOfMass0 = ReadVector4(objData + Off::RB_CenterOfMass0);
		rb.motion.motionState.centerOfMass1 = ReadVector4(objData + Off::RB_CenterOfMass1);
		rb.motion.motionState.rotation0 = ReadQuat(objData + Off::RB_Rotation0);
		rb.motion.motionState.rotation1 = ReadQuat(objData + Off::RB_Rotation1);
		rb.motion.motionState.centerOfMassLocal = ReadVector4(objData + Off::RB_COMLocal);
		rb.rotation = rb.motion.motionState.rotation1;
	}

	// --- MotionState scalar fields ---

	if (remaining >= Off::RB_DeltaAngle + 16) {
		rb.motion.motionState.deltaAngle = ReadVector4(objData + Off::RB_DeltaAngle);
	}

	if (remaining >= Off::RB_AngularDamping + 4) {
		rb.motion.motionState.objectRadius = ReadFloat(objData + Off::RB_ObjectRadius);
		rb.motion.motionState.linearDamping = ReadFloat(objData + Off::RB_LinearDamping);
		rb.motion.motionState.angularDamping = ReadFloat(objData + Off::RB_AngularDamping);
	}

	if (remaining >= Off::RB_DeactClass + 1) {
		// maxLinearVelocity and maxAngularVelocity are stored as UINT8 in the
		// packfile (quantized). The runtime float value is reconstructed as
		// value * 256.0 / 128.0 (Havok's hkUFloat8 encoding). We store the
		// raw byte cast to float for now; the caller can rescale if needed.
		rb.motion.motionState.maxLinearVelocity = static_cast<float>(ReadU8(objData + Off::RB_MaxLinVel));
		rb.motion.motionState.maxAngularVelocity = static_cast<float>(ReadU8(objData + Off::RB_MaxAngVel));
		rb.motion.motionState.deactivationClass = ReadU8(objData + Off::RB_DeactClass);
	}

	// --- Motion physics fields (inertia, velocity, etc.) ---

	if (remaining >= Off::RB_AngularVelocity + 16) {
		rb.motion.inertiaAndMassInv = ReadVector4(objData + Off::RB_InertiaInvMass);
		rb.motion.linearVelocity = ReadVector4(objData + Off::RB_LinearVelocity);
		rb.motion.angularVelocity = ReadVector4(objData + Off::RB_AngularVelocity);

		// Compute mass from inverse mass (.w component)
		if (rb.motion.inertiaAndMassInv.w != 0.0f) {
			rb.mass = 1.0f / rb.motion.inertiaAndMassInv.w;
		}
	}

	if (remaining >= Off::RB_DeactRefPos + 32) {
		rb.motion.deactivationRefPosition[0] = ReadVector4(objData + Off::RB_DeactRefPos);
		rb.motion.deactivationRefPosition[1] = ReadVector4(objData + Off::RB_DeactRefPos + 16);
	}

	if (remaining >= Off::RB_GravityFactor + 2) {
		// gravityFactor is stored as hkHalf (16-bit float). Havok's hkHalf uses
		// a simple encoding: value = int16 / 16384.0f (hkHalf::getReal()).
		// However, in packfile version 5 the field may also just be IEEE 754
		// half-precision. We use the Havok-specific encoding since that matches
		// the client's runtime behavior.
		int16_t halfRaw;
		std::memcpy(&halfRaw, objData + Off::RB_GravityFactor, 2);
		rb.motion.gravityFactor = static_cast<float>(halfRaw) / 16384.0f;
	}

	// --- Name (hkStringPtr at +0x74, resolved via fixup) ---

	uint32_t nameDst = ResolvePointer(offset + Off::RB_Name);
	if (nameDst != 0xFFFFFFFF && nameDst < m_DataSectionSize) {
		const char* str = reinterpret_cast<const char*>(m_DataSection + nameDst);
		size_t len = strnlen(str, m_DataSectionSize - nameDst);
		if (len > 0 && len < 256) {
			bool allPrint = true;
			for (size_t i = 0; i < len && allPrint; i++) {
				allPrint = (str[i] >= 0x20 && str[i] < 0x7F);
			}
			if (allPrint) {
				rb.name = std::string(str, len);
			}
		}
	}

	return rb;
}

PhysicsSystemInfo HkxFile::ReadPhysicsSystem(uint32_t offset) {
	PhysicsSystemInfo sys;

	if (offset >= m_DataSectionSize) return sys;

	const uint8_t* objData = m_DataSection + offset;
	size_t remaining = m_DataSectionSize - offset;

	if (remaining < Off::PhysSys_Active + 1) return sys;

	// Read rigid bodies array
	// Array data pointer is local fixup, but entries within are global fixups (object pointers)
	uint32_t rbCount = ReadU32(objData + Off::PhysSys_RigidBodies + 4);
	uint32_t rbArrayDst = ResolveLocalPointer(offset + Off::PhysSys_RigidBodies);
	if (rbArrayDst != 0xFFFFFFFF && rbCount > 0) {
		for (uint32_t i = 0; i < rbCount; i++) {
			uint32_t rbPtrOff = rbArrayDst + i * 4;
			uint32_t rbOff = ResolvePointer(rbPtrOff);
			if (rbOff != 0xFFFFFFFF) {
				sys.rigidBodyOffsets.push_back(rbOff);
				sys.rigidBodies.push_back(ReadRigidBody(rbOff));
			}
		}
	}

	// Read constraints array
	uint32_t constraintCount = ReadU32(objData + Off::PhysSys_Constraints + 4);
	uint32_t constraintArrayDst = ResolveLocalPointer(offset + Off::PhysSys_Constraints);
	if (constraintArrayDst != 0xFFFFFFFF && constraintCount > 0) {
		for (uint32_t i = 0; i < constraintCount; i++) {
			uint32_t off = ResolvePointer(constraintArrayDst + i * 4);
			if (off != 0xFFFFFFFF) sys.constraintOffsets.push_back(off);
		}
	}

	// Read actions array
	uint32_t actionCount = ReadU32(objData + Off::PhysSys_Actions + 4);
	uint32_t actionArrayDst = ResolveLocalPointer(offset + Off::PhysSys_Actions);
	if (actionArrayDst != 0xFFFFFFFF && actionCount > 0) {
		for (uint32_t i = 0; i < actionCount; i++) {
			uint32_t off = ResolvePointer(actionArrayDst + i * 4);
			if (off != 0xFFFFFFFF) sys.actionOffsets.push_back(off);
		}
	}

	// Read phantoms array
	uint32_t phantomCount = ReadU32(objData + Off::PhysSys_Phantoms + 4);
	uint32_t phantomArrayDst = ResolveLocalPointer(offset + Off::PhysSys_Phantoms);
	if (phantomArrayDst != 0xFFFFFFFF && phantomCount > 0) {
		for (uint32_t i = 0; i < phantomCount; i++) {
			uint32_t off = ResolvePointer(phantomArrayDst + i * 4);
			if (off != 0xFFFFFFFF) sys.phantomOffsets.push_back(off);
		}
	}

	// Name
	sys.name = ResolveString(offset + Off::PhysSys_Name);

	// UserData and active flag
	sys.userData = ReadU32(objData + Off::PhysSys_UserData);
	sys.active = ReadU8(objData + Off::PhysSys_Active) != 0;

	return sys;
}

PhysicsDataInfo HkxFile::ReadPhysicsData(uint32_t offset) {
	PhysicsDataInfo data;

	if (offset >= m_DataSectionSize) return data;

	const uint8_t* objData = m_DataSection + offset;

	data.worldCinfoOffset = ResolvePointer(offset + Off::PhysData_WorldCinfo);

	// Read systems array
	uint32_t sysCount = ReadU32(objData + Off::PhysData_Systems + 4);
	uint32_t sysArrayDst = ResolveLocalPointer(offset + Off::PhysData_Systems);
	if (sysArrayDst != 0xFFFFFFFF && sysCount > 0) {
		for (uint32_t i = 0; i < sysCount; i++) {
			uint32_t sysOff = ResolvePointer(sysArrayDst + i * 4);
			if (sysOff != 0xFFFFFFFF) {
				data.systemOffsets.push_back(sysOff);
				data.systems.push_back(ReadPhysicsSystem(sysOff));
			}
		}
	}

	return data;
}

RootLevelContainerInfo HkxFile::ReadRootLevelContainer(uint32_t offset) {
	RootLevelContainerInfo rlc;

	if (offset >= m_DataSectionSize) return rlc;

	const uint8_t* objData = m_DataSection + offset;

	// hkRootLevelContainer has m_namedVariants as hkArray (ptr + size + cap = 12 bytes)
	// Offset 0: ptr to NamedVariant array
	// Offset 4: size
	// Offset 8: capacityAndFlags

	uint32_t variantCount = ReadU32(objData + 4);
	uint32_t variantArrayDst = ResolveLocalPointer(offset);

	if (variantArrayDst != 0xFFFFFFFF && variantCount > 0 && variantCount < 100) {
		// Each NamedVariant is 12 bytes: name(ptr:4), className(ptr:4), variant(ptr:4)
		// Confirmed by XML: hkRootLevelContainerNamedVariant has 3 fields × 4 bytes
		for (uint32_t i = 0; i < variantCount; i++) {
			uint32_t entryOff = variantArrayDst + i * 12;
			NamedVariant nv;
			nv.name = ResolveString(entryOff);
			nv.className = ResolveString(entryOff + 4);
			nv.objectOffset = ResolvePointer(entryOff + 8);
			rlc.namedVariants.push_back(nv);
		}
	}

	return rlc;
}

SceneMesh HkxFile::ReadSceneMesh(uint32_t meshSectionOffset) {
	SceneMesh mesh;

	if (meshSectionOffset >= m_DataSectionSize) return mesh;
	const uint8_t* msData = m_DataSection + meshSectionOffset;

	// hkxMeshSection layout (Havok 7.x):
	// +0x00: vtable(4) + memSizeAndFlags(2) + refCount(2) = 8
	// +0x08: vertexBuffer ptr (global fixup to hkxVertexBuffer object)
	// +0x0C: indexBuffers hkArray (ptr, size, cap = 12)
	// +0x18: material ptr
	// +0x1C: userChannels hkArray

	// Get vertex buffer via fixup
	uint32_t vbOff = ResolvePointer(meshSectionOffset + 0x08);
	if (vbOff == 0xFFFFFFFF || vbOff >= m_DataSectionSize) return mesh;

	const uint8_t* vbData = m_DataSection + vbOff;

	// hkxVertexBuffer: inline hkxVertexBufferVertexData at +0x08
	// VertexData layout (from Ghidra RE):
	//   +0x08: vectorData hkArray<hkVector4> (ptr, size, cap)  — position/normal as hkVector4
	//   +0x14: floatData hkArray<float> (ptr, size, cap)
	//   +0x44: numVerts (uint32)
	//   +0x48: vectorStride (uint32) — hkVector4s per vertex in vectorData

	uint32_t numVerts = ReadU32(vbData + 0x44);
	uint32_t vecArraySize = ReadU32(vbData + 0x0C); // vectorData array size (total hkVector4 count)
	uint32_t vecStride = ReadU32(vbData + 0x48);    // vectorStride (bytes per vertex, 0 = blocked layout)

	if (numVerts == 0 || numVerts > 100000) return mesh;

	// Determine vec4s per vertex. vectorStride > 0 means interleaved layout
	// (position + normal etc. interleaved per vertex, stride in bytes).
	// vectorStride == 0 means blocked layout: all positions first, then all
	// normals. In blocked mode, positions are simply the first numVerts vec4s.
	uint32_t vecsPerVert;
	if (vecStride > 0) {
		vecsPerVert = vecStride / 16;
		if (vecsPerVert < 1) vecsPerVert = 1;
	} else {
		// Blocked layout — detect by checking if vecArraySize > numVerts
		// (positions + normals stored in separate contiguous blocks).
		// Positions are the first numVerts vec4 entries.
		vecsPerVert = 1;
	}

	bool blockedLayout = (vecStride == 0 && vecArraySize > numVerts);

	// Read vertex positions from vectorData
	uint32_t vecDataOff = ResolveLocalPointer(vbOff + 0x08);
	if (vecDataOff != 0xFFFFFFFF && vecDataOff + vecArraySize * 16 <= m_DataSectionSize) {
		mesh.vertices.reserve(numVerts);
		for (uint32_t v = 0; v < numVerts; v++) {
			// In interleaved mode: position at v * vecsPerVert
			// In blocked mode: position at v (consecutive)
			Vector4 pos = ReadVector4(m_DataSection + vecDataOff + v * vecsPerVert * 16);
			mesh.vertices.push_back(pos);
		}

		// Read normals
		if (blockedLayout && vecArraySize >= numVerts * 2) {
			// Blocked: normals start right after all positions
			uint32_t normOff = vecDataOff + numVerts * 16;
			for (uint32_t v = 0; v < numVerts; v++) {
				Vector4 nrm = ReadVector4(m_DataSection + normOff + v * 16);
				mesh.normals.push_back(nrm);
			}
		} else if (!blockedLayout && vecsPerVert >= 2) {
			// Interleaved: normal is the second vec4 per vertex
			for (uint32_t v = 0; v < numVerts; v++) {
				Vector4 nrm = ReadVector4(m_DataSection + vecDataOff + v * vecsPerVert * 16 + 16);
				mesh.normals.push_back(nrm);
			}
		}
	}

	if (mesh.vertices.empty()) return mesh;

	// Read index buffer(s)
	uint32_t ibCount = ReadU32(msData + 0x10); // indexBuffers array size
	uint32_t ibArrayOff = ResolveLocalPointer(meshSectionOffset + 0x0C);

	if (ibArrayOff != 0xFFFFFFFF && ibCount > 0) {
		for (uint32_t ib = 0; ib < ibCount; ib++) {
			uint32_t ibPtrOff = ibArrayOff + ib * 4;
			uint32_t ibOff = ResolvePointer(ibPtrOff);
			if (ibOff == 0xFFFFFFFF || ibOff >= m_DataSectionSize) continue;

			const uint8_t* ibData = m_DataSection + ibOff;

			// hkxIndexBuffer layout (from Ghidra RE of hkxMeshSection::collectTriangles):
			// +0x08: indexType (1=triangle list, 2=triangle strip)
			// +0x0C: indices16 hkArray<uint16> (ptr, size, cap)
			// +0x18: indices32 hkArray<uint32> (ptr, size, cap)
			// +0x24: vertexBaseOffset
			// +0x28: length

			uint32_t indexType = ReadU32(ibData + 0x08);
			uint32_t idx16Size = ReadU32(ibData + 0x10);
			uint32_t idx32Size = ReadU32(ibData + 0x1C);

			// Read raw indices into a flat vector (from whichever array has data)
			std::vector<uint32_t> indices;
			if (idx16Size > 0) {
				uint32_t idx16DataOff = ResolveLocalPointer(ibOff + 0x0C);
				if (idx16DataOff != 0xFFFFFFFF &&
					idx16DataOff + idx16Size * 2 <= m_DataSectionSize) {
					indices.reserve(idx16Size);
					for (uint32_t j = 0; j < idx16Size; j++) {
						indices.push_back(ReadU16(m_DataSection + idx16DataOff + j * 2));
					}
				}
			} else if (idx32Size > 0) {
				uint32_t idx32DataOff = ResolveLocalPointer(ibOff + 0x18);
				if (idx32DataOff != 0xFFFFFFFF &&
					idx32DataOff + idx32Size * 4 <= m_DataSectionSize) {
					indices.reserve(idx32Size);
					for (uint32_t j = 0; j < idx32Size; j++) {
						indices.push_back(ReadU32(m_DataSection + idx32DataOff + j * 4));
					}
				}
			}

			if (indices.size() < 3) continue;

			if (indexType == 1) {
				// Triangle list: every 3 indices = 1 triangle
				for (size_t j = 0; j + 2 < indices.size(); j += 3) {
					mesh.triangles.push_back({indices[j], indices[j+1], indices[j+2]});
				}
			} else if (indexType == 2) {
				// Triangle strip: first 3 = tri, then each next index adds a tri
				// Winding alternates: even tris = (i, i+1, i+2), odd = (i+1, i, i+2)
				for (size_t j = 0; j + 2 < indices.size(); j++) {
					uint32_t a = indices[j], b = indices[j+1], c = indices[j+2];
					if (a == b || b == c || a == c) continue; // degenerate
					if (j % 2 == 0) {
						mesh.triangles.push_back({a, b, c});
					} else {
						mesh.triangles.push_back({b, a, c});
					}
				}
			}
		}
	}

	return mesh;
}

// ---------------------------------------------------------------------------
// hkxScene / hkxNode binary extraction
// ---------------------------------------------------------------------------
//
// hkxNode layout (Havok 7.0, 32-bit pointers):
// Inherits hkxAttributeHolder which inherits hkReferencedObject.
//   +0x00: hkReferencedObject (vtable:4 + memSizeAndFlags:2 + refCount:2) = 8
//   +0x08: attributeGroups hkArray<hkxAttributeGroup> (ptr:4, size:4, cap:4) = 12
//   +0x14: name ptr (char*)
//   +0x18: object ptr (hkReferencedObject* — hkxMesh, hkxLight, hkxCamera, etc.)
//   +0x1C: keyFrames hkArray<hkMatrix4> (ptr:4, size:4, cap:4) = 12
//   +0x28: children hkArray<hkxNode*> (ptr:4, size:4, cap:4) = 12
//   +0x34: annotations hkArray
//   +0x40: userProperties ptr
//
// hkxScene layout (Havok 7.0, 32-bit):
// Inherits hkxAttributeHolder.
//   +0x00: hkReferencedObject = 8
//   +0x08: attributeGroups hkArray = 12
//   +0x14: modeller ptr (char*)  — confirmed by probe: string at +0x08 on scene
//          Actually scene fixups show +0x08→string, +0x14→rootNode
//          So scene has: base(8) + attributeGroups(12) = 0x14 for first field
//          But probe showed +0x08→string. Scene may NOT inherit hkxAttributeHolder.
//          Using confirmed probe offsets: +0x08=modeller, +0x14=rootNode

SceneNode HkxFile::ReadSceneNode(uint32_t nodeOffset, SceneInfo& scene) {
	SceneNode node;
	if (nodeOffset + 0x34 > m_DataSectionSize) return node;

	// Name at +0x14 (after hkReferencedObject + attributeGroups)
	node.name = ResolveString(nodeOffset + 0x14);

	// keyFrames array at +0x1C — first matrix is the node's local transform
	uint32_t kfSize = ReadU32(m_DataSection + nodeOffset + 0x20); // keyFrames array size
	if (kfSize > 0) {
		uint32_t kfDataOff = ResolveLocalPointer(nodeOffset + 0x1C);
		if (kfDataOff != 0xFFFFFFFF && kfDataOff + 64 <= m_DataSectionSize) {
			const uint8_t* m = m_DataSection + kfDataOff;
			node.transform.col0 = ReadVector4(m);
			node.transform.col1 = ReadVector4(m + 16);
			node.transform.col2 = ReadVector4(m + 32);
			node.transform.translation = ReadVector4(m + 48);
		}
	}

	// Object pointer at +0x18 — check if it's an hkxMesh
	uint32_t objOff = ResolvePointer(nodeOffset + 0x18);
	if (objOff != 0xFFFFFFFF) {
		// Check if this offset is a known hkxMesh
		auto meshIt = m_Result.objectsByClass.find("hkxMesh");
		if (meshIt != m_Result.objectsByClass.end()) {
			for (uint32_t meshOff : meshIt->second) {
				if (meshOff == objOff) {
					// Found the mesh — read its sections
					// hkxMesh: +0x08 = sections hkArray<hkxMeshSection*>
					uint32_t secCount = ReadU32(m_DataSection + meshOff + 0x0C);
					uint32_t secArrayOff = ResolveLocalPointer(meshOff + 0x08);
					if (secArrayOff != 0xFFFFFFFF && secCount > 0) {
						for (uint32_t s = 0; s < secCount && s < 100; s++) {
							uint32_t secOff = ResolvePointer(secArrayOff + s * 4);
							if (secOff != 0xFFFFFFFF) {
								SceneMesh mesh = ReadSceneMesh(secOff);
								if (!mesh.vertices.empty()) {
									node.meshIndex = static_cast<int>(scene.meshes.size());
									scene.meshes.push_back(std::move(mesh));
								}
							}
						}
					}
					break;
				}
			}
		}
	}

	// Children array at +0x28
	uint32_t childCount = ReadU32(m_DataSection + nodeOffset + 0x2C); // children array size
	uint32_t childArrayOff = ResolveLocalPointer(nodeOffset + 0x28);
	if (childArrayOff != 0xFFFFFFFF && childCount > 0 && childCount < 10000) {
		for (uint32_t c = 0; c < childCount; c++) {
			uint32_t childNodeOff = ResolvePointer(childArrayOff + c * 4);
			if (childNodeOff != 0xFFFFFFFF && childNodeOff + 0x28 <= m_DataSectionSize) {
				int childIdx = static_cast<int>(scene.nodes.size());
				scene.nodes.push_back({}); // placeholder
				scene.nodes[childIdx] = ReadSceneNode(childNodeOff, scene);
				node.childIndices.push_back(childIdx);
			}
		}
	}

	return node;
}

SceneInfo HkxFile::ReadScene(uint32_t sceneOffset) {
	SceneInfo scene;
	if (sceneOffset + 0x20 > m_DataSectionSize) return scene;

	scene.modeller = ResolveString(sceneOffset + 0x08);

	// rootNode pointer — at +0x14 (global fixup to hkxNode)
	// Layout: +0x08=modeller, +0x0C=sceneTransform(ptr or inline?), +0x14=rootNode
	uint32_t rootNodeOff = ResolvePointer(sceneOffset + 0x14);
	if (rootNodeOff != 0xFFFFFFFF && rootNodeOff + 0x28 <= m_DataSectionSize) {
		scene.rootNodeIndex = static_cast<int>(scene.nodes.size());
		scene.nodes.push_back({}); // placeholder
		scene.nodes[scene.rootNodeIndex] = ReadSceneNode(rootNodeOff, scene);
	}

	return scene;
}

// --- hkpCompressedMeshShape extraction helpers ---

std::vector<uint16_t> HkxFile::ReadInlineArrayU16(uint32_t dataOffset) const {
	std::vector<uint16_t> result;
	if (dataOffset + 12 > m_DataSectionSize) return result;

	const uint8_t* arrData = m_DataSection + dataOffset;
	uint32_t count = ReadU32(arrData + 4);
	if (count == 0 || count > 1000000) return result;

	uint32_t dataDst = ResolveLocalPointer(dataOffset);
	if (dataDst == 0xFFFFFFFF) return result;
	if (dataDst + count * 2 > m_DataSectionSize) return result;

	result.reserve(count);
	for (uint32_t i = 0; i < count; i++) {
		result.push_back(ReadU16(m_DataSection + dataDst + i * 2));
	}
	return result;
}

std::vector<uint8_t> HkxFile::ReadInlineArrayU8(uint32_t dataOffset) const {
	std::vector<uint8_t> result;
	if (dataOffset + 12 > m_DataSectionSize) return result;

	const uint8_t* arrData = m_DataSection + dataOffset;
	uint32_t count = ReadU32(arrData + 4);
	if (count == 0 || count > 1000000) return result;

	uint32_t dataDst = ResolveLocalPointer(dataOffset);
	if (dataDst == 0xFFFFFFFF) return result;
	if (dataDst + count > m_DataSectionSize) return result;

	result.reserve(count);
	for (uint32_t i = 0; i < count; i++) {
		result.push_back(m_DataSection[dataDst + i]);
	}
	return result;
}

void HkxFile::ReadCompressedMeshShape(ShapeInfo& info, uint32_t offset,
                                       const uint8_t* objData, size_t remaining) {
	// hkpCompressedMeshShape (Havok 5.5) — total 0xE0 bytes
	// See HkxOffsets.h for the full field layout derived from Ghidra RE.
	//
	// The shape stores collision geometry in three forms:
	//   1. Chunks (quantized triangles in spatial regions)
	//   2. BigTriangles (full-precision triangles using bigVertices)
	//   3. ConvexPieces (quantized convex hulls)
	//
	// Vertex dequantization: world_pos = chunk.offset + float3(quantized_u16) * error

	if (remaining < Off::CMS_NamedMaterials + 12) return;

	auto cms = std::make_shared<ShapeInfo::CompressedMeshData>();

	// Read shape parameters
	cms->bitsPerIndex  = static_cast<int32_t>(ReadU32(objData + Off::CMS_BitsPerIndex));
	cms->bitsPerWIndex = static_cast<int32_t>(ReadU32(objData + Off::CMS_BitsPerWIndex));
	cms->wIndexMask    = static_cast<int32_t>(ReadU32(objData + Off::CMS_WIndexMask));
	cms->indexMask     = static_cast<int32_t>(ReadU32(objData + Off::CMS_IndexMask));
	info.radius        = ReadFloat(objData + Off::CMS_Radius);
	cms->error         = ReadFloat(objData + Off::CMS_Error);

	info.disableWelding = ReadU8(objData + Off::ShapeCollection_DisableWelding) != 0;
	info.collectionType = ReadU8(objData + Off::ShapeCollection_CollectionType);
	info.weldingType    = ReadU8(objData + Off::CMS_WeldingType);

	// --- Read transforms array (hkQsTransform, 64 bytes each) ---
	{
		uint32_t count = ReadU32(objData + Off::CMS_Transforms + 4);
		uint32_t dataDst = ResolveLocalPointer(offset + Off::CMS_Transforms);
		if (dataDst != 0xFFFFFFFF && count > 0 && count < 10000 &&
			dataDst + count * 0x40 <= m_DataSectionSize) {
			cms->transforms.reserve(count);
			for (uint32_t i = 0; i < count; i++) {
				// hkQsTransform: rotation (quaternion, 16 bytes), translation (16 bytes),
				// scale (16 bytes), padding (16 bytes) — 64 bytes total.
				// But serialized as 3 hkVector4s + 1 hkVector4 = translation, rotation, scale, ?
				// Actually in Havok 5.5 the hkQsTransform is stored as a 4x4 matrix (hkTransform).
				// We read it as a standard hkTransform (3 rotation columns + translation).
				const uint8_t* tData = m_DataSection + dataDst + i * 0x40;
				cms->transforms.push_back(ReadTransform(tData));
			}
		}
	}

	// --- Read bigVertices array (hkVector4, 16 bytes each) ---
	{
		uint32_t count = ReadU32(objData + Off::CMS_BigVertices + 4);
		uint32_t dataDst = ResolveLocalPointer(offset + Off::CMS_BigVertices);
		if (dataDst != 0xFFFFFFFF && count > 0 && count < 1000000 &&
			dataDst + count * 16 <= m_DataSectionSize) {
			cms->bigVertices.reserve(count);
			for (uint32_t i = 0; i < count; i++) {
				cms->bigVertices.push_back(
					ReadVector4(m_DataSection + dataDst + i * 16));
			}
		}
	}

	// --- Read bigTriangles array (BigTriangle, 16 bytes each) ---
	{
		uint32_t count = ReadU32(objData + Off::CMS_BigTriangles + 4);
		uint32_t dataDst = ResolveLocalPointer(offset + Off::CMS_BigTriangles);
		if (dataDst != 0xFFFFFFFF && count > 0 && count < 1000000 &&
			dataDst + count * Off::CMSBigTri_Size <= m_DataSectionSize) {
			cms->bigTriangles.reserve(count);
			for (uint32_t i = 0; i < count; i++) {
				const uint8_t* btData = m_DataSection + dataDst + i * Off::CMSBigTri_Size;
				ShapeInfo::CompressedMeshBigTriangle bt;
				bt.a = ReadU16(btData + Off::CMSBigTri_A);
				bt.b = ReadU16(btData + Off::CMSBigTri_B);
				bt.c = ReadU16(btData + Off::CMSBigTri_C);
				bt.material = ReadU32(btData + Off::CMSBigTri_Material);
				bt.weldingInfo = ReadU16(btData + Off::CMSBigTri_WeldingInfo);
				cms->bigTriangles.push_back(bt);
			}
		}
	}

	// --- Read chunks array (Chunk, 80 bytes each) ---
	{
		uint32_t count = ReadU32(objData + Off::CMS_Chunks + 4);
		uint32_t dataDst = ResolveLocalPointer(offset + Off::CMS_Chunks);
		if (dataDst != 0xFFFFFFFF && count > 0 && count < 100000 &&
			dataDst + count * Off::CMSChunk_Size <= m_DataSectionSize) {
			cms->chunks.reserve(count);
			for (uint32_t i = 0; i < count; i++) {
				uint32_t chunkBase = dataDst + i * Off::CMSChunk_Size;
				const uint8_t* cData = m_DataSection + chunkBase;

				ShapeInfo::CompressedMeshChunk chunk;
				chunk.offset = ReadVector4(cData + Off::CMSChunk_Offset);

				// Read inline hkArrays — each is at a data-section-absolute offset
				chunk.vertices     = ReadInlineArrayU16(chunkBase + Off::CMSChunk_Vertices);
				chunk.indices      = ReadInlineArrayU16(chunkBase + Off::CMSChunk_Indices);
				chunk.stripLengths = ReadInlineArrayU16(chunkBase + Off::CMSChunk_StripLengths);
				chunk.weldingInfo  = ReadInlineArrayU16(chunkBase + Off::CMSChunk_WeldingInfo);

				chunk.materialInfo   = ReadU32(cData + Off::CMSChunk_MaterialInfo);
				chunk.reference      = ReadU16(cData + Off::CMSChunk_Reference);
				chunk.transformIndex = ReadU16(cData + Off::CMSChunk_TransformIndex);

				cms->chunks.push_back(std::move(chunk));
			}
		}
	}

	// --- Read convexPieces array (ConvexPiece, 64 bytes each) ---
	{
		uint32_t count = ReadU32(objData + Off::CMS_ConvexPieces + 4);
		uint32_t dataDst = ResolveLocalPointer(offset + Off::CMS_ConvexPieces);
		if (dataDst != 0xFFFFFFFF && count > 0 && count < 100000 &&
			dataDst + count * Off::CMSConvex_Size <= m_DataSectionSize) {
			cms->convexPieces.reserve(count);
			for (uint32_t i = 0; i < count; i++) {
				uint32_t pieceBase = dataDst + i * Off::CMSConvex_Size;
				const uint8_t* cpData = m_DataSection + pieceBase;

				ShapeInfo::CompressedMeshConvexPiece piece;
				piece.offset = ReadVector4(cpData + Off::CMSConvex_Offset);

				piece.vertices     = ReadInlineArrayU16(pieceBase + Off::CMSConvex_Vertices);
				piece.faceVertices = ReadInlineArrayU8(pieceBase + Off::CMSConvex_FaceVertices);
				piece.faceOffsets  = ReadInlineArrayU16(pieceBase + Off::CMSConvex_FaceOffsets);

				piece.reference      = ReadU16(cpData + Off::CMSConvex_Reference);
				piece.transformIndex = ReadU16(cpData + Off::CMSConvex_TransformIndex);

				cms->convexPieces.push_back(std::move(piece));
			}
		}
	}

	info.compressedMesh = cms;

	// Also extract world-space triangles into the flat triangles/planeEquations arrays
	// so existing code that reads SimpleContainer-style data can render this shape too.
	//
	// Phase 1: BigTriangles — already world-space, just reference bigVertices directly.
	uint32_t vertexBase = 0;
	for (const auto& bv : cms->bigVertices) {
		info.planeEquations.push_back(bv);
	}
	for (const auto& bt : cms->bigTriangles) {
		if (bt.a < cms->bigVertices.size() &&
			bt.b < cms->bigVertices.size() &&
			bt.c < cms->bigVertices.size()) {
			info.triangles.push_back({bt.a, bt.b, bt.c});
		}
	}
	vertexBase = static_cast<uint32_t>(info.planeEquations.size());

	// Phase 2: Chunks — dequantize vertices then extract triangle indices.
	for (const auto& chunk : cms->chunks) {
		uint32_t chunkVertBase = static_cast<uint32_t>(info.planeEquations.size());

		// Dequantize vertices: world = chunk.offset + float(quantized) * error
		// Vertices are stored as interleaved u16 triples: x0,y0,z0,x1,y1,z1,...
		uint32_t numVerts = static_cast<uint32_t>(chunk.vertices.size()) / 3;
		for (uint32_t v = 0; v < numVerts; v++) {
			float qx = static_cast<float>(chunk.vertices[v * 3 + 0]);
			float qy = static_cast<float>(chunk.vertices[v * 3 + 1]);
			float qz = static_cast<float>(chunk.vertices[v * 3 + 2]);
			Vector4 worldPos;
			worldPos.x = chunk.offset.x + qx * cms->error;
			worldPos.y = chunk.offset.y + qy * cms->error;
			worldPos.z = chunk.offset.z + qz * cms->error;
			worldPos.w = 0.0f;

			// Apply chunk transform if present
			if (chunk.transformIndex != 0xFFFF &&
				chunk.transformIndex < cms->transforms.size()) {
				const auto& tf = cms->transforms[chunk.transformIndex];
				// Apply rotation (3x3 matrix stored as column vectors) + translation
				float rx = tf.col0.x * worldPos.x + tf.col1.x * worldPos.y + tf.col2.x * worldPos.z + tf.translation.x;
				float ry = tf.col0.y * worldPos.x + tf.col1.y * worldPos.y + tf.col2.y * worldPos.z + tf.translation.y;
				float rz = tf.col0.z * worldPos.x + tf.col1.z * worldPos.y + tf.col2.z * worldPos.z + tf.translation.z;
				worldPos.x = rx;
				worldPos.y = ry;
				worldPos.z = rz;
			}

			info.planeEquations.push_back(worldPos);
		}

		// Extract triangles from indices.
		// The indices array contains triangle vertex indices. According to getChildShape,
		// indices are accessed in groups — each index points into the vertices array
		// as a vertex index (not a component index), and 3 consecutive indices form
		// a triangle. The index values in the indices array are multiplied by 3 in
		// getChildShape to get the component offset: idx*3 -> vertices[idx*3+0..2].
		//
		// If stripLengths is empty, indices are a flat triangle list (groups of 3).
		// If stripLengths is non-empty, indices form triangle strips.
		if (chunk.stripLengths.empty()) {
			// Flat triangle list: every 3 indices = 1 triangle
			for (size_t j = 0; j + 2 < chunk.indices.size(); j += 3) {
				uint32_t ia = chunk.indices[j];
				uint32_t ib = chunk.indices[j + 1];
				uint32_t ic = chunk.indices[j + 2];
				if (ia < numVerts && ib < numVerts && ic < numVerts) {
					info.triangles.push_back({
						chunkVertBase + ia,
						chunkVertBase + ib,
						chunkVertBase + ic
					});
				}
			}
		} else {
			// Triangle strips: each strip has a length from stripLengths
			size_t idxPos = 0;
			for (uint16_t stripLen : chunk.stripLengths) {
				if (idxPos + stripLen > chunk.indices.size()) break;
				for (uint16_t s = 0; s + 2 < stripLen; s++) {
					uint32_t ia = chunk.indices[idxPos + s];
					uint32_t ib = chunk.indices[idxPos + s + 1];
					uint32_t ic = chunk.indices[idxPos + s + 2];
					if (ia == ib || ib == ic || ia == ic) continue; // degenerate
					if (ia < numVerts && ib < numVerts && ic < numVerts) {
						if (s % 2 == 0) {
							info.triangles.push_back({
								chunkVertBase + ia,
								chunkVertBase + ib,
								chunkVertBase + ic
							});
						} else {
							info.triangles.push_back({
								chunkVertBase + ib,
								chunkVertBase + ia,
								chunkVertBase + ic
							});
						}
					}
				}
				idxPos += stripLen;
			}
		}
	}

	info.numVertices = static_cast<int32_t>(info.planeEquations.size());
	info.numTriangles = static_cast<int32_t>(info.triangles.size());

	// If we extracted geometry, mark as SimpleContainer for downstream rendering
	if (!info.planeEquations.empty()) {
		info.type = ShapeType::SimpleContainer;
	}
}

} // namespace Hkx
