// Havok 5.5 / 7.x 32-bit binary packfile field offsets.
//
// These offsets describe the in-packfile binary layout used by the binary
// packfile extraction code to read physics shapes, rigid bodies, physics
// systems, and physics data from the raw data section bytes.
//
// LEGO Universe ships Havok 5.5 (packfile version 5). Binary packfiles
// store objects at their full runtime size with nosave fields zeroed.
//
// Shape base hierarchy:
//   hkBaseObject:          vtable (4 bytes)
//   hkReferencedObject:    +4 memSizeAndFlags(u16) + refCount(i16) = total 8
//   hkpShape:              +8 userData(u32), +12 type(u32)         = total 16
//   hkpSphereRepShape:     = hkpShape (no additions)               = total 16
//   hkpConvexShape:        +16 radius(float)                       = total 20, padded to 32
#pragma once

#include <cstdint>

namespace Hkx {

// Sentinel values used throughout binary packfiles
constexpr uint32_t NULL_OFFSET = 0xFFFFFFFF; // Invalid pointer / null reference
constexpr uint16_t NULL_INDEX  = 0xFFFF;     // Invalid index (chunk/piece transformIndex, etc.)

// Size of hkpConvexVerticesShape::FourTransposedPoints (3 x Vector4 = 48 bytes)
constexpr size_t FOUR_TRANSPOSED_POINTS_SIZE = 48;

namespace Off {

	// hkpShape
	constexpr uint32_t Shape_UserData = 8;
	constexpr uint32_t Shape_Type = 12;

	// hkpConvexShape
	constexpr uint32_t ConvexShape_Radius = 16;

	// hkpBoxShape : hkpConvexShape (padded to 32)
	constexpr uint32_t BoxShape_HalfExtents = 32; // hkVector4

	// hkpCapsuleShape : hkpConvexShape (padded to 32)
	constexpr uint32_t CapsuleShape_VertexA = 32; // hkVector4
	constexpr uint32_t CapsuleShape_VertexB = 48; // hkVector4

	// hkpCylinderShape : hkpConvexShape
	// m_cylRadius at +20, m_cylBaseRadiusFactorForHeightFieldCollisions at +24
	// pad to 32, then vertices
	constexpr uint32_t CylinderShape_CylRadius = 20;
	constexpr uint32_t CylinderShape_CylBaseFactor = 24;
	constexpr uint32_t CylinderShape_VertexA = 32;
	constexpr uint32_t CylinderShape_VertexB = 48;
	constexpr uint32_t CylinderShape_Perp1 = 64;
	constexpr uint32_t CylinderShape_Perp2 = 80;

	// hkpConvexVerticesShape : hkpConvexShape (padded to 32)
	constexpr uint32_t CVS_AabbHalfExtents = 32;   // +0x20
	constexpr uint32_t CVS_AabbCenter = 48;         // +0x30
	constexpr uint32_t CVS_RotatedVertices = 64;    // +0x40 hkArray (ptr+size+cap = 12)
	constexpr uint32_t CVS_NumVertices = 76;        // +0x4C

	// PlaneEquations offset differs between Havok versions:
	//   Havok 5.x-6.x: numVertices(4) immediately followed by planeEquations = +0x50
	//   Havok 7.x:      numVertices(4) + 4 bytes padding before planeEquations = +0x54
	constexpr uint32_t CVS_PlaneEquations_v5 = 80;  // +0x50 (Havok 5.x, 6.x)
	constexpr uint32_t CVS_PlaneEquations_v7 = 84;  // +0x54 (Havok 7.x)
	constexpr uint32_t CVS_Connectivity_v5 = 92;    // +0x5C (Havok 5.x, 6.x)
	constexpr uint32_t CVS_Connectivity_v7 = 96;    // +0x60 (Havok 7.x)

	// Helper: select offset based on file version
	inline uint32_t CVS_PlaneEquations(uint32_t fileVersion) {
		return fileVersion >= 7 ? CVS_PlaneEquations_v7 : CVS_PlaneEquations_v5;
	}
	inline uint32_t CVS_Connectivity(uint32_t fileVersion) {
		return fileVersion >= 7 ? CVS_Connectivity_v7 : CVS_Connectivity_v5;
	}

	// hkpConvexTransformShapeBase : hkpConvexShape
	// hkpConvexShape base = 20 bytes, padded. Child pointer at +24.
	constexpr uint32_t CTSB_ChildShape = 24; // ptr (4 bytes), resolved via global fixup

