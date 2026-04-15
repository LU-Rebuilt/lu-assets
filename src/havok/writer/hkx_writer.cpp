#include "havok/writer/hkx_writer.h"

#include <fstream>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <filesystem>

namespace Hkx {

const std::unordered_map<std::string, uint32_t> HkxWriter::s_ClassSignatures = {
    {"hkClass",                    0x14425e51},
    {"hkClassMember",              0x4a986551},
    {"hkClassEnum",                0x8a3609cf},
    {"hkClassEnumItem",            0xce6f8a6c},
    {"hkRootLevelContainer",       0x2772c11e},
    {"hkpPhysicsData",             0xc2a461e4},
    {"hkpPhysicsSystem",           0xff724c17},
    {"hkpRigidBody",               0xaa8c3b19},
    {"hkpBoxShape",                0x3444d2d5},
    {"hkpSphereShape",             0x0795d9fa},
    {"hkpCapsuleShape",            0x06d47650},
    {"hkpCylinderShape",           0x3e463c3a},
    {"hkpConvexVerticesShape",     0x4e8ab2b7},
    {"hkpConvexTransformShape",    0xae3e5017},
    {"hkpConvexTranslateShape",    0x5ba0a5f7},
    {"hkpListShape",               0xa1937cbd},
    {"hkpTransformShape",          0x787ef513},
    {"hkpMoppBvTreeShape",         0x90b29d39},
    {"hkpMoppCode",                0x924c2661},
    {"hkpSimpleMeshShape",         0x16b3c811},
    {"hkpStorageExtendedMeshShape",0x5db9a717},
};

// --- Primitive write helpers ---

void HkxWriter::Reset() {
    m_DataSection.clear();
    m_ClassnamesSection.clear();
    m_RegisteredClasses.clear();
    m_LocalFixups.clear();
    m_GlobalFixups.clear();
    m_VirtualFixups.clear();
    m_WrittenShapes.clear();
    m_Error.clear();
}

void HkxWriter::WriteU8(uint8_t v) { m_DataSection.push_back(v); }
void HkxWriter::WriteU16(uint16_t v) { WriteBytes(&v, 2); }
void HkxWriter::WriteU32(uint32_t v) { WriteBytes(&v, 4); }
void HkxWriter::WriteFloat(float v) { WriteBytes(&v, 4); }

void HkxWriter::WriteVector4(const Vector4& v) {
    WriteFloat(v.x); WriteFloat(v.y); WriteFloat(v.z); WriteFloat(v.w);
}

void HkxWriter::WriteQuaternion(const Quaternion& q) {
    WriteFloat(q.x); WriteFloat(q.y); WriteFloat(q.z); WriteFloat(q.w);
}

void HkxWriter::WriteTransform(const Transform& t) {
    WriteVector4(t.col0);
    WriteVector4(t.col1);
    WriteVector4(t.col2);
    WriteVector4(t.translation);
}

void HkxWriter::WriteBytes(const void* data, size_t size) {
    auto* p = static_cast<const uint8_t*>(data);
    m_DataSection.insert(m_DataSection.end(), p, p + size);
}

// --- Random-access writers ---

void HkxWriter::WriteU8At(uint32_t pos, uint8_t v) {
    m_DataSection[pos] = v;
}

void HkxWriter::WriteU16At(uint32_t pos, uint16_t v) {
    std::memcpy(&m_DataSection[pos], &v, 2);
}

void HkxWriter::WriteU32At(uint32_t pos, uint32_t v) {
    std::memcpy(&m_DataSection[pos], &v, 4);
}

void HkxWriter::WriteI8At(uint32_t pos, int8_t v) {
    m_DataSection[pos] = static_cast<uint8_t>(v);
}

void HkxWriter::WriteFloatAt(uint32_t pos, float v) {
    std::memcpy(&m_DataSection[pos], &v, 4);
}

void HkxWriter::WriteVector4At(uint32_t pos, const Vector4& v) {
    WriteFloatAt(pos, v.x);
    WriteFloatAt(pos + 4, v.y);
    WriteFloatAt(pos + 8, v.z);
    WriteFloatAt(pos + 12, v.w);
}

void HkxWriter::WriteQuaternionAt(uint32_t pos, const Quaternion& q) {
    WriteFloatAt(pos, q.x);
    WriteFloatAt(pos + 4, q.y);
    WriteFloatAt(pos + 8, q.z);
    WriteFloatAt(pos + 12, q.w);
}

void HkxWriter::WriteTransformAt(uint32_t pos, const Transform& t) {
    WriteVector4At(pos, t.col0);
    WriteVector4At(pos + 16, t.col1);
    WriteVector4At(pos + 32, t.col2);
    WriteVector4At(pos + 48, t.translation);
}

void HkxWriter::Pad(size_t alignment) {
    while (m_DataSection.size() % alignment != 0) m_DataSection.push_back(0);
}

void HkxWriter::PadTo(size_t target) {
    while (m_DataSection.size() < target) m_DataSection.push_back(0);
}

uint32_t HkxWriter::Pos() const {
    return static_cast<uint32_t>(m_DataSection.size());
}

uint32_t HkxWriter::WriteArray(uint32_t count) {
    uint32_t ptrOffset = Pos();
    WriteU32(0);     // pointer (will be resolved via local fixup)
    WriteU32(count); // size
    WriteU32(count | 0x80000000); // capacityAndFlags (DONT_DEALLOCATE flag)
    return ptrOffset;
}

// --- Class registration ---

uint32_t HkxWriter::RegisterClass(const std::string& name) {
    auto it = m_RegisteredClasses.find(name);
    if (it != m_RegisteredClasses.end()) return it->second.nameOffset;

    uint32_t sig = 0;
    auto sigIt = s_ClassSignatures.find(name);
    if (sigIt != s_ClassSignatures.end()) sig = sigIt->second;

    // Write to classnames section: [u32 signature][u8 0x09][null-terminated name]
    auto& cs = m_ClassnamesSection;
    auto writeCsU32 = [&](uint32_t v) {
        cs.push_back(v & 0xFF); cs.push_back((v >> 8) & 0xFF);
        cs.push_back((v >> 16) & 0xFF); cs.push_back((v >> 24) & 0xFF);
    };
    writeCsU32(sig);
    cs.push_back(0x09);
    uint32_t nameOffset = static_cast<uint32_t>(cs.size());
    for (char c : name) cs.push_back(static_cast<uint8_t>(c));
    cs.push_back(0);

    m_RegisteredClasses[name] = {sig, nameOffset};
    return nameOffset;
}

// --- Fixup tracking ---

void HkxWriter::AddLocalFixup(uint32_t srcOffset, uint32_t dstOffset) {
    m_LocalFixups.push_back({srcOffset, dstOffset});
}

void HkxWriter::AddGlobalFixup(uint32_t srcOffset, uint32_t dstSectionIndex, uint32_t dstOffset) {
    m_GlobalFixups.push_back({srcOffset, dstSectionIndex, dstOffset});
}

void HkxWriter::AddVirtualFixup(uint32_t dataOffset, const std::string& className) {
    RegisterClass(className);
    m_VirtualFixups.push_back({dataOffset, className});
}

// --- Shape serializers ---

uint32_t HkxWriter::WriteShape(const ShapeInfo& shape) {
    // Check if already written (dedup by original data offset)
    if (shape.dataOffset != 0) {
        auto it = m_WrittenShapes.find(shape.dataOffset);
        if (it != m_WrittenShapes.end()) return it->second;
    }

    Pad(16);
    uint32_t offset = Pos();

    if (shape.dataOffset != 0) {
        m_WrittenShapes[shape.dataOffset] = offset;
    }

    switch (shape.type) {
    case ShapeType::Box: {
        AddVirtualFixup(offset, "hkpBoxShape");
        WriteU32(0);              // +0x00 vtable
        WriteU16(0); WriteU16(0); // +0x04
        WriteU32(shape.userData);  // +0x08
        WriteU32(shape.shapeTypeEnum); // +0x0C
        WriteFloat(shape.radius);  // +0x10
        PadTo(offset + 32);        // pad to +0x20
        WriteVector4(shape.halfExtents); // +0x20
        PadTo(offset + 48);
        break;
    }

    case ShapeType::Sphere: {
        AddVirtualFixup(offset, "hkpSphereShape");
        WriteU32(0);
        WriteU16(0); WriteU16(0);
        WriteU32(shape.userData);
        WriteU32(shape.shapeTypeEnum);
        WriteFloat(shape.radius);
        Pad(16);
        break;
    }

    case ShapeType::Capsule: {
        AddVirtualFixup(offset, "hkpCapsuleShape");
        WriteU32(0);
        WriteU16(0); WriteU16(0);
        WriteU32(shape.userData);
        WriteU32(shape.shapeTypeEnum);
        WriteFloat(shape.radius);
        PadTo(offset + 32);
        WriteVector4(shape.vertexA);
        WriteVector4(shape.vertexB);
        Pad(16);
        break;
    }

    case ShapeType::Cylinder: {
        AddVirtualFixup(offset, "hkpCylinderShape");
        WriteU32(0);               // +0x00
        WriteU16(0); WriteU16(0);  // +0x04
        WriteU32(shape.userData);   // +0x08
        WriteU32(shape.shapeTypeEnum); // +0x0C
        WriteFloat(shape.radius);   // +0x10
        WriteFloat(shape.cylRadius); // +0x14
        WriteFloat(shape.cylBaseRadiusFactor); // +0x18
        PadTo(offset + 32);         // pad to +0x20
        WriteVector4(shape.vertexA);       // +0x20
        WriteVector4(shape.vertexB);       // +0x30
        WriteVector4(shape.perpendicular1); // +0x40
        WriteVector4(shape.perpendicular2); // +0x50
        Pad(16);
        break;
    }

    case ShapeType::ConvexVertices: {
        AddVirtualFixup(offset, "hkpConvexVerticesShape");
        WriteU32(0);              // +0x00 vtable
        WriteU16(0); WriteU16(0); // +0x04 memSizeAndFlags + refCount
        WriteU32(shape.userData);  // +0x08 userData
        WriteU32(shape.shapeTypeEnum); // +0x0C type
        WriteFloat(shape.radius);  // +0x10 radius
        PadTo(offset + Off::CVS_AabbHalfExtents); // pad to +0x20
        WriteVector4(shape.aabbHalfExtents);       // +0x20
        WriteVector4(shape.aabbCenter);             // +0x30
        // Rotated vertices array at +0x40
        uint32_t vertArrayPtrOff = WriteArray(static_cast<uint32_t>(shape.rotatedVertices.size()));
        // numVertices at +0x4C
        WriteU32(static_cast<uint32_t>(shape.numVertices));
        // Plane equations offset differs by version:
        //   v5/v6: +0x50 (immediately after numVertices)
        //   v7:    +0x54 (4 bytes padding after numVertices)
        PadTo(offset + Off::CVS_PlaneEquations(m_FileVersion));
        uint32_t planeArrayPtrOff = WriteArray(static_cast<uint32_t>(shape.planeEquations.size()));
        // Connectivity pointer
        PadTo(offset + Off::CVS_Connectivity(m_FileVersion));
        WriteU32(0);
        Pad(16);

        // Write rotated vertices data
        if (!shape.rotatedVertices.empty()) {
            Pad(16);
            uint32_t vertDataOff = Pos();
            AddLocalFixup(vertArrayPtrOff, vertDataOff);
            for (const auto& ftp : shape.rotatedVertices) {
                WriteVector4(ftp.xs);
                WriteVector4(ftp.ys);
                WriteVector4(ftp.zs);
            }
        }

        // Write plane equations data
        if (!shape.planeEquations.empty()) {
            Pad(16);
            uint32_t planeDataOff = Pos();
            AddLocalFixup(planeArrayPtrOff, planeDataOff);
            for (const auto& p : shape.planeEquations) {
                WriteVector4(p);
            }
        }
        break;
    }

    case ShapeType::Transform: {
        AddVirtualFixup(offset, "hkpTransformShape");
        WriteU32(0);               // +0x00
        WriteU16(0); WriteU16(0);  // +0x04
        WriteU32(shape.userData);   // +0x08
        WriteU32(shape.shapeTypeEnum); // +0x0C
        WriteU32(0);               // +0x10 padding
        uint32_t childPtrOff = Pos(); // +0x14
        WriteU32(0);               // child shape ptr
        PadTo(offset + 48);        // pad to +0x30 for transform alignment
        WriteTransform(shape.childTransform); // +0x30 (64 bytes)
        Pad(16);

        // Write child shape and create global fixup
        if (!shape.children.empty()) {
            uint32_t childOff = WriteShape(shape.children[0]);
            AddGlobalFixup(childPtrOff, 2, childOff); // section 2 = data
        }
        break;
    }

    case ShapeType::ConvexTransform: {
        AddVirtualFixup(offset, "hkpConvexTransformShape");
        WriteU32(0);               // +0x00
        WriteU16(0); WriteU16(0);  // +0x04
        WriteU32(shape.userData);   // +0x08
        WriteU32(shape.shapeTypeEnum); // +0x0C
        WriteFloat(shape.radius);   // +0x10
        PadTo(offset + 24);        // pad to +0x18
        uint32_t childPtrOff = Pos(); // +0x18
        WriteU32(0);               // child ptr
        PadTo(offset + 32);        // pad to +0x20 for transform
        WriteTransform(shape.childTransform); // +0x20 (64 bytes)
        Pad(16);

        if (!shape.children.empty()) {
            uint32_t childOff = WriteShape(shape.children[0]);
            AddGlobalFixup(childPtrOff, 2, childOff);
        }
        break;
    }

    case ShapeType::ConvexTranslate: {
        AddVirtualFixup(offset, "hkpConvexTranslateShape");
        WriteU32(0);               // +0x00
        WriteU16(0); WriteU16(0);  // +0x04
        WriteU32(shape.userData);   // +0x08
        WriteU32(shape.shapeTypeEnum); // +0x0C
        WriteFloat(shape.radius);   // +0x10
        PadTo(offset + 24);        // pad to +0x18
        uint32_t childPtrOff = Pos(); // +0x18
        WriteU32(0);               // child ptr
        PadTo(offset + 32);        // pad to +0x20
        WriteVector4(shape.translation); // +0x20
        Pad(16);

        if (!shape.children.empty()) {
            uint32_t childOff = WriteShape(shape.children[0]);
            AddGlobalFixup(childPtrOff, 2, childOff);
        }
        break;
    }

    case ShapeType::List: {
        AddVirtualFixup(offset, "hkpListShape");
        WriteU32(0);               // +0x00
        WriteU16(0); WriteU16(0);  // +0x04
        WriteU32(shape.userData);   // +0x08
        WriteU32(shape.shapeTypeEnum); // +0x0C
        WriteU8(shape.disableWelding ? 1 : 0); // +0x10
        WriteU8(shape.collectionType);          // +0x11
        PadTo(offset + 24);        // pad to +0x18 (ShapeCollection base=20, aligned to 24)
        // ChildInfo array at +0x18
        uint32_t childArrayPtrOff = WriteArray(static_cast<uint32_t>(shape.children.size()));
        WriteU16(shape.listFlags);       // +0x24
        WriteU16(shape.numDisabledChildren); // +0x26
        PadTo(offset + 48);        // pad to +0x30 for Vector4 alignment
        WriteVector4(shape.listAabbHalfExtents); // +0x30
        WriteVector4(shape.listAabbCenter);       // +0x40
        Pad(16);

        // First write all child shapes so we have their offsets
        std::vector<uint32_t> childShapeOffsets;
        for (const auto& child : shape.children) {
            childShapeOffsets.push_back(WriteShape(child));
        }

        // Now write the ChildInfo array data
        Pad(16);
        uint32_t childInfoDataOff = Pos();
        AddLocalFixup(childArrayPtrOff, childInfoDataOff);
        for (size_t i = 0; i < shape.children.size(); i++) {
            uint32_t entryOff = Pos();
            WriteU32(0); // shape ptr (global fixup)
            uint32_t filterInfo = (i < shape.childCollisionFilterInfos.size()) ?
                shape.childCollisionFilterInfos[i] : 0;
            WriteU32(filterInfo);
            WriteU32(0); // numChildShapeKeys
            WriteU32(0xFFFFFFFF); // shapeKey
            AddGlobalFixup(entryOff, 2, childShapeOffsets[i]);
        }
        break;
    }

    case ShapeType::SimpleContainer: {
        AddVirtualFixup(offset, "hkpSimpleMeshShape");
        WriteU32(0);               // +0x00
        WriteU16(0); WriteU16(0);  // +0x04
        WriteU32(shape.userData);   // +0x08
        WriteU32(shape.shapeTypeEnum); // +0x0C
        WriteU8(shape.disableWelding ? 1 : 0); // +0x10
        WriteU8(shape.collectionType);          // +0x11
        PadTo(offset + 24);        // pad to +0x18
        // Vertices array at +0x18
        uint32_t vertArrayPtrOff = WriteArray(static_cast<uint32_t>(shape.planeEquations.size()));
        // Triangles array at +0x24
        uint32_t triArrayPtrOff = WriteArray(shape.numTriangles);
        // MaterialIndices array at +0x30 (empty)
        WriteArray(0);
        // Radius at +0x3C
        WriteFloat(shape.meshRadius);
        // WeldingType at +0x40
        WriteU8(shape.weldingType);
        Pad(16);

        // Write vertex data
        if (!shape.planeEquations.empty()) {
            Pad(16);
            uint32_t vertDataOff = Pos();
            AddLocalFixup(vertArrayPtrOff, vertDataOff);
            for (const auto& v : shape.planeEquations) {
                WriteVector4(v);
            }
        }

        // Triangle data would go here but we don't currently store triangle indices
        // in the reader (only vertex count and triangle count)
        break;
    }

    case ShapeType::Mopp: {
        AddVirtualFixup(offset, "hkpMoppBvTreeShape");
        WriteU32(0);
        WriteU16(0); WriteU16(0);
        WriteU32(shape.userData);
        WriteU32(shape.shapeTypeEnum);
        WriteU32(0); // bvTreeType + padding
        WriteU32(0); // m_code ptr (would need MOPP code data)
        Pad(16);
        // m_codeInfoCopy (hkVector4)
        WriteVector4({});
        // hkpSingleShapeContainer
        WriteU32(0); // vtable
        uint32_t childPtrOff = Pos();
        WriteU32(0); // child ptr
        Pad(16);

        if (!shape.children.empty()) {
            uint32_t childOff = WriteShape(shape.children[0]);
            AddGlobalFixup(childPtrOff, 2, childOff);
        }
        break;
    }

    case ShapeType::ExtendedMesh: {
        // First write the mesh subpart storage (vertices + indices)
        Pad(16);
        uint32_t storageOff = Pos();
        AddVirtualFixup(storageOff, "hkpStorageExtendedMeshShapeMeshSubpartStorage");
        WriteU32(0); WriteU16(0); WriteU16(0); // hkReferencedObject base

        // vertices hkArray<hkVector4> at +0x08
        uint32_t vertArrayPtrOff = Pos();
        WriteU32(0); // ptr (local fixup)
        WriteU32(static_cast<uint32_t>(shape.planeEquations.size())); // size
        WriteU32(static_cast<uint32_t>(shape.planeEquations.size()) | 0x80000000u); // capacity|flag

        // indices8 hkArray (empty)
        WriteU32(0); WriteU32(0); WriteU32(0x80000000u);
        // indices16 hkArray (empty)
        WriteU32(0); WriteU32(0); WriteU32(0x80000000u);
        // indices32 hkArray at +0x2C
        uint32_t idxArrayPtrOff = Pos();
        uint32_t numIdx = static_cast<uint32_t>(shape.triangles.size()) * 4; // 4 ints per tri (3 verts + material)
        WriteU32(0); // ptr
        WriteU32(numIdx); // size
        WriteU32(numIdx | 0x80000000u); // capacity|flag
        // materialIndices, materials, namedMaterials, materialIndices16 (all empty)
        for (int i = 0; i < 4; i++) { WriteU32(0); WriteU32(0); WriteU32(0x80000000u); }

        // Write vertex data
        Pad(16);
        uint32_t vertDataOff = Pos();
        AddLocalFixup(vertArrayPtrOff, vertDataOff);
        for (const auto& v : shape.planeEquations) {
            WriteFloat(v.x); WriteFloat(v.y); WriteFloat(v.z); WriteFloat(v.w);
        }

        // Write index data (groups of 4: v0, v1, v2, materialIndex=0)
        Pad(4);
        uint32_t idxDataOff = Pos();
        AddLocalFixup(idxArrayPtrOff, idxDataOff);
        for (const auto& tri : shape.triangles) {
            WriteU32(tri.a); WriteU32(tri.b); WriteU32(tri.c); WriteU32(0); // material=0
        }

        // Now write the ExtendedMeshShape itself
        Pad(16);
        offset = Pos(); // update offset to point to the shape, not the storage
        AddVirtualFixup(offset, "hkpStorageExtendedMeshShape");
        WriteU32(0); WriteU16(0); WriteU16(0); // base
        WriteU32(shape.userData);  // +0x08
        WriteU32(shape.shapeTypeEnum); // +0x0C
        WriteU32(0); // disableWelding
        WriteU32(7); // collectionType = COLLECTION_EXTENDED_MESH

        // embeddedTrianglesSubpart (112 bytes of zeros for the inline struct)
        for (int i = 0; i < 28; i++) WriteU32(0);

        // aabbHalfExtents, aabbCenter (compute from vertices)
        float bmin[3] = {1e30f,1e30f,1e30f}, bmax[3] = {-1e30f,-1e30f,-1e30f};
        for (const auto& v : shape.planeEquations) {
            bmin[0]=std::min(bmin[0],v.x); bmin[1]=std::min(bmin[1],v.y); bmin[2]=std::min(bmin[2],v.z);
            bmax[0]=std::max(bmax[0],v.x); bmax[1]=std::max(bmax[1],v.y); bmax[2]=std::max(bmax[2],v.z);
        }
        float hx=(bmax[0]-bmin[0])*0.5f, hy=(bmax[1]-bmin[1])*0.5f, hz=(bmax[2]-bmin[2])*0.5f;
        float cx=(bmin[0]+bmax[0])*0.5f, cy=(bmin[1]+bmax[1])*0.5f, cz=(bmin[2]+bmax[2])*0.5f;
        WriteFloat(hx); WriteFloat(hy); WriteFloat(hz); WriteFloat(0);
        WriteFloat(cx); WriteFloat(cy); WriteFloat(cz); WriteFloat(0);

        // Various fields until meshstorage
        WriteU32(0); // numBitsForSubpartIndex
        // trianglesSubparts, shapesSubparts, weldingInfo (hkArrays, empty)
        for (int i = 0; i < 3; i++) { WriteU32(0); WriteU32(0); WriteU32(0x80000000u); }
        WriteU16(0); WriteU16(0); // weldingType + padding
        WriteU32(0); // defaultCollisionFilterInfo
        WriteU32(0); // cachedNumChildShapes
        WriteFloat(0.01f); // triangleRadius

        // meshstorage hkArray<hkpStorageExtendedMeshShapeMeshSubpartStorage*>
        uint32_t msArrayPtrOff = Pos();
        WriteU32(0); // ptr
        WriteU32(1); // size (1 storage)
        WriteU32(1 | 0x80000000u);

        // shapestorage (empty)
        WriteU32(0); WriteU32(0); WriteU32(0x80000000u);

        Pad(16);

        // Write the meshstorage pointer array (1 entry pointing to storageOff)
        uint32_t msArrayDataOff = Pos();
        AddLocalFixup(msArrayPtrOff, msArrayDataOff);
        uint32_t storagePtrOff = Pos();
        WriteU32(0); // ptr to storage
        AddLocalFixup(storagePtrOff, storageOff);

        break;
    }

    default:
        // Unknown shape type - write minimal stub
        AddVirtualFixup(offset, shape.className.empty() ? "hkpShape" : shape.className);
        WriteU32(0);
        WriteU16(0); WriteU16(0);
        WriteU32(shape.userData);
        WriteU32(shape.shapeTypeEnum);
        Pad(16);
        break;
    }

    return offset;
}

uint32_t HkxWriter::WriteRigidBody(const RigidBodyInfo& rb) {
    Pad(16);
    uint32_t base = Pos();
    AddVirtualFixup(base, "hkpRigidBody");

    // hkpRigidBody serialized layout (Havok 5.5/7.1, 32-bit):
    //
    // Binary packfiles store objects at their full runtime size (0x220 = 544 bytes)
    // with nosave fields zeroed. All offsets from hkx_binary_offsets.h, verified
    // via Ghidra RE of legouniverse.exe Havok reflection metadata.
    //
    // Pre-allocate the full object zeroed, then write fields at exact offsets.
    PadTo(base + Off::RB_TotalSize);

    // hkBaseObject + hkReferencedObject (vtable, memSizeAndFlags, refCount) — zeros

    // hkpWorldObject
    // +0x08: world ptr (nosave, serialized as 0) — already zero
    WriteU32At(base + Off::RB_UserData, rb.userData);

    // hkpCollidable (embedded at +0x10)
    uint32_t shapePtrOff = base + Off::RB_Shape;
    // shape ptr written as 0, patched via global fixup below
    WriteU32At(base + Off::RB_ShapeKey, rb.shapeKey);
    WriteU32At(base + Off::RB_CollFilterInfo, rb.collisionFilterInfo);
    WriteI8At(base + Off::RB_QualityType, rb.qualityType);
    WriteFloatAt(base + Off::RB_AllowedPenDepth, rb.allowedPenetrationDepth);

    // Name pointer at +0x74 — patched via local fixup below
    uint32_t namePtrOff = base + Off::RB_Name;

    // hkpMaterial (at +0x8C, 12 bytes)
    WriteI8At(base + Off::RB_ResponseType, rb.material.responseType);
    WriteFloatAt(base + Off::RB_Friction, rb.material.friction);
    WriteFloatAt(base + Off::RB_Restitution, rb.material.restitution);

    // hkpEntity fields
    WriteFloatAt(base + Off::RB_DamageMultiplier, rb.damageMultiplier);
    WriteU16At(base + Off::RB_ContactCBDelay, rb.contactPointCallbackDelay);
    WriteI8At(base + Off::RB_AutoRemoveLevel, rb.autoRemoveLevel);
    WriteU8At(base + Off::RB_NumShapeKeysCPP, rb.numShapeKeysInContactPointProperties);
    WriteU8At(base + Off::RB_RespModFlags, rb.responseModifierFlags);
    WriteU32At(base + Off::RB_UID, rb.uid);

    // hkpMotion (embedded at +0xE0, size 0x120)
    WriteU8At(base + Off::RB_MotionType, static_cast<uint8_t>(rb.motion.type));
    WriteU8At(base + Off::RB_DeactIntCounter, rb.motion.deactivationIntegrateCounter);
    WriteU16At(base + Off::RB_DeactNumFrames, rb.motion.deactivationNumInactiveFrames[0]);
    WriteU16At(base + Off::RB_DeactNumFrames + 2, rb.motion.deactivationNumInactiveFrames[1]);

    // hkMotionState transform (hkTransform at +0xF0, 64 bytes)
    WriteTransformAt(base + Off::RB_Transform, rb.motion.motionState.transform);

    // hkSweptTransform (at +0x130)
    WriteVector4At(base + Off::RB_CenterOfMass0, rb.motion.motionState.centerOfMass0);
    WriteVector4At(base + Off::RB_CenterOfMass1, rb.motion.motionState.centerOfMass1);
    WriteQuaternionAt(base + Off::RB_Rotation0, rb.motion.motionState.rotation0);
    WriteQuaternionAt(base + Off::RB_Rotation1, rb.motion.motionState.rotation1);
    WriteVector4At(base + Off::RB_COMLocal, rb.motion.motionState.centerOfMassLocal);

    // Remaining hkMotionState fields
    WriteVector4At(base + Off::RB_DeltaAngle, rb.motion.motionState.deltaAngle);
    WriteFloatAt(base + Off::RB_ObjectRadius, rb.motion.motionState.objectRadius);
    WriteFloatAt(base + Off::RB_LinearDamping, rb.motion.motionState.linearDamping);
    WriteFloatAt(base + Off::RB_AngularDamping, rb.motion.motionState.angularDamping);

    // maxLinearVelocity and maxAngularVelocity are stored as u8 (hkUFloat8 quantized)
    WriteU8At(base + Off::RB_MaxLinVel,
              static_cast<uint8_t>(rb.motion.motionState.maxLinearVelocity));
    WriteU8At(base + Off::RB_MaxAngVel,
              static_cast<uint8_t>(rb.motion.motionState.maxAngularVelocity));
    WriteU8At(base + Off::RB_DeactClass, rb.motion.motionState.deactivationClass);

    // hkpMotion physics fields
    WriteVector4At(base + Off::RB_InertiaInvMass, rb.motion.inertiaAndMassInv);
    WriteVector4At(base + Off::RB_LinearVelocity, rb.motion.linearVelocity);
    WriteVector4At(base + Off::RB_AngularVelocity, rb.motion.angularVelocity);
    WriteVector4At(base + Off::RB_DeactRefPos, rb.motion.deactivationRefPosition[0]);
    WriteVector4At(base + Off::RB_DeactRefPos + 16, rb.motion.deactivationRefPosition[1]);

    // gravityFactor stored as hkHalf (int16, value = raw / 16384.0)
    int16_t gravHalf = static_cast<int16_t>(rb.motion.gravityFactor * 16384.0f);
    WriteU16At(base + Off::RB_GravityFactor, static_cast<uint16_t>(gravHalf));

    // Write shape and set up global fixup
    if (rb.shape.type != ShapeType::Unknown) {
        uint32_t shapeOff = WriteShape(rb.shape);
        AddGlobalFixup(shapePtrOff, 2, shapeOff);
    }

    // Write name string data (appended after the rigid body object)
    if (!rb.name.empty()) {
        uint32_t nameDataOff = Pos();
        for (char c : rb.name) WriteU8(static_cast<uint8_t>(c));
        WriteU8(0);
        AddLocalFixup(namePtrOff, nameDataOff);
        Pad(4);
    }

    return base;
}

uint32_t HkxWriter::WritePhysicsSystem(const PhysicsSystemInfo& sys) {
    Pad(16);
    uint32_t offset = Pos();
    AddVirtualFixup(offset, "hkpPhysicsSystem");

    // Write header with placeholder arrays first
    WriteU32(0);               // +0x00 vtable
    WriteU16(0); WriteU16(0);  // +0x04

    // RigidBodies array at +0x08
    uint32_t rbArrayPtrOff = WriteArray(static_cast<uint32_t>(sys.rigidBodies.size()));
    // Constraints array at +0x14
    WriteArray(0);
    // Actions array at +0x20
    WriteArray(0);
    // Phantoms array at +0x2C
    WriteArray(0);
    // Name at +0x38
    uint32_t namePtrOff = Pos();
    WriteU32(0);
    WriteU32(sys.userData);
    WriteU8(sys.active ? 1 : 0);
    Pad(16);

    // Now write rigid bodies (children) - header is done, data grows after it
    std::vector<uint32_t> rbOffsets;
    for (const auto& rb : sys.rigidBodies) {
        rbOffsets.push_back(WriteRigidBody(rb));
    }

    // Write RB pointer array data
    if (!rbOffsets.empty()) {
        Pad(4);
        uint32_t rbArrayDataOff = Pos();
        AddLocalFixup(rbArrayPtrOff, rbArrayDataOff);
        for (uint32_t rbOff : rbOffsets) {
            uint32_t ptrOff = Pos();
            WriteU32(0);
            AddGlobalFixup(ptrOff, 2, rbOff);
        }
    }

    // Write name string
    if (!sys.name.empty()) {
        uint32_t nameDataOff = Pos();
        for (char c : sys.name) WriteU8(static_cast<uint8_t>(c));
        WriteU8(0);
        AddLocalFixup(namePtrOff, nameDataOff);
        Pad(4);
    }

    return offset;
}

uint32_t HkxWriter::WritePhysicsData(const PhysicsDataInfo& data) {
    Pad(16);
    uint32_t offset = Pos();
    AddVirtualFixup(offset, "hkpPhysicsData");

    // hkReferencedObject (8 bytes)
    WriteU32(0);
    WriteU16(0); WriteU16(0);

    // worldCinfo ptr (null)
    WriteU32(0);
    // Systems array
    uint32_t sysArrayPtrOff = WriteArray(static_cast<uint32_t>(data.systems.size()));
    Pad(16);

    // Write systems
    std::vector<uint32_t> sysOffsets;
    for (const auto& sys : data.systems) {
        sysOffsets.push_back(WritePhysicsSystem(sys));
    }

    // Write system pointer array
    if (!sysOffsets.empty()) {
        Pad(4);
        uint32_t sysArrayDataOff = Pos();
        AddLocalFixup(sysArrayPtrOff, sysArrayDataOff);
        for (uint32_t sysOff : sysOffsets) {
            uint32_t ptrOff = Pos();
            WriteU32(0);
            AddGlobalFixup(ptrOff, 2, sysOff);
        }
    }

    return offset;
}

uint32_t HkxWriter::WriteRootLevelContainer(const std::vector<NamedVariant>& variants) {
    Pad(16);
    uint32_t offset = Pos();
    AddVirtualFixup(offset, "hkRootLevelContainer");

    // hkRootLevelContainer has only m_namedVariants hkArray
    // No hkReferencedObject base - it's directly the array
    uint32_t variantArrayPtrOff = Pos();
    WriteU32(0);     // ptr
    WriteU32(static_cast<uint32_t>(variants.size()));
    WriteU32(static_cast<uint32_t>(variants.size()) | 0x80000000);
    Pad(16);

    // Write variant entries (each 16 bytes: name ptr, className ptr, object ptr, class ptr)
    if (!variants.empty()) {
        Pad(4);
        uint32_t variantDataOff = Pos();
        AddLocalFixup(variantArrayPtrOff, variantDataOff);

        struct PendingStr { uint32_t ptrOff; std::string str; };
        std::vector<PendingStr> pendingStrings;

        for (const auto& nv : variants) {
            uint32_t namePtrOff = Pos();
            WriteU32(0); // name ptr
            uint32_t classPtrOff = Pos();
            WriteU32(0); // className ptr
            uint32_t objPtrOff = Pos();
            WriteU32(0); // object ptr
            WriteU32(0); // variant class ptr

            if (!nv.name.empty()) pendingStrings.push_back({namePtrOff, nv.name});
            if (!nv.className.empty()) pendingStrings.push_back({classPtrOff, nv.className});
            if (nv.objectOffset != 0 && nv.objectOffset != 0xFFFFFFFF) {
                AddGlobalFixup(objPtrOff, 2, nv.objectOffset);
            }
        }

        // Write strings
        for (auto& ps : pendingStrings) {
            uint32_t strOff = Pos();
            for (char c : ps.str) WriteU8(static_cast<uint8_t>(c));
            WriteU8(0);
            AddLocalFixup(ps.ptrOff, strOff);
        }
        Pad(4);
    }

    return offset;
}

// --- Main write functions ---

bool HkxWriter::Write(const std::filesystem::path& outputPath,
                       const ParseResult& result,
                       const WriteOptions& options) {
    Reset();
    m_FileVersion = options.fileVersion;

    // Write physics data hierarchy
    if (!result.physicsData.empty()) {
        // Write root level container pointing to physics data
        uint32_t physDataOff = WritePhysicsData(result.physicsData[0]);

        // Build named variants for RLC
        std::vector<NamedVariant> variants;
        NamedVariant nv;
        nv.name = "hkpPhysicsData";
        nv.className = "hkpPhysicsData";
        nv.objectOffset = physDataOff;
        variants.push_back(nv);
        WriteRootLevelContainer(variants);
    } else if (!result.rigidBodies.empty()) {
        // No physics data container - write rigid bodies in a system
        PhysicsSystemInfo sys;
        sys.active = true;
        sys.rigidBodies = result.rigidBodies;

        PhysicsDataInfo data;
        data.systems.push_back(sys);
        uint32_t physDataOff = WritePhysicsData(data);

        std::vector<NamedVariant> variants;
        NamedVariant nv;
        nv.name = "hkpPhysicsData";
        nv.className = "hkpPhysicsData";
        nv.objectOffset = physDataOff;
        variants.push_back(nv);
        WriteRootLevelContainer(variants);
    } else {
        m_Error = "No physics objects to write";
        return false;
    }

    auto fileData = BuildFile(options);
    if (fileData.empty()) return false;

    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
        m_Error = "Failed to open output file: " + outputPath.string();
        return false;
    }
    file.write(reinterpret_cast<const char*>(fileData.data()), fileData.size());
    return true;
}

