// Tagged format object conversion.
//
// Converts tagged binary format objects (parsed by HkxTaggedReader) into the
// shared ParseResult domain types. Handles physics shapes, rigid bodies,
// physics systems, physics data, and scene data (meshes, nodes, scenes).

#include "havok/reader/hkx_tagged_reader.h"

#include <cstring>
#include <iostream>

namespace Hkx {

// --- Object resolution ---

const TaggedObject* HkxTaggedReader::ResolveObject(int refId) const {
    if (refId <= 0 || refId > static_cast<int>(m_Objects.size())) return nullptr;
    return &m_Objects[refId - 1];
}

// --- Field access helpers ---

float HkxTaggedReader::GetFloatField(const TaggedObject& obj, const std::string& name, float def) const {
    auto it = obj.fields.find(name);
    if (it == obj.fields.end()) return def;
    if (auto* d = std::get_if<double>(&it->second.data)) return static_cast<float>(*d);
    if (auto* i = std::get_if<int64_t>(&it->second.data)) return static_cast<float>(*i);
    return def;
}

int64_t HkxTaggedReader::GetIntField(const TaggedObject& obj, const std::string& name, int64_t def) const {
    auto it = obj.fields.find(name);
    if (it == obj.fields.end()) return def;
    if (auto* i = std::get_if<int64_t>(&it->second.data)) return *i;
    return def;
}

std::string HkxTaggedReader::GetStringField(const TaggedObject& obj, const std::string& name) const {
    auto it = obj.fields.find(name);
    if (it == obj.fields.end()) return "";
    if (auto* s = std::get_if<std::string>(&it->second.data)) return *s;
    return "";
}

std::vector<float> HkxTaggedReader::GetVecField(const TaggedObject& obj, const std::string& name) const {
    auto it = obj.fields.find(name);
    if (it == obj.fields.end()) return {};
    if (auto* v = std::get_if<std::vector<float>>(&it->second.data)) return *v;
    return {};
}

int HkxTaggedReader::GetObjectRef(const TaggedObject& obj, const std::string& name) const {
    auto it = obj.fields.find(name);
    if (it == obj.fields.end()) return 0;
    if (auto* i = std::get_if<int>(&it->second.data)) return *i;
    return 0;
}

const TaggedArray* HkxTaggedReader::GetArrayField(const TaggedObject& obj, const std::string& name) const {
    auto it = obj.fields.find(name);
    if (it == obj.fields.end()) return nullptr;
    return std::get_if<TaggedArray>(&it->second.data);
}

// --- Shape conversion ---

ShapeInfo HkxTaggedReader::ConvertShape(const TaggedObject& obj) {
    ShapeInfo shape;
    shape.className = obj.typeName;
    shape.type = ShapeType::Unknown;

    if (obj.typeName == "hkpBoxShape") shape.type = ShapeType::Box;
    else if (obj.typeName == "hkpSphereShape") shape.type = ShapeType::Sphere;
    else if (obj.typeName == "hkpCapsuleShape") shape.type = ShapeType::Capsule;
    else if (obj.typeName == "hkpCylinderShape") shape.type = ShapeType::Cylinder;
    else if (obj.typeName == "hkpConvexVerticesShape") shape.type = ShapeType::ConvexVertices;
    else if (obj.typeName == "hkpConvexTransformShape") shape.type = ShapeType::ConvexTransform;
    else if (obj.typeName == "hkpConvexTranslateShape") shape.type = ShapeType::ConvexTranslate;
    else if (obj.typeName == "hkpListShape") shape.type = ShapeType::List;
    else if (obj.typeName == "hkpTransformShape") shape.type = ShapeType::Transform;
    else if (obj.typeName == "hkpMoppBvTreeShape") shape.type = ShapeType::Mopp;
    else if (obj.typeName == "hkpSimpleMeshShape") shape.type = ShapeType::SimpleContainer;
    else if (obj.typeName == "hkpStorageExtendedMeshShape") shape.type = ShapeType::ExtendedMesh;
    else if (obj.typeName == "hkpSingleShapeContainer") {
        // Wrapper - follow childShape to the actual shape
        int cr = GetObjectRef(obj, "childShape");
        if (cr > 0) {
            const auto* c = ResolveObject(cr);
            if (c) return ConvertShape(*c);
        }
        return shape;
    }
    else return shape;

    shape.radius = GetFloatField(obj, "radius", 0);

    switch (shape.type) {
    case ShapeType::Box: {
        auto he = GetVecField(obj, "halfExtents");
        if (he.size() >= 3) shape.halfExtents = {he[0], he[1], he[2], he.size()>3?he[3]:0};
        break;
    }
    case ShapeType::Sphere:
        // Radius already extracted above from hkpConvexShape base
        break;
    case ShapeType::Capsule: {
        auto va = GetVecField(obj, "vertexA");
        auto vb = GetVecField(obj, "vertexB");
        if (va.size() >= 3) shape.vertexA = {va[0], va[1], va[2], va.size()>3?va[3]:0};
        if (vb.size() >= 3) shape.vertexB = {vb[0], vb[1], vb[2], vb.size()>3?vb[3]:0};
        break;
    }
    case ShapeType::Cylinder: {
        auto va = GetVecField(obj, "vertexA");
        auto vb = GetVecField(obj, "vertexB");
        auto p1 = GetVecField(obj, "perpendicular1");
        auto p2 = GetVecField(obj, "perpendicular2");
        if (va.size() >= 3) shape.vertexA = {va[0], va[1], va[2], va.size()>3?va[3]:0};
        if (vb.size() >= 3) shape.vertexB = {vb[0], vb[1], vb[2], vb.size()>3?vb[3]:0};
        if (p1.size() >= 3) shape.perpendicular1 = {p1[0], p1[1], p1[2], p1.size()>3?p1[3]:0};
        if (p2.size() >= 3) shape.perpendicular2 = {p2[0], p2[1], p2[2], p2.size()>3?p2[3]:0};
        shape.cylRadius = GetFloatField(obj, "cylRadius", 0);
        shape.cylBaseRadiusFactor = GetFloatField(obj, "cylBaseRadiusFactor", 0);
        break;
    }
    case ShapeType::ConvexVertices: {
        auto ahe = GetVecField(obj, "aabbHalfExtents");
        auto ac = GetVecField(obj, "aabbCenter");
        if (ahe.size() >= 3) shape.aabbHalfExtents = {ahe[0], ahe[1], ahe[2], 0};
        if (ac.size() >= 3) shape.aabbCenter = {ac[0], ac[1], ac[2], 0};
        int rawNumVerts = static_cast<int32_t>(GetIntField(obj, "numVertices", 0));

        // rotatedVertices: array of FourVectors structs (x,y,z vec4 fields)
        auto* rvArr = GetArrayField(obj, "rotatedVertices");
        if (rvArr) {
            for (const auto& rvVal : *rvArr) {
                if (auto* ref = std::get_if<int>(&rvVal.data)) {
                    const auto* fvObj = ResolveObject(*ref);
                    if (fvObj) {
                        FourTransposedPoints ftp;
                        auto xv = GetVecField(*fvObj, "x");
                        auto yv = GetVecField(*fvObj, "y");
                        auto zv = GetVecField(*fvObj, "z");
                        if (xv.size() >= 4) ftp.xs = {xv[0], xv[1], xv[2], xv[3]};
                        if (yv.size() >= 4) ftp.ys = {yv[0], yv[1], yv[2], yv[3]};
                        if (zv.size() >= 4) ftp.zs = {zv[0], zv[1], zv[2], zv[3]};
                        shape.rotatedVertices.push_back(ftp);
                    }
                }
            }
        }

        // Compute numVertices: prefer the explicit field value from the object,
        // fall back to inferring from rotatedVertices count (each FourVectors stores 4 verts)
        if (rawNumVerts > 0) {
            shape.numVertices = rawNumVerts;
        } else if (!shape.rotatedVertices.empty()) {
            shape.numVertices = static_cast<int32_t>(shape.rotatedVertices.size()) * 4;
        }

        // planeEquations: array of vec4
        auto* peArr = GetArrayField(obj, "planeEquations");
        if (peArr) {
            for (const auto& peVal : *peArr) {
                if (auto* v = std::get_if<std::vector<float>>(&peVal.data)) {
                    if (v->size() >= 4)
                        shape.planeEquations.push_back({(*v)[0], (*v)[1], (*v)[2], (*v)[3]});
                }
            }
        }
        break;
    }
    case ShapeType::List: {
        auto ahe = GetVecField(obj, "aabbHalfExtents");
        auto ac = GetVecField(obj, "aabbCenter");
        if (ahe.size() >= 3) shape.listAabbHalfExtents = {ahe[0], ahe[1], ahe[2], 0};
        if (ac.size() >= 3) shape.listAabbCenter = {ac[0], ac[1], ac[2], 0};
        // Children via childInfo struct array - each childInfo has a "shape" field
        auto* ciArr = GetArrayField(obj, "childInfo");
        if (ciArr) {
            for (const auto& ci : *ciArr) {
                if (auto* ref = std::get_if<int>(&ci.data)) {
                    const auto* ciObj = ResolveObject(*ref);
                    if (!ciObj) continue;
                    // childInfo struct has "shape" field pointing to actual shape
                    int shapeRef = GetObjectRef(*ciObj, "shape");
                    if (shapeRef > 0) {
                        const auto* shapeObj = ResolveObject(shapeRef);
                        if (shapeObj) {
                            shape.children.push_back(ConvertShape(*shapeObj));
                            continue;
                        }
                    }
                    // Fallback: try converting the childInfo object itself as a shape
                    const auto* c = ciObj;
                    if (c) shape.children.push_back(ConvertShape(*c));
                }
            }
        }
        break;
    }
    case ShapeType::Transform:
    case ShapeType::ConvexTransform: {
        auto t = GetVecField(obj, "transform");
        if (t.size() >= 16) {
            shape.childTransform.col0 = {t[0], t[1], t[2], t[3]};
            shape.childTransform.col1 = {t[4], t[5], t[6], t[7]};
            shape.childTransform.col2 = {t[8], t[9], t[10], t[11]};
            shape.childTransform.translation = {t[12], t[13], t[14], t[15]};
        }
        int cr = GetObjectRef(obj, "childShape");
        if (cr > 0) { const auto* c = ResolveObject(cr); if (c) shape.children.push_back(ConvertShape(*c)); }
        break;
    }
    case ShapeType::ConvexTranslate: {
        auto tr = GetVecField(obj, "translation");
        if (tr.size() >= 3) shape.translation = {tr[0], tr[1], tr[2], 0};
        int cr = GetObjectRef(obj, "childShape");
        if (cr > 0) { const auto* c = ResolveObject(cr); if (c) shape.children.push_back(ConvertShape(*c)); }
        break;
    }
    case ShapeType::Mopp: {
        int cr = GetObjectRef(obj, "child");
        if (cr <= 0) cr = GetObjectRef(obj, "childShape");
        if (cr > 0) { const auto* c = ResolveObject(cr); if (c) shape.children.push_back(ConvertShape(*c)); }
        break;
    }
    case ShapeType::ExtendedMesh: {
        // Search for MeshSubpartStorage objects and extract vertex/index data
        for (const auto& storObj : m_Objects) {
            if (storObj.typeName.find("MeshSubpartStorage") == std::string::npos) continue;

            // vertices: array of vec4 (hkVector4)
            auto* vertsArr = GetArrayField(storObj, "vertices");
            if (vertsArr && !vertsArr->empty()) {
                for (const auto& vVal : *vertsArr) {
                    if (auto* v = std::get_if<std::vector<float>>(&vVal.data)) {
                        if (v->size() >= 3)
                            shape.planeEquations.push_back({(*v)[0], (*v)[1], (*v)[2], v->size() > 3 ? (*v)[3] : 0});
                    }
                }
            }

            // indices32: flat uint32 array (every 4th is padding: a,b,c,0)
            auto* idx32Arr = GetArrayField(storObj, "indices32");
            if (idx32Arr && idx32Arr->size() >= 4) {
                for (size_t i = 0; i + 3 < idx32Arr->size(); i += 4) {
                    uint32_t a = 0, b = 0, c = 0;
                    if (auto* iv = std::get_if<int64_t>(&(*idx32Arr)[i].data)) a = static_cast<uint32_t>(*iv);
                    if (auto* iv = std::get_if<int64_t>(&(*idx32Arr)[i+1].data)) b = static_cast<uint32_t>(*iv);
                    if (auto* iv = std::get_if<int64_t>(&(*idx32Arr)[i+2].data)) c = static_cast<uint32_t>(*iv);
                    shape.triangles.push_back({a, b, c});
                }
            }

            // indices16 fallback
            if (shape.triangles.empty()) {
                auto* idx16Arr = GetArrayField(storObj, "indices16");
                if (idx16Arr && idx16Arr->size() >= 3) {
                    for (size_t i = 0; i + 2 < idx16Arr->size(); i += 3) {
                        uint32_t a = 0, b = 0, c = 0;
                        if (auto* iv = std::get_if<int64_t>(&(*idx16Arr)[i].data)) a = static_cast<uint32_t>(*iv);
                        if (auto* iv = std::get_if<int64_t>(&(*idx16Arr)[i+1].data)) b = static_cast<uint32_t>(*iv);
                        if (auto* iv = std::get_if<int64_t>(&(*idx16Arr)[i+2].data)) c = static_cast<uint32_t>(*iv);
                        shape.triangles.push_back({a, b, c});
                    }
                }
            }
        }

        shape.numVertices = static_cast<int32_t>(shape.planeEquations.size());
        shape.numTriangles = static_cast<int32_t>(shape.triangles.size());
        if (!shape.planeEquations.empty()) shape.type = ShapeType::SimpleContainer;
        break;
    }
    default: break;
    }
    return shape;
}

// --- Rigid body conversion ---

RigidBodyInfo HkxTaggedReader::ConvertRigidBody(const TaggedObject& obj) {
    RigidBodyInfo rb;
    rb.name = GetStringField(obj, "name");

    // Follow the inheritance chain to find fields from parent classes.
    // hkpRigidBody -> hkpEntity -> hkpWorldObject
    // The tagged reader flattens all inherited fields into the object's field map
    // via FlattenTypes(), so fields from hkpWorldObject and hkpEntity are directly
    // accessible on the hkpRigidBody object.

    // --- Scalar fields (from hkpEntity / hkpWorldObject) ---

    rb.userData = static_cast<uint32_t>(GetIntField(obj, "userData", 0));
    rb.damageMultiplier = GetFloatField(obj, "damageMultiplier", 1.0f);
    rb.contactPointCallbackDelay = static_cast<uint16_t>(GetIntField(obj, "contactPointCallbackDelay", 0xFFFF));
    rb.autoRemoveLevel = static_cast<int8_t>(GetIntField(obj, "autoRemoveLevel", 0));
    rb.numShapeKeysInContactPointProperties = static_cast<uint8_t>(GetIntField(obj, "numShapeKeysInContactPointProperties", 0));
    rb.responseModifierFlags = static_cast<uint8_t>(GetIntField(obj, "responseModifierFlags", 0));
    rb.uid = static_cast<uint32_t>(GetIntField(obj, "uid", 0xFFFFFFFF));

    // --- Material (hkpEntity.material — inline struct stored as object ref) ---

    int matRef = GetObjectRef(obj, "material");
    if (matRef > 0) {
        const auto* matObj = ResolveObject(matRef);
        if (matObj) {
            rb.material.friction = GetFloatField(*matObj, "friction", 0.5f);
            rb.material.restitution = GetFloatField(*matObj, "restitution", 0.4f);
            rb.material.responseType = static_cast<int8_t>(GetIntField(*matObj, "responseType", 0));
            rb.material.rollingFrictionMultiplier = GetFloatField(*matObj, "rollingFrictionMultiplier", 0.0f);
        }
    }
    rb.friction = rb.material.friction;
    rb.restitution = rb.material.restitution;

    // --- Motion (hkpEntity.motion — inline struct, hkpMaxSizeMotion) ---
    // The motion struct contains the full motion state including transform,
    // swept transform, velocities, inertia, and deactivation data.

    int motionRef = GetObjectRef(obj, "motion");
    if (motionRef > 0) {
        const auto* motObj = ResolveObject(motionRef);
        if (motObj) {
            rb.motion.type = static_cast<MotionType>(GetIntField(*motObj, "type", 5));
            rb.motion.deactivationIntegrateCounter = static_cast<uint8_t>(GetIntField(*motObj, "deactivationIntegrateCounter", 0));

            // deactivationNumInactiveFrames (array of 2 u16)
            auto* deactArr = GetArrayField(*motObj, "deactivationNumInactiveFrames");
            if (deactArr && deactArr->size() >= 2) {
                if (auto* v0 = std::get_if<int64_t>(&(*deactArr)[0].data))
                    rb.motion.deactivationNumInactiveFrames[0] = static_cast<uint16_t>(*v0);
                if (auto* v1 = std::get_if<int64_t>(&(*deactArr)[1].data))
                    rb.motion.deactivationNumInactiveFrames[1] = static_cast<uint16_t>(*v1);
            }

            // motionState (hkMotionState — inline struct)
            int msRef = GetObjectRef(*motObj, "motionState");
            if (msRef > 0) {
                const auto* msObj = ResolveObject(msRef);
                if (msObj) {
                    // Transform: hkTransform stored as 4 column vectors (col0, col1, col2, translation)
                    // or as a single "transform" vec16 field
                    auto tVec = GetVecField(*msObj, "transform");
                    if (tVec.size() >= 16) {
                        rb.motion.motionState.transform.col0 = {tVec[0], tVec[1], tVec[2], tVec[3]};
                        rb.motion.motionState.transform.col1 = {tVec[4], tVec[5], tVec[6], tVec[7]};
                        rb.motion.motionState.transform.col2 = {tVec[8], tVec[9], tVec[10], tVec[11]};
                        rb.motion.motionState.transform.translation = {tVec[12], tVec[13], tVec[14], tVec[15]};
                        rb.position = rb.motion.motionState.transform.translation;
                    }

                    // SweptTransform: 5 hkVector4 fields
                    // In tagged format, sweptTransform is an inline struct
                    int stRef = GetObjectRef(*msObj, "sweptTransform");
                    if (stRef > 0) {
                        const auto* stObj = ResolveObject(stRef);
                        if (stObj) {
                            auto com0 = GetVecField(*stObj, "centerOfMass0");
                            auto com1 = GetVecField(*stObj, "centerOfMass1");
                            auto rot0 = GetVecField(*stObj, "rotation0");
                            auto rot1 = GetVecField(*stObj, "rotation1");
                            auto comL = GetVecField(*stObj, "centerOfMassLocal");
                            if (com0.size() >= 4) rb.motion.motionState.centerOfMass0 = {com0[0], com0[1], com0[2], com0[3]};
                            if (com1.size() >= 4) rb.motion.motionState.centerOfMass1 = {com1[0], com1[1], com1[2], com1[3]};
                            if (rot0.size() >= 4) rb.motion.motionState.rotation0 = {rot0[0], rot0[1], rot0[2], rot0[3]};
                            if (rot1.size() >= 4) {
                                rb.motion.motionState.rotation1 = {rot1[0], rot1[1], rot1[2], rot1[3]};
                                rb.rotation = rb.motion.motionState.rotation1;
                            }
                            if (comL.size() >= 4) rb.motion.motionState.centerOfMassLocal = {comL[0], comL[1], comL[2], comL[3]};
                        }
                    }

                    // MotionState scalar fields
                    auto da = GetVecField(*msObj, "deltaAngle");
                    if (da.size() >= 4) rb.motion.motionState.deltaAngle = {da[0], da[1], da[2], da[3]};
                    rb.motion.motionState.objectRadius = GetFloatField(*msObj, "objectRadius", 0.0f);
                    rb.motion.motionState.linearDamping = GetFloatField(*msObj, "linearDamping", 0.0f);
                    rb.motion.motionState.angularDamping = GetFloatField(*msObj, "angularDamping", 0.0f);
                    rb.motion.motionState.maxLinearVelocity = GetFloatField(*msObj, "maxLinearVelocity", 0.0f);
                    rb.motion.motionState.maxAngularVelocity = GetFloatField(*msObj, "maxAngularVelocity", 0.0f);
                    rb.motion.motionState.deactivationClass = static_cast<uint8_t>(GetIntField(*msObj, "deactivationClass", 0));
                }
            }

            // Inertia and mass (vec4: .xyz = inverse inertia diagonal, .w = inverse mass)
            auto iamInv = GetVecField(*motObj, "inertiaAndMassInv");
            if (iamInv.size() >= 4) {
                rb.motion.inertiaAndMassInv = {iamInv[0], iamInv[1], iamInv[2], iamInv[3]};
                if (iamInv[3] != 0.0f) {
                    rb.mass = 1.0f / iamInv[3];
                }
            }

            // Velocities
            auto lv = GetVecField(*motObj, "linearVelocity");
            if (lv.size() >= 4) rb.motion.linearVelocity = {lv[0], lv[1], lv[2], lv[3]};
            auto av = GetVecField(*motObj, "angularVelocity");
            if (av.size() >= 4) rb.motion.angularVelocity = {av[0], av[1], av[2], av[3]};

            // Deactivation reference positions
            auto* drpArr = GetArrayField(*motObj, "deactivationRefPosition");
            if (drpArr && drpArr->size() >= 2) {
                for (int d = 0; d < 2 && d < static_cast<int>(drpArr->size()); d++) {
                    if (auto* v = std::get_if<std::vector<float>>(&(*drpArr)[d].data)) {
                        if (v->size() >= 4)
                            rb.motion.deactivationRefPosition[d] = {(*v)[0], (*v)[1], (*v)[2], (*v)[3]};
                    }
                }
            }

            // Gravity factor
            rb.motion.gravityFactor = GetFloatField(*motObj, "gravityFactor", 1.0f);
        }
    }

    // --- Collidable (hkpWorldObject.collidable — inline struct) ---
    // Contains the shape reference and broadPhaseHandle with collision filter info.

    int collidableRef = GetObjectRef(obj, "collidable");
    if (collidableRef > 0) {
        const auto* colObj = ResolveObject(collidableRef);
        if (colObj) {
            // hkpCdBody.shape is an object reference to a shape
            int shapeRef = GetObjectRef(*colObj, "shape");
            if (shapeRef > 0) {
                const auto* shapeObj = ResolveObject(shapeRef);
                if (shapeObj) {
                    rb.shape = ConvertShape(*shapeObj);
                }
            }

            // allowedPenetrationDepth from collidable
            rb.allowedPenetrationDepth = GetFloatField(*colObj, "allowedPenetrationDepth", 0.0f);

            // qualityType from collidable
            rb.qualityType = static_cast<int8_t>(GetIntField(*colObj, "qualityType", 0));

            // Collision filter info from broadPhaseHandle inline struct
            int bphRef = GetObjectRef(*colObj, "broadPhaseHandle");
            if (bphRef > 0) {
                const auto* bphObj = ResolveObject(bphRef);
                if (bphObj) {
                    rb.collisionFilterInfo = static_cast<uint32_t>(GetIntField(*bphObj, "collisionFilterInfo", 0));
                }
            }

            // Also try collisionFilterInfo directly on collidable (some tagged versions)
            if (rb.collisionFilterInfo == 0) {
                rb.collisionFilterInfo = static_cast<uint32_t>(GetIntField(*colObj, "collisionFilterInfo", 0));
            }
        }
    }

    // Also try direct shape reference (some tagged format layouts)
    if (rb.shape.type == ShapeType::Unknown) {
        int directShape = GetObjectRef(obj, "shape");
        if (directShape > 0) {
            const auto* shapeObj = ResolveObject(directShape);
            if (shapeObj) rb.shape = ConvertShape(*shapeObj);
        }
    }

    // Fallback: if RB has no shape yet, search all collidable objects for shapes.
    // This handles the case where tagged format serialization separates the
    // collidable from the rigid body without explicit references between them.
    if (rb.shape.type == ShapeType::Unknown) {
        for (const auto& other : m_Objects) {
            if (other.typeName == "hkpLinkedCollidable" || other.typeName == "hkpCollidable") {
                int shapeRef = GetObjectRef(other, "shape");
                if (shapeRef > 0) {
                    const auto* shapeObj = ResolveObject(shapeRef);
                    if (shapeObj && shapeObj->typeName.find("Shape") != std::string::npos) {
                        rb.shape = ConvertShape(*shapeObj);
                        if (rb.shape.type != ShapeType::Unknown) break;
                    }
                }
            }
        }
    }

    return rb;
}

// --- Physics system / data conversion ---

PhysicsSystemInfo HkxTaggedReader::ConvertPhysicsSystem(const TaggedObject& obj) {
    PhysicsSystemInfo sys;
    sys.name = GetStringField(obj, "name");
    sys.active = GetIntField(obj, "active", 1) != 0;

    auto* rbArr = GetArrayField(obj, "rigidBodies");
    if (rbArr) {
        for (const auto& v : *rbArr) {
            if (auto* ref = std::get_if<int>(&v.data)) {
                const auto* o = ResolveObject(*ref);
                if (o) sys.rigidBodies.push_back(ConvertRigidBody(*o));
            }
        }
    }
    return sys;
}

PhysicsDataInfo HkxTaggedReader::ConvertPhysicsData(const TaggedObject& obj) {
    PhysicsDataInfo data;
    auto* sysArr = GetArrayField(obj, "systems");
    if (sysArr) {
        for (const auto& v : *sysArr) {
            if (auto* ref = std::get_if<int>(&v.data)) {
                const auto* o = ResolveObject(*ref);
                if (o) data.systems.push_back(ConvertPhysicsSystem(*o));
            }
        }
    }
    return data;
}

// --- Scene conversion ---

SceneMesh HkxTaggedReader::ConvertMeshSection(const TaggedObject& meshSectionObj) {
    SceneMesh mesh;

    // Get vertex buffer -> data struct -> vectorData / floatData
    //
    // hkxVertexBuffer layout (from Havok tagged binary):
    //   data: hkxVertexBufferVertexData (inline struct)
    //     vectorData: array of hkVector4 — positions and normals interleaved
    //     floatData:  array of float     — UV coordinates and other per-vertex floats
    //     numVerts:   int                — vertex count
    //     vectorStride: int              — bytes per vertex in vectorData (e.g. 32 = 2 vec4s)
    //     floatStride:  int              — floats per vertex in floatData (e.g. 8)
    //
    // vectorData stores N * (vectorStride/16) vec4 entries. With vectorStride=32
    // that's 2 vec4s per vertex: [position, normal, position, normal, ...].
    // Positions have w=1.0, normals have w=0.0 (but we use stride, not w, to index).
    //
    // floatData stores per-vertex scalar data (UVs etc), NOT positions.
    int vbRef = GetObjectRef(meshSectionObj, "vertexBuffer");
    if (vbRef > 0) {
        const auto* vbObj = ResolveObject(vbRef);
        if (vbObj) {
            const TaggedObject* dataObj = nullptr;

            // hkxVertexBuffer.data is an inline struct (stored as object ref in tagged format)
            int dataRef = GetObjectRef(*vbObj, "data");
            if (dataRef > 0) dataObj = ResolveObject(dataRef);

            // Fallback: fields may be directly on the vertex buffer object
            if (!dataObj) dataObj = vbObj;

            auto* vecData = GetArrayField(*dataObj, "vectorData");
            auto* vecBytes = GetField<std::vector<uint8_t>>(*dataObj, "vectorData");

            int numVerts = static_cast<int>(GetIntField(*dataObj, "numVerts", 0));
            if (numVerts == 0) numVerts = static_cast<int>(GetIntField(*dataObj, "numVertices", 0));
            int vecStride = static_cast<int>(GetIntField(*dataObj, "vectorStride", 0));

            // vectorData is the primary source for positions and normals.
            // Each vertex occupies (vectorStride / 16) vec4 entries. The first
            // vec4 of each vertex is the position; the second (if present) is the normal.
            if (vecData && !vecData->empty()) {
                // vec4s per vertex: vectorStride is in bytes, each vec4 is 16 bytes
                int vecsPerVertex = (vecStride > 0) ? (vecStride / 16) : 1;
                if (vecsPerVertex < 1) vecsPerVertex = 1;

                if (numVerts == 0 && vecsPerVertex > 0) {
                    numVerts = static_cast<int>(vecData->size()) / vecsPerVertex;
                }

                for (int i = 0; i < numVerts; i++) {
                    size_t posIdx = static_cast<size_t>(i) * vecsPerVertex;
                    if (posIdx >= vecData->size()) break;
                    auto* posVec = std::get_if<std::vector<float>>(&(*vecData)[posIdx].data);
                    if (posVec && posVec->size() >= 3) {
                        mesh.vertices.push_back({(*posVec)[0], (*posVec)[1], (*posVec)[2], 0});
                    }

                    // Extract normals from the second vec4 per vertex (if stride allows)
                    if (vecsPerVertex >= 2) {
                        size_t nrmIdx = posIdx + 1;
                        if (nrmIdx < vecData->size()) {
                            auto* nrmVec = std::get_if<std::vector<float>>(&(*vecData)[nrmIdx].data);
                            if (nrmVec && nrmVec->size() >= 3) {
                                mesh.normals.push_back({(*nrmVec)[0], (*nrmVec)[1], (*nrmVec)[2], 0});
                            }
                        }
                    }
                }
            } else if (vecBytes && !vecBytes->empty()) {
                // Raw byte array of vec4 data (less common path)
                int bytesPerVertex = (vecStride > 0) ? vecStride : 16;
                if (numVerts == 0 && bytesPerVertex > 0)
                    numVerts = static_cast<int>(vecBytes->size()) / bytesPerVertex;
                for (int i = 0; i < numVerts; i++) {
                    size_t off = static_cast<size_t>(i) * bytesPerVertex;
                    if (off + 12 > vecBytes->size()) break;
                    float x, y, z;
                    std::memcpy(&x, vecBytes->data() + off, 4);
                    std::memcpy(&y, vecBytes->data() + off + 4, 4);
                    std::memcpy(&z, vecBytes->data() + off + 8, 4);
                    mesh.vertices.push_back({x, y, z, 0});
                }
            }
        }
    }

    // Get index buffers
    auto* ibArr = GetArrayField(meshSectionObj, "indexBuffers");
    if (ibArr) {
        for (const auto& ibVal : *ibArr) {
            if (auto* ref = std::get_if<int>(&ibVal.data)) {
                const auto* ibObj = ResolveObject(*ref);
                if (!ibObj) continue;

                // Try indices16 first, then indices32
                auto* idx16 = GetArrayField(*ibObj, "indices16");
                auto* idx32 = GetArrayField(*ibObj, "indices32");

                if (idx16 && idx16->size() >= 3) {
                    for (size_t i = 0; i + 2 < idx16->size(); i += 3) {
                        uint32_t a = 0, b = 0, c = 0;
                        if (auto* v = std::get_if<int64_t>(&(*idx16)[i].data)) a = static_cast<uint32_t>(*v);
                        if (auto* v = std::get_if<int64_t>(&(*idx16)[i+1].data)) b = static_cast<uint32_t>(*v);
                        if (auto* v = std::get_if<int64_t>(&(*idx16)[i+2].data)) c = static_cast<uint32_t>(*v);
                        mesh.triangles.push_back({a, b, c});
                    }
                } else if (idx32 && idx32->size() >= 3) {
                    for (size_t i = 0; i + 2 < idx32->size(); i += 3) {
                        uint32_t a = 0, b = 0, c = 0;
                        if (auto* v = std::get_if<int64_t>(&(*idx32)[i].data)) a = static_cast<uint32_t>(*v);
                        if (auto* v = std::get_if<int64_t>(&(*idx32)[i+1].data)) b = static_cast<uint32_t>(*v);
                        if (auto* v = std::get_if<int64_t>(&(*idx32)[i+2].data)) c = static_cast<uint32_t>(*v);
                        mesh.triangles.push_back({a, b, c});
                    }
                }
            }
        }
    }

    return mesh;
}

SceneNode HkxTaggedReader::ConvertNode(const TaggedObject& nodeObj, SceneInfo& scene) {
    SceneNode node;
    node.name = GetStringField(nodeObj, "name");

    // Check if this node has a mesh (object ref)
    int meshRef = GetObjectRef(nodeObj, "object");
    if (meshRef > 0) {
        const auto* meshObj = ResolveObject(meshRef);
        if (meshObj && meshObj->typeName == "hkxMesh") {
            // Extract mesh sections
            auto* sections = GetArrayField(*meshObj, "sections");
            if (sections) {
                for (const auto& secVal : *sections) {
                    if (auto* ref = std::get_if<int>(&secVal.data)) {
                        const auto* secObj = ResolveObject(*ref);
                        if (secObj) {
                            SceneMesh m = ConvertMeshSection(*secObj);
                            if (!m.vertices.empty()) {
                                node.meshIndex = static_cast<int>(scene.meshes.size());
                                scene.meshes.push_back(std::move(m));
                            }
                        }
                    }
                }
            }
        }
    }

    // Process children
    auto* children = GetArrayField(nodeObj, "children");
    if (children) {
        for (const auto& childVal : *children) {
            if (auto* ref = std::get_if<int>(&childVal.data)) {
                const auto* childObj = ResolveObject(*ref);
                if (childObj && childObj->typeName == "hkxNode") {
                    int childIdx = static_cast<int>(scene.nodes.size());
                    scene.nodes.push_back({}); // placeholder
                    scene.nodes[childIdx] = ConvertNode(*childObj, scene);
                    node.childIndices.push_back(childIdx);
                }
            }
        }
    }

    return node;
}

void HkxTaggedReader::ConvertScenes(ParseResult& result) {
    for (const auto& obj : m_Objects) {
        if (obj.typeName != "hkxScene") continue;

        SceneInfo scene;
        scene.modeller = GetStringField(obj, "modeller");

        // Get root node
        int rootRef = GetObjectRef(obj, "rootNode");
        if (rootRef > 0) {
            const auto* rootObj = ResolveObject(rootRef);
            if (rootObj && rootObj->typeName == "hkxNode") {
                scene.rootNodeIndex = static_cast<int>(scene.nodes.size());
                scene.nodes.push_back({});
                scene.nodes[scene.rootNodeIndex] = ConvertNode(*rootObj, scene);
            }
        }

        // Fallback: if the node tree did not produce any meshes (e.g. the
        // hkxScene has no rootNode or nodes lack object references), extract
        // meshes directly from all hkxMeshSection objects in the file.
        if (scene.meshes.empty()) {
            for (const auto& meshObj : m_Objects) {
                if (meshObj.typeName != "hkxMeshSection") continue;
                SceneMesh m = ConvertMeshSection(meshObj);
                if (!m.vertices.empty() && !m.triangles.empty()) {
                    scene.meshes.push_back(std::move(m));
                }
            }
        }

        if (!scene.meshes.empty() || !scene.nodes.empty()) {
            result.scenes.push_back(std::move(scene));
        }
    }
}

// --- Main conversion entry point ---

void HkxTaggedReader::ConvertToParseResult(ParseResult& result) {
    result.havokVersion = "(tagged binary format)";

    // Track which rigid body IDs came from hkpPhysicsSystem to avoid duplicates
    std::vector<int> systemRbIds;

    // Process shapes first so they're available for rigid body reconstruction
    for (const auto& obj : m_Objects) {
        if (obj.typeName.find("Shape") != std::string::npos && obj.typeName.find("hkp") == 0) {
            ShapeInfo s = ConvertShape(obj);
            if (s.type != ShapeType::Unknown)
                result.shapes.push_back(std::move(s));
        }
        result.objectsByClass[obj.typeName].push_back(static_cast<uint32_t>(obj.id));
    }

    for (const auto& obj : m_Objects) {
        if (obj.typeName == "hkpPhysicsData") {
            result.physicsData.push_back(ConvertPhysicsData(obj));
        } else if (obj.typeName == "hkpPhysicsSystem") {
            auto sys = ConvertPhysicsSystem(obj);
            auto* rbArr = GetArrayField(obj, "rigidBodies");
            if (rbArr) {
                for (const auto& v : *rbArr) {
                    if (auto* ref = std::get_if<int>(&v.data))
                        systemRbIds.push_back(*ref);
                }
            }
            result.physicsSystems.push_back(std::move(sys));
        } else if (obj.typeName == "hkpRigidBody" || obj.typeName == "hkpEntity") {
            auto rb = ConvertRigidBody(obj);

            // Validate: if the motion type is out of range (0-7), the rigid body
            // was parsed from a desynced stream position. In this case, reconstruct
            // the rigid body from individually-found correct physics objects.
            // If the normal conversion failed to find a shape, use the
            // first shape from result.shapes (already populated from the
            // shapes pass above).
            if (rb.shape.type == ShapeType::Unknown && !result.shapes.empty()) {
                rb.shape = result.shapes[0];
            }

            if (static_cast<int>(rb.motion.type) > 7) {
                RigidBodyInfo fixedRb;

                // Find the best motion object: look for hkpFixedRigidMotion or
                // any hkpMotion-derived object with a valid type field.
                for (const auto& other : m_Objects) {
                    if (other.typeName.find("Motion") == std::string::npos) continue;
                    if (other.typeName.find("hkp") != 0 && other.typeName.find("hk") != 0) continue;
                    auto typeIt = other.fields.find("type");
                    if (typeIt == other.fields.end()) continue;
                    auto* tv = std::get_if<int64_t>(&typeIt->second.data);
                    if (!tv || *tv < 0 || *tv > 7) continue;

                    // Valid motion found. Extract data.
                    fixedRb.motion.type = static_cast<MotionType>(*tv);
                    fixedRb.motion.deactivationIntegrateCounter = static_cast<uint8_t>(GetIntField(other, "deactivationIntegrateCounter", 0));
                    fixedRb.motion.gravityFactor = GetFloatField(other, "gravityFactor", 1.0f);

                    auto iamInv = GetVecField(other, "inertiaAndMassInv");
                    if (iamInv.size() >= 4) {
                        fixedRb.motion.inertiaAndMassInv = {iamInv[0], iamInv[1], iamInv[2], iamInv[3]};
                        if (iamInv[3] != 0.0f) fixedRb.mass = 1.0f / iamInv[3];
                    }
                    auto lv = GetVecField(other, "linearVelocity");
                    if (lv.size() >= 4) fixedRb.motion.linearVelocity = {lv[0], lv[1], lv[2], lv[3]};
                    auto av = GetVecField(other, "angularVelocity");
                    if (av.size() >= 4) fixedRb.motion.angularVelocity = {av[0], av[1], av[2], av[3]};

                    // Motion state
                    int msRef = GetObjectRef(other, "motionState");
                    if (msRef > 0) {
                        const auto* msObj = ResolveObject(msRef);
                        if (msObj) {
                            auto tVec = GetVecField(*msObj, "transform");
                            if (tVec.size() >= 16) {
                                fixedRb.motion.motionState.transform.col0 = {tVec[0], tVec[1], tVec[2], tVec[3]};
                                fixedRb.motion.motionState.transform.col1 = {tVec[4], tVec[5], tVec[6], tVec[7]};
                                fixedRb.motion.motionState.transform.col2 = {tVec[8], tVec[9], tVec[10], tVec[11]};
                                fixedRb.motion.motionState.transform.translation = {tVec[12], tVec[13], tVec[14], tVec[15]};
                                fixedRb.position = fixedRb.motion.motionState.transform.translation;
                            }
                        }
                    }
                    break;
                }

                // Find the best material: look for hkpMaterial with valid friction
                for (const auto& other : m_Objects) {
                    if (other.typeName != "hkpMaterial") continue;
                    auto fricIt = other.fields.find("friction");
                    if (fricIt == other.fields.end()) continue;
                    fixedRb.material.friction = GetFloatField(other, "friction", 0.5f);
                    fixedRb.material.restitution = GetFloatField(other, "restitution", 0.4f);
                    fixedRb.material.responseType = static_cast<int8_t>(GetIntField(other, "responseType", 0));
                    fixedRb.friction = fixedRb.material.friction;
                    fixedRb.restitution = fixedRb.material.restitution;
                    break;
                }

                // Find the best collision filter info from hkpTypedBroadPhaseHandle
                for (const auto& other : m_Objects) {
                    if (other.typeName != "hkpTypedBroadPhaseHandle") continue;
                    fixedRb.collisionFilterInfo = static_cast<uint32_t>(GetIntField(other, "collisionFilterInfo", 0));
                    break;
                }

                // Use shapes already extracted in the first pass.
                // Pick the first valid shape from result.shapes.
                if (!result.shapes.empty()) {
                    fixedRb.shape = result.shapes[0];
                }

                result.rigidBodies.push_back(std::move(fixedRb));
            } else {
                result.rigidBodies.push_back(std::move(rb));
            }
        }

    }

    // Also populate rigidBodies in physicsSystems from the reconstructed data
    for (auto& sys : result.physicsSystems) {
        if (sys.rigidBodies.empty() && !result.rigidBodies.empty()) {
            sys.rigidBodies = result.rigidBodies;
        }
    }

    // Extract scene data
    ConvertScenes(result);

    for (const auto& type : m_Types) {
        if (!type.name.empty()) {
            ClassEntry e; e.name = type.name; e.signature = 0;
            result.classEntries.push_back(e);
        }
    }
}

} // namespace Hkx