	// hkpConvexTransformShape : hkpConvexTransformShapeBase
	// After base (padded to 32), transform at 32
	constexpr uint32_t CTS_Transform = 32; // hkTransform (64 bytes)

	// hkpConvexTranslateShape : hkpConvexTransformShapeBase
	constexpr uint32_t CTrS_Translation = 32; // hkVector4

	// hkpBvTreeShape : hkpShape
	constexpr uint32_t BvTree_BvTreeType = 16; // u8

	// hkpMoppBvTreeShape (serialized, nosave fields omitted):
	// After hkpShape(16): bvTreeType(1+pad=4), code ptr(4), m_codeInfoCopy(hkVector4=16),
	// then hkpSingleShapeContainer(vtable(4)+childPtr(4))
	constexpr uint32_t Mopp_Code = 20;      // ptr to hkpMoppCode
	constexpr uint32_t Mopp_Child = 52;     // ptr in hkpSingleShapeContainer (+20+4+16+4+4+4=52)

	// hkpShapeCollection : hkpShape (+16 disableWelding(u8) + collectionType(u8) + pad = 20 bytes)
	constexpr uint32_t ShapeCollection_DisableWelding = 16;
	constexpr uint32_t ShapeCollection_CollectionType = 17;

	// hkpListShape : hkpShapeCollection (base = 20 bytes)
	constexpr uint32_t List_ChildInfo = 24;     // hkArray<ChildInfo> (12 bytes) - after ShapeCollection base(20) + pad
	constexpr uint32_t List_Flags = 36;
	constexpr uint32_t List_NumDisabledChildren = 38;
	// pad to 48 for hkVector4 alignment
	constexpr uint32_t List_AabbHalfExtents = 48;
	constexpr uint32_t List_AabbCenter = 64;

	// hkpTransformShape : hkpShape
	// Ghidra runtime: child at +16, transform at +32. But serialized format may differ
	// due to hkpShapeContainer vtable insertion. Existing offsets work for LU packfiles.
	constexpr uint32_t TransformShape_Child = 20;    // ptr (resolved via global fixup)
	constexpr uint32_t TransformShape_Transform = 48; // hkTransform (64 bytes)

	// hkpSimpleMeshShape : hkpShapeCollection (base = 20 bytes, padded to 24)
	constexpr uint32_t SimpleMesh_Vertices = 24;       // hkArray<hkVector4> (12)
	constexpr uint32_t SimpleMesh_Triangles = 36;      // hkArray<Triangle> (12)
	constexpr uint32_t SimpleMesh_MaterialIndices = 48; // hkArray<u8> (12)
	constexpr uint32_t SimpleMesh_Radius = 60;
	constexpr uint32_t SimpleMesh_WeldingType = 64;

	// hkpPhysicsData : hkReferencedObject
	constexpr uint32_t PhysData_WorldCinfo = 8;    // ptr
	constexpr uint32_t PhysData_Systems = 12;      // hkArray (12)

	// hkpPhysicsSystem : hkReferencedObject
	constexpr uint32_t PhysSys_RigidBodies = 8;    // hkArray (12)
	constexpr uint32_t PhysSys_Constraints = 20;   // hkArray (12)
	constexpr uint32_t PhysSys_Actions = 32;       // hkArray (12)
	constexpr uint32_t PhysSys_Phantoms = 44;      // hkArray (12)
	constexpr uint32_t PhysSys_Name = 56;          // ptr
	constexpr uint32_t PhysSys_UserData = 60;
	constexpr uint32_t PhysSys_Active = 64;        // bool