bool HkxWriter::Write(const std::filesystem::path& outputPath,
                       const std::vector<RigidBodyInfo>& rigidBodies,
                       const WriteOptions& options) {
    ParseResult result;
    result.rigidBodies = rigidBodies;
    return Write(outputPath, result, options);
}

std::vector<uint8_t> HkxWriter::BuildFile(const WriteOptions& options) {
    // Pad classnames section
    while (m_ClassnamesSection.size() % 16 != 0) m_ClassnamesSection.push_back(0);
    uint32_t classnamesDataSize = static_cast<uint32_t>(m_ClassnamesSection.size());

    // Pad data section
    Pad(16);
    uint32_t dataSectionBodySize = Pos();

    // Build fixup sections
    std::vector<uint8_t> localFixupData;
    auto writeFixupU32 = [](std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF);
        buf.push_back((v >> 16) & 0xFF); buf.push_back((v >> 24) & 0xFF);
    };

    // Sort fixups by source offset
    std::sort(m_LocalFixups.begin(), m_LocalFixups.end(),
        [](const auto& a, const auto& b) { return a.src < b.src; });
    std::sort(m_GlobalFixups.begin(), m_GlobalFixups.end(),
        [](const auto& a, const auto& b) { return a.src < b.src; });

    for (const auto& f : m_LocalFixups) {
        writeFixupU32(localFixupData, f.src);
        writeFixupU32(localFixupData, f.dst);
    }
    // Sentinel
    writeFixupU32(localFixupData, 0xFFFFFFFF);
    writeFixupU32(localFixupData, 0xFFFFFFFF);

    std::vector<uint8_t> globalFixupData;
    for (const auto& f : m_GlobalFixups) {
        writeFixupU32(globalFixupData, f.src);
        writeFixupU32(globalFixupData, f.dstSection);
        writeFixupU32(globalFixupData, f.dst);
    }
    writeFixupU32(globalFixupData, 0xFFFFFFFF);
    writeFixupU32(globalFixupData, 0xFFFFFFFF);
    writeFixupU32(globalFixupData, 0xFFFFFFFF);

    std::vector<uint8_t> virtualFixupData;
    for (const auto& f : m_VirtualFixups) {
        auto it = m_RegisteredClasses.find(f.className);
        writeFixupU32(virtualFixupData, f.dataOffset);
        writeFixupU32(virtualFixupData, 0); // classnames section index
        writeFixupU32(virtualFixupData, it != m_RegisteredClasses.end() ? it->second.nameOffset : 0);
    }
    writeFixupU32(virtualFixupData, 0xFFFFFFFF);
    writeFixupU32(virtualFixupData, 0xFFFFFFFF);
    writeFixupU32(virtualFixupData, 0xFFFFFFFF);

    uint32_t localFixupsOffset = dataSectionBodySize;
    uint32_t globalFixupsOffset = localFixupsOffset + static_cast<uint32_t>(localFixupData.size());
    uint32_t virtualFixupsOffset = globalFixupsOffset + static_cast<uint32_t>(globalFixupData.size());
    uint32_t exportsOffset = virtualFixupsOffset + static_cast<uint32_t>(virtualFixupData.size());

    // Calculate absolute positions
    uint32_t headerSize = 64;
    uint32_t numSections = 3;
    uint32_t sectionHeadersSize = numSections * 48;
    uint32_t classnamesAbsStart = headerSize + sectionHeadersSize;
    uint32_t typesAbsStart = classnamesAbsStart + classnamesDataSize;
    uint32_t dataAbsStart = typesAbsStart; // types section is empty

    // Build final file
    std::vector<uint8_t> file;
    auto wU8 = [&](uint8_t v) { file.push_back(v); };
    auto wU16 = [&](uint16_t v) { writeFixupU32(file, v); file.resize(file.size() - 2); }; // hacky
    auto wU32 = [&](uint32_t v) { writeFixupU32(file, v); };
    auto wBytes = [&](const void* data, size_t n) {
        auto* p = static_cast<const uint8_t*>(data);
        file.insert(file.end(), p, p + n);
    };

    // Fix wU16
    auto writeU16 = [&](uint16_t v) {
        file.push_back(v & 0xFF);
        file.push_back((v >> 8) & 0xFF);
    };

    // Header (64 bytes)
    wU32(BINARY_MAGIC_0);
    wU32(BINARY_MAGIC_1);
    wU32(0);                    // userTag
    wU32(options.fileVersion);
    wU8(options.pointerSize);
    wU8(1);                     // littleEndian
    wU8(0);                     // reusePaddingOptimization
    wU8(1);                     // emptyBaseClassOptimization
    wU32(numSections);
    wU32(2);                    // contentsSectionIndex (data)
    wU32(0);                    // contentsSectionOffset
    wU32(0);                    // contentsClassNameSectionIndex
    wU32(0);                    // contentsClassNameSectionOffset
    {
        char ver[16] = {};
        strncpy(ver, options.havokVersion.c_str(), 15);
        wBytes(ver, 16);
    }
    wU32(0);                    // flags
    writeU16(0);                // maxPredicate
    writeU16(0);                // predicateArraySizePlusPadding

    // Section headers
    auto writeSecHeader = [&](const char* tag, uint32_t absStart,
        uint32_t localFix, uint32_t globalFix, uint32_t virtualFix,
        uint32_t exports, uint32_t imports, uint32_t end) {
        char tagBuf[20] = {};
        strncpy(tagBuf, tag, 19);
        wBytes(tagBuf, 20);
        wU32(absStart);
        wU32(localFix);
        wU32(globalFix);
        wU32(virtualFix);
        wU32(exports);
        wU32(imports);
        wU32(end);
    };

    writeSecHeader("__classnames__", classnamesAbsStart,
        classnamesDataSize, classnamesDataSize, classnamesDataSize,
        classnamesDataSize, classnamesDataSize, classnamesDataSize);

    writeSecHeader("__types__", typesAbsStart, 0, 0, 0, 0, 0, 0);

    writeSecHeader("__data__", dataAbsStart,
        localFixupsOffset, globalFixupsOffset, virtualFixupsOffset,
        exportsOffset, exportsOffset, exportsOffset);

    // Section data
    file.insert(file.end(), m_ClassnamesSection.begin(), m_ClassnamesSection.end());
    file.insert(file.end(), m_DataSection.begin(), m_DataSection.end());
    file.insert(file.end(), localFixupData.begin(), localFixupData.end());
    file.insert(file.end(), globalFixupData.begin(), globalFixupData.end());
    file.insert(file.end(), virtualFixupData.begin(), virtualFixupData.end());

    return file;
}

} // namespace Hkx