	// hkpCompressedMeshShape : hkpShapeCollection (Havok 5.5)
	//
	// Full serialized layout (0xE0 = 224 bytes) from Ghidra RE of Havok reflection data
	// in legouniverse.exe. The class inherits hkpShapeCollection which ends at +0x18
	// (vtable(4) + refobj(4) + userData(4) + shapeType(4) + collectionVtable(4) +
	// disableWelding(1) + collectionType(1) + pad(2)).
	//
	// Two additional hkArrays (materials16 at +0x3C, materials8 at +0x48) were
	// previously missed; they shift transforms/bigVertices/bigTriangles/chunks/
	// convexPieces by +0x18 from the offsets in the original constructor analysis.
	constexpr uint32_t CMS_BitsPerIndex   = 0x18; // int32
	constexpr uint32_t CMS_BitsPerWIndex  = 0x1C; // int32
	constexpr uint32_t CMS_WIndexMask     = 0x20; // int32
	constexpr uint32_t CMS_IndexMask      = 0x24; // int32
	constexpr uint32_t CMS_Radius         = 0x28; // float
	constexpr uint32_t CMS_WeldingType    = 0x2C; // u8 (enum)
	constexpr uint32_t CMS_MaterialType   = 0x2D; // u8 (enum)
	constexpr uint32_t CMS_Materials      = 0x30; // hkArray<Material> (12 bytes)
	constexpr uint32_t CMS_Materials16    = 0x3C; // hkArray<u16> (12 bytes)
	constexpr uint32_t CMS_Materials8     = 0x48; // hkArray<u8> (12 bytes)
	constexpr uint32_t CMS_Transforms     = 0x54; // hkArray<hkQsTransform> (12 bytes, element=0x40)
	constexpr uint32_t CMS_BigVertices    = 0x60; // hkArray<hkVector4> (12 bytes, element=0x10)
	constexpr uint32_t CMS_BigTriangles   = 0x6C; // hkArray<BigTriangle> (12 bytes, element=0x10)
	constexpr uint32_t CMS_Chunks         = 0x78; // hkArray<Chunk> (12 bytes, element=0x50)
	constexpr uint32_t CMS_ConvexPieces   = 0x84; // hkArray<ConvexPiece> (12 bytes, element=0x40)
	constexpr uint32_t CMS_Error          = 0x90; // float — quantization scale
	constexpr uint32_t CMS_Bounds         = 0xA0; // hkAabb (32 bytes: min hkVector4 + max hkVector4)
	constexpr uint32_t CMS_NumMaterials   = 0xC4; // u16
	constexpr uint32_t CMS_MaterialStride = 0xC6; // u16
	constexpr uint32_t CMS_NamedMaterials = 0xC8; // hkArray<NamedMaterial> (12 bytes)

	// hkpCompressedMeshShape::Chunk (0x50 = 80 bytes)
	constexpr uint32_t CMSChunk_Offset         = 0x00; // hkVector4 (16 bytes) — dequantization origin
	constexpr uint32_t CMSChunk_Vertices       = 0x10; // hkArray<u16> (12 bytes) — quantized x,y,z triples
	constexpr uint32_t CMSChunk_Indices        = 0x1C; // hkArray<u16> (12 bytes) — triangle vertex indices
	constexpr uint32_t CMSChunk_StripLengths   = 0x28; // hkArray<u16> (12 bytes)
	constexpr uint32_t CMSChunk_WeldingInfo    = 0x34; // hkArray<u16> (12 bytes)
	constexpr uint32_t CMSChunk_MaterialInfo   = 0x40; // u32
	constexpr uint32_t CMSChunk_Reference      = 0x44; // u16 (0xFFFF = none)
	constexpr uint32_t CMSChunk_TransformIndex = 0x46; // u16 (0xFFFF = none)
	constexpr uint32_t CMSChunk_Size           = 0x50; // total size

	// hkpCompressedMeshShape::BigTriangle (0x10 = 16 bytes)
	constexpr uint32_t CMSBigTri_A           = 0x00; // u16 — index into bigVertices
	constexpr uint32_t CMSBigTri_B           = 0x02; // u16
	constexpr uint32_t CMSBigTri_C           = 0x04; // u16
	constexpr uint32_t CMSBigTri_Material    = 0x08; // u32
	constexpr uint32_t CMSBigTri_WeldingInfo = 0x0C; // u16
	constexpr uint32_t CMSBigTri_Size        = 0x10; // total size

	// hkpCompressedMeshShape::ConvexPiece (0x40 = 64 bytes)
	constexpr uint32_t CMSConvex_Offset         = 0x00; // hkVector4 (16 bytes) — dequantization origin
	constexpr uint32_t CMSConvex_Vertices       = 0x10; // hkArray<u16> (12 bytes)
	constexpr uint32_t CMSConvex_FaceVertices   = 0x1C; // hkArray<u8> (12 bytes)
	constexpr uint32_t CMSConvex_FaceOffsets    = 0x28; // hkArray<u16> (12 bytes)
	constexpr uint32_t CMSConvex_Reference      = 0x34; // u16
	constexpr uint32_t CMSConvex_TransformIndex = 0x36; // u16
	constexpr uint32_t CMSConvex_Size           = 0x40; // total size

	// =========================================================================
	// hkpRigidBody / hkpEntity / hkpWorldObject (runtime layout)
	//
	// LEGO Universe ships Havok 5.5 (packfile version 5, 32-bit).
	// Binary packfiles store objects at their full runtime size with
	// nosave fields zeroed, so these runtime offsets apply directly.
	//
	// Class hierarchy (all offsets from object start):
	//   hkBaseObject          +0x00  vtable
	//   hkReferencedObject    +0x04  memSizeAndFlags(u16), refCount(i16)
	//   hkpWorldObject        +0x08  world(ptr), userData(u32), collidable(struct), ...
	//   hkpEntity             +0x8C  material(struct), motion(struct), ...
	//   hkpRigidBody          (no additional serialized fields; total size 0x220)
	//
	// Offsets were extracted from Havok reflection metadata (hkClass/
	// hkClassMember tables) in legouniverse.exe via Ghidra.
	// =========================================================================

	// --- hkpWorldObject base ---
	constexpr uint32_t RB_UserData         = 0x0C;   // hkpWorldObject::m_userData (hkUlong)

	// --- hkpCollidable (embedded at +0x10 within hkpWorldObject) ---
	// hkpCdBody base:
	constexpr uint32_t RB_Shape            = 0x10;   // hkpCdBody::m_shape (ptr, resolved via fixup)
	constexpr uint32_t RB_ShapeKey         = 0x14;   // hkpCdBody::m_shapeKey (u32)

	// hkpCollidable own fields:
	constexpr uint32_t RB_ForceCollideOnPpu = 0x21;  // hkpCollidable::m_forceCollideOntoPpu (u8)

	// hkpTypedBroadPhaseHandle (at collidable+0x14 = +0x24):
	constexpr uint32_t RB_BPH_Type         = 0x28;   // hkpTypedBroadPhaseHandle::m_type (i8)
	constexpr uint32_t RB_QualityType      = 0x2A;   // hkpTypedBroadPhaseHandle::m_objectQualityType (i8)
	constexpr uint32_t RB_CollFilterInfo   = 0x2C;   // hkpTypedBroadPhaseHandle::m_collisionFilterInfo (u32)

	// hkpCollidable::m_allowedPenetrationDepth:
	constexpr uint32_t RB_AllowedPenDepth  = 0x5C;   // float

	// --- hkpWorldObject string/property fields ---
	constexpr uint32_t RB_Name             = 0x74;   // hkpWorldObject::m_name (hkStringPtr, resolved via fixup)
	constexpr uint32_t RB_Properties       = 0x78;   // hkpWorldObject::m_properties (hkArray<hkpProperty>, 12 bytes)

	// --- hkpEntity fields (starting at +0x8C) ---

	// hkpMaterial (embedded struct, size 0x0C):
	//   +0x00 responseType (enum/i8), +0x04 friction (float), +0x08 restitution (float)
	constexpr uint32_t RB_Material         = 0x8C;   // start of hkpMaterial struct
	constexpr uint32_t RB_ResponseType     = 0x8C;   // hkpMaterial::m_responseType (enum i8)
	constexpr uint32_t RB_Friction         = 0x90;   // hkpMaterial::m_friction (float)
	constexpr uint32_t RB_Restitution      = 0x94;   // hkpMaterial::m_restitution (float)

	constexpr uint32_t RB_DamageMultiplier = 0x9C;   // hkpEntity::m_damageMultiplier (float)
	constexpr uint32_t RB_StorageIndex     = 0xA8;   // hkpEntity::m_storageIndex (u16)
	constexpr uint32_t RB_ContactCBDelay   = 0xAA;   // hkpEntity::m_contactPointCallbackDelay (u16)
	constexpr uint32_t RB_AutoRemoveLevel  = 0xD0;   // hkpEntity::m_autoRemoveLevel (i8)
	constexpr uint32_t RB_NumShapeKeysCPP  = 0xD1;   // hkpEntity::m_numShapeKeysInContactPointProperties (u8)
	constexpr uint32_t RB_RespModFlags     = 0xD2;   // hkpEntity::m_responseModifierFlags (u8)
	constexpr uint32_t RB_UID              = 0xD4;   // hkpEntity::m_uid (u32)
	constexpr uint32_t RB_SpuCollCB        = 0xD8;   // hkpEntity::m_spuCollisionCallback (struct, 8 bytes)

	// --- hkpMotion (embedded at +0xE0, size 0x120) ---
	constexpr uint32_t RB_Motion           = 0xE0;   // start of hkpMotion sub-object
	constexpr uint32_t RB_MotionType       = 0xE8;   // hkpMotion::m_type (enum u8)
	constexpr uint32_t RB_DeactIntCounter  = 0xE9;   // hkpMotion::m_deactivationIntegrateCounter (u8)
	constexpr uint32_t RB_DeactNumFrames   = 0xEA;   // hkpMotion::m_deactivationNumInactiveFrames[2] (u16[2])

	// hkMotionState (embedded at motion+0x10 = +0xF0):
	constexpr uint32_t RB_Transform        = 0xF0;   // hkMotionState::m_transform (hkTransform, 64 bytes)
	constexpr uint32_t RB_TransformCol0    = 0xF0;   // hkTransform column 0 (hkVector4)
	constexpr uint32_t RB_TransformCol1    = 0x100;  // hkTransform column 1 (hkVector4)
	constexpr uint32_t RB_TransformCol2    = 0x110;  // hkTransform column 2 (hkVector4)
	constexpr uint32_t RB_Translation      = 0x120;  // hkTransform translation (position, hkVector4)

	// hkSweptTransform (at motionState+0x40 = motion+0x50 = +0x130):
	constexpr uint32_t RB_CenterOfMass0    = 0x130;  // hkSweptTransform::m_centerOfMass0 (hkVector4)
	constexpr uint32_t RB_CenterOfMass1    = 0x140;  // hkSweptTransform::m_centerOfMass1 (hkVector4)
	constexpr uint32_t RB_Rotation0        = 0x150;  // hkSweptTransform::m_rotation0 (hkQuaternion)
	constexpr uint32_t RB_Rotation1        = 0x160;  // hkSweptTransform::m_rotation1 (hkQuaternion)
	constexpr uint32_t RB_COMLocal         = 0x170;  // hkSweptTransform::m_centerOfMassLocal (hkVector4)

	// Remaining hkMotionState fields:
	constexpr uint32_t RB_DeltaAngle       = 0x180;  // hkMotionState::m_deltaAngle (hkVector4)
	constexpr uint32_t RB_ObjectRadius     = 0x190;  // hkMotionState::m_objectRadius (float)
	constexpr uint32_t RB_LinearDamping    = 0x194;  // hkMotionState::m_linearDamping (float)
	constexpr uint32_t RB_AngularDamping   = 0x198;  // hkMotionState::m_angularDamping (float)
	constexpr uint32_t RB_MaxLinVel        = 0x19C;  // hkMotionState::m_maxLinearVelocity (u8)
	constexpr uint32_t RB_MaxAngVel        = 0x19D;  // hkMotionState::m_maxAngularVelocity (u8)
	constexpr uint32_t RB_DeactClass       = 0x19E;  // hkMotionState::m_deactivationClass (u8)

	// hkpMotion fields after motionState:
	constexpr uint32_t RB_InertiaInvMass   = 0x1A0;  // hkpMotion::m_inertiaAndMassInv (hkVector4; .w = 1/mass)
	constexpr uint32_t RB_LinearVelocity   = 0x1B0;  // hkpMotion::m_linearVelocity (hkVector4)
	constexpr uint32_t RB_AngularVelocity  = 0x1C0;  // hkpMotion::m_angularVelocity (hkVector4)
	constexpr uint32_t RB_DeactRefPos      = 0x1D0;  // hkpMotion::m_deactivationRefPosition[2] (hkVector4[2])
	constexpr uint32_t RB_DeactRefOrient   = 0x1F0;  // hkpMotion::m_deactivationRefOrientation[2] (u32[2])
	constexpr uint32_t RB_SavedMotion      = 0x1F8;  // hkpMotion::m_savedMotion (ptr)
	constexpr uint32_t RB_SavedQualityIdx  = 0x1FC;  // hkpMotion::m_savedQualityTypeIndex (u16)
	constexpr uint32_t RB_GravityFactor    = 0x1FE;  // hkpMotion::m_gravityFactor (hkHalf)

	// --- hkpEntity trailing fields ---
	constexpr uint32_t RB_LocalFrame       = 0x210;  // hkpEntity::m_localFrame (ptr)

	// Total object size
	constexpr uint32_t RB_TotalSize        = 0x220;  // hkpRigidBody/hkpEntity total size

} // namespace Off
} // namespace Hkx
