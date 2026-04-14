// HkxXmlReader.cpp — Parse Havok XML packfile into ParseResult.

#include "havok/reader/hkx_xml_reader.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <set>
#include <unordered_map>

namespace Hkx {

// ---------------------------------------------------------------------------
// Local parse context and helpers
// ---------------------------------------------------------------------------

struct XmlParseCtx {
    std::unordered_map<std::string, pugi::xml_node> objects;
    ParseResult result;
};

static Vector4 parseVec4(const std::string& str) {
    Vector4 v{};
    std::string s = str;
    for (auto& c : s) if (c == '(' || c == ')') c = ' ';
    std::istringstream ss(s);
    ss >> v.x >> v.y >> v.z >> v.w;
    return v;
}

static Transform parseKeyFrame(const std::string& str) {
    Transform t;
    std::vector<Vector4> vecs;
    size_t pos = 0;
    while (pos < str.size() && vecs.size() < 4) {
        size_t open = str.find('(', pos);
        size_t close = str.find(')', open);
        if (open == std::string::npos || close == std::string::npos) break;
        vecs.push_back(parseVec4(str.substr(open, close - open + 1)));
        pos = close + 1;
    }
    if (vecs.size() >= 4) {
        t.col0 = vecs[0]; t.col1 = vecs[1]; t.col2 = vecs[2]; t.translation = vecs[3];
    }
    return t;
}

static std::vector<float> parseFloats(const std::string& str) {
    std::vector<float> out;
    std::string s = str;
    for (auto& c : s) if (c == '(' || c == ')') c = ' ';
    std::istringstream ss(s);
    float v; while (ss >> v) out.push_back(v);
    return out;
}

static std::vector<int> parseInts(const std::string& str) {
    std::vector<int> out;
    std::istringstream ss(str);
    int v; while (ss >> v) out.push_back(v);
    return out;
}

static std::vector<std::string> parseRefs(const pugi::xml_node& param) {
    std::vector<std::string> refs;
    std::istringstream ss(param.text().as_string());
    std::string tok;
    while (ss >> tok) if (!tok.empty() && tok[0] == '#') refs.push_back(tok);
    return refs;
}

static std::string getParam(const pugi::xml_node& obj, const char* name) {
    for (auto p = obj.child("hkparam"); p; p = p.next_sibling("hkparam"))
        if (std::strcmp(p.attribute("name").as_string(), name) == 0)
            return p.text().as_string();
    return "";
}

static pugi::xml_node findParam(const pugi::xml_node& obj, const char* name) {
    for (auto p = obj.child("hkparam"); p; p = p.next_sibling("hkparam"))
        if (std::strcmp(p.attribute("name").as_string(), name) == 0) return p;
    return {};
}

// Recursively find a named param in nested hkobject sub-params.
static pugi::xml_node findNestedParam(const pugi::xml_node& obj, const char* name) {
    for (auto p = obj.child("hkparam"); p; p = p.next_sibling("hkparam")) {
        if (std::strcmp(p.attribute("name").as_string(), name) == 0) return p;
        for (auto sub = p.child("hkobject"); sub; sub = sub.next_sibling("hkobject")) {
            auto found = findNestedParam(sub, name);
            if (found) return found;
        }
    }
    return {};
}

static const pugi::xml_node* resolve(XmlParseCtx& ctx, const std::string& ref) {
    if (ref.empty() || ref == "null") return nullptr;
    auto it = ctx.objects.find(ref);
    return (it != ctx.objects.end()) ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Converters
// ---------------------------------------------------------------------------

static ShapeInfo convertShape(XmlParseCtx& ctx, const pugi::xml_node& obj);

static ShapeInfo convertShape(XmlParseCtx& ctx, const pugi::xml_node& obj) {
    ShapeInfo shape;
    std::string cls = obj.attribute("class").as_string();
    shape.className = cls;

    if (cls.find("MoppBvTree") != std::string::npos) {
        shape.type = ShapeType::Mopp;
        // child is an inline hkpSingleShapeContainer with childShape ref
        auto childShapeParam = findNestedParam(obj, "childShape");
        if (childShapeParam) {
            auto* child = resolve(ctx, childShapeParam.text().as_string());
            if (child) shape.children.push_back(convertShape(ctx, *child));
        }
    } else if (cls.find("ListShape") != std::string::npos) {
        shape.type = ShapeType::List;
        auto param = findParam(obj, "childShapes");
        if (param) {
            for (auto c = param.child("hkobject"); c; c = c.next_sibling("hkobject")) {
                auto* sn = resolve(ctx, getParam(c, "shape"));
                if (sn) shape.children.push_back(convertShape(ctx, *sn));
            }
        }
    } else if (cls.find("ConvexVerticesShape") != std::string::npos) {
        shape.type = ShapeType::ConvexVertices;
        shape.radius = std::atof(getParam(obj, "radius").c_str());
        // rotatedVertices are nested hkobject children with x, y, z params
        auto rvParam = findParam(obj, "rotatedVertices");
        if (rvParam) {
            for (auto sub = rvParam.child("hkobject"); sub; sub = sub.next_sibling("hkobject")) {
                FourTransposedPoints ftp;
                ftp.xs = parseVec4(getParam(sub, "x"));
                ftp.ys = parseVec4(getParam(sub, "y"));
                ftp.zs = parseVec4(getParam(sub, "z"));
                shape.rotatedVertices.push_back(ftp);
            }
        }
        shape.numVertices = std::atoi(getParam(obj, "numVertices").c_str());
        auto pf = parseFloats(getParam(obj, "planeEquations"));
        for (size_t i = 0; i + 3 < pf.size(); i += 4)
            shape.planeEquations.push_back({pf[i], pf[i+1], pf[i+2], pf[i+3]});
    } else if (cls.find("BoxShape") != std::string::npos) {
        shape.type = ShapeType::Box;
        shape.halfExtents = parseVec4(getParam(obj, "halfExtents"));
    } else if (cls.find("SphereShape") != std::string::npos) {
        shape.type = ShapeType::Sphere;
        shape.radius = std::atof(getParam(obj, "radius").c_str());
    } else if (cls.find("CapsuleShape") != std::string::npos) {
        shape.type = ShapeType::Capsule;
        shape.vertexA = parseVec4(getParam(obj, "vertexA"));
        shape.vertexB = parseVec4(getParam(obj, "vertexB"));
        shape.radius = std::atof(getParam(obj, "radius").c_str());
    } else if (cls.find("CylinderShape") != std::string::npos) {
        shape.type = ShapeType::Cylinder;
        shape.vertexA = parseVec4(getParam(obj, "vertexA"));
        shape.vertexB = parseVec4(getParam(obj, "vertexB"));
        shape.radius = std::atof(getParam(obj, "radius").c_str());
        shape.cylRadius = std::atof(getParam(obj, "cylRadius").c_str());
    } else if (cls.find("StorageExtendedMesh") != std::string::npos && cls.find("Subpart") == std::string::npos) {
        shape.type = ShapeType::ExtendedMesh;
        auto param = findParam(obj, "meshstorage");
        if (param) {
            for (auto& ref : parseRefs(param)) {
                auto* sn = resolve(ctx, ref);
                if (!sn) continue;
                // Track base vertex offset for this storage's local indices
                uint32_t vertBase = static_cast<uint32_t>(shape.planeEquations.size());
                auto verts = parseFloats(getParam(*sn, "vertices"));
                auto indices = parseInts(getParam(*sn, "indices32"));
                if (indices.empty()) indices = parseInts(getParam(*sn, "indices16"));
                for (size_t i = 0; i + 3 < verts.size(); i += 4)
                    shape.planeEquations.push_back({verts[i], verts[i+1], verts[i+2], verts[i+3]});
                // Indices are groups of 4: (v0, v1, v2, material) — offset by vertBase
                for (size_t i = 0; i + 3 < indices.size(); i += 4) {
                    ShapeInfo::Triangle tri;
                    tri.a = vertBase + indices[i];
                    tri.b = vertBase + indices[i+1];
                    tri.c = vertBase + indices[i+2];
                    shape.triangles.push_back(tri);
                }
            }
        }
        shape.numVertices = static_cast<int>(shape.planeEquations.size());
        shape.numTriangles = static_cast<int>(shape.triangles.size());
    } else if (cls.find("TransformShape") != std::string::npos) {
        shape.type = ShapeType::Transform;
        for (auto p = obj.child("hkparam"); p; p = p.next_sibling("hkparam")) {
            std::string pn = p.attribute("name").as_string();
            if (pn == "rotation") shape.childTransform = parseKeyFrame(p.text().as_string());
            else if (pn == "childShape") {
                auto* cn = resolve(ctx, p.text().as_string());
                if (cn) shape.children.push_back(convertShape(ctx, *cn));
            }
        }
    }
    return shape;
}

static RigidBodyInfo convertRigidBody(XmlParseCtx& ctx, const pugi::xml_node& obj) {
    RigidBodyInfo rb;

    // Shape is inside collidable sub-object: collidable/shape = "#ref"
    auto shapeParam = findNestedParam(obj, "shape");
    if (shapeParam) {
        auto* sn = resolve(ctx, shapeParam.text().as_string());
        if (sn) rb.shape = convertShape(ctx, *sn);
    }

    rb.shape.className = rb.shape.className.empty() ? getParam(obj, "name") : rb.shape.className;

    // Motion state transform: motion(param) → hkobject → motionState(param) → hkobject → transform(param)
    auto motionParam = findParam(obj, "motion");
    if (motionParam) {
        // The motion param contains an inline hkobject — search within it
        for (auto motionObj = motionParam.child("hkobject"); motionObj; motionObj = motionObj.next_sibling("hkobject")) {
            auto msParam = findParam(motionObj, "motionState");
            if (msParam) {
                for (auto msObj = msParam.child("hkobject"); msObj; msObj = msObj.next_sibling("hkobject")) {
                    auto transformParam = findParam(msObj, "transform");
                    if (transformParam) {
                        std::string tStr = transformParam.text().as_string();
                        if (!tStr.empty()) {
                            rb.motion.motionState.transform = parseKeyFrame(tStr);
                            rb.position = rb.motion.motionState.transform.translation;
                        }
                    }
                }
            }
        }
    }

    return rb;
}

static SceneNode convertNode(XmlParseCtx& ctx, const pugi::xml_node& obj, SceneInfo& scene) {
    SceneNode node;
    node.name = getParam(obj, "name");

    auto kfParam = findParam(obj, "keyFrames");
    if (kfParam && kfParam.attribute("numelements").as_int() > 0)
        node.transform = parseKeyFrame(kfParam.text().as_string());

    auto* meshObj = resolve(ctx, getParam(obj, "object"));
    if (meshObj && std::string(meshObj->attribute("class").as_string()).find("hkxMesh") != std::string::npos) {
        auto secParam = findParam(*meshObj, "sections");
        if (secParam) {
            for (auto& ref : parseRefs(secParam)) {
                auto* secNode = resolve(ctx, ref);
                if (!secNode) continue;
                auto* vbNode = resolve(ctx, getParam(*secNode, "vertexBuffer"));
                if (!vbNode) continue;

                SceneMesh mesh;
                // vectorData and numVerts are inside the nested "data" sub-object
                auto dataParam = findNestedParam(*vbNode, "vectorData");
                auto vf = dataParam ? parseFloats(dataParam.text().as_string()) : std::vector<float>{};
                auto numVertsParam = findNestedParam(*vbNode, "numVerts");
                int nv = numVertsParam ? std::atoi(numVertsParam.text().as_string()) : 0;
                if (nv <= 0) nv = static_cast<int>(vf.size()) / 4;
                int vpv = (nv > 0) ? static_cast<int>(vf.size()) / (nv * 4) : 1;
                if (vpv < 1) vpv = 1;
                for (int v = 0; v < nv && v * vpv * 4 + 3 < static_cast<int>(vf.size()); v++) {
                    int b = v * vpv * 4;
                    mesh.vertices.push_back({vf[b], vf[b+1], vf[b+2], vf[b+3]});
                    if (vpv >= 2) mesh.normals.push_back({vf[b+4], vf[b+5], vf[b+6], vf[b+7]});
                }

                auto ibParam = findParam(*secNode, "indexBuffers");
                if (ibParam) {
                    for (auto& ibRef : parseRefs(ibParam)) {
                        auto* ibNode = resolve(ctx, ibRef);
                        if (!ibNode) continue;
                        auto i16 = parseInts(getParam(*ibNode, "indices16"));
                        auto i32 = parseInts(getParam(*ibNode, "indices32"));
                        auto& arr = i32.empty() ? i16 : i32;
                        for (size_t i = 0; i + 2 < arr.size(); i += 3)
                            mesh.triangles.push_back({static_cast<uint32_t>(arr[i]), static_cast<uint32_t>(arr[i+1]), static_cast<uint32_t>(arr[i+2])});
                    }
                }
                if (!mesh.vertices.empty()) {
                    node.meshIndex = static_cast<int>(scene.meshes.size());
                    scene.meshes.push_back(std::move(mesh));
                }
            }
        }
    }

    auto childParam = findParam(obj, "children");
    if (childParam) {
        for (auto& ref : parseRefs(childParam)) {
            auto* cn = resolve(ctx, ref);
            if (cn && std::string(cn->attribute("class").as_string()).find("hkxNode") != std::string::npos) {
                int ci = static_cast<int>(scene.nodes.size());
                scene.nodes.push_back({});
                scene.nodes[ci] = convertNode(ctx, *cn, scene);
                node.childIndices.push_back(ci);
            }
        }
    }
    return node;
}

static SceneInfo convertScene(XmlParseCtx& ctx, const pugi::xml_node& obj) {
    SceneInfo scene;
    scene.modeller = getParam(obj, "modeller");
    auto* rn = resolve(ctx, getParam(obj, "rootNode"));
    if (rn) {
        scene.rootNodeIndex = static_cast<int>(scene.nodes.size());
        scene.nodes.push_back({});
        scene.nodes[scene.rootNodeIndex] = convertNode(ctx, *rn, scene);
    }
    return scene;
}

static PhysicsSystemInfo convertPhysicsSystem(XmlParseCtx& ctx, const pugi::xml_node& obj) {
    PhysicsSystemInfo sys;
    auto param = findParam(obj, "rigidBodies");
    if (param) for (auto& ref : parseRefs(param)) {
        auto* rn = resolve(ctx, ref);
        if (rn) sys.rigidBodies.push_back(convertRigidBody(ctx, *rn));
    }
    return sys;
}

static PhysicsDataInfo convertPhysicsData(XmlParseCtx& ctx, const pugi::xml_node& obj) {
    PhysicsDataInfo pd;
    auto param = findParam(obj, "systems");
    if (param) for (auto& ref : parseRefs(param)) {
        auto* sn = resolve(ctx, ref);
        if (sn) pd.systems.push_back(convertPhysicsSystem(ctx, *sn));
    }
    return pd;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ParseResult HkxXmlReader::Parse(const std::string& xmlContent) {
    XmlParseCtx ctx;

    pugi::xml_document doc;
    auto pr = doc.load_string(xmlContent.c_str());
    if (!pr) {
        ctx.result.error = "XML parse error: " + std::string(pr.description());
        return ctx.result;
    }

    auto packfile = doc.child("hkpackfile");
    if (!packfile) {
        ctx.result.error = "Not an HKX XML packfile (missing <hkpackfile>)";
        return ctx.result;
    }

    ctx.result.havokVersion = packfile.attribute("contentsversion").as_string();
    ctx.result.fileVersion = packfile.attribute("classversion").as_int();

    // Index all objects from __data__ section
    for (auto section = packfile.child("hksection"); section; section = section.next_sibling("hksection")) {
        if (std::string(section.attribute("name").as_string()) == "__data__") {
            for (auto obj = section.child("hkobject"); obj; obj = obj.next_sibling("hkobject")) {
                std::string name = obj.attribute("name").as_string();
                if (!name.empty()) ctx.objects[name] = obj;
            }
        }
    }

    // Collect refs to physics systems owned by PhysicsData to avoid double-conversion
    std::set<std::string> ownedSystems;
    for (auto& [name, node] : ctx.objects) {
        if (std::string(node.attribute("class").as_string()) == "hkpPhysicsData") {
            auto param = findParam(node, "systems");
            if (param) for (auto& ref : parseRefs(param)) ownedSystems.insert(ref);
        }
    }

    // Convert known object types
    for (auto& [name, node] : ctx.objects) {
        std::string cls = node.attribute("class").as_string();
        if (cls == "hkpPhysicsData")
            ctx.result.physicsData.push_back(convertPhysicsData(ctx, node));
        else if (cls == "hkpPhysicsSystem" && !ownedSystems.count(name))
            ctx.result.physicsSystems.push_back(convertPhysicsSystem(ctx, node));
        else if (cls == "hkxScene")
            ctx.result.scenes.push_back(convertScene(ctx, node));
        else if (cls == "hkRootLevelContainer") {
            RootLevelContainerInfo rlc;
            auto param = findParam(node, "namedVariants");
            if (param) for (auto nv = param.child("hkobject"); nv; nv = nv.next_sibling("hkobject")) {
                NamedVariant v;
                v.name = getParam(nv, "name");
                v.className = getParam(nv, "className");
                rlc.namedVariants.push_back(v);
            }
            ctx.result.rootContainers.push_back(rlc);
        }
    }

    ctx.result.success = true;
    return ctx.result;
}

ParseResult HkxXmlReader::ParseFile(const std::string& filePath) {
    std::ifstream f(filePath);
    if (!f) {
        ParseResult r;
        r.error = "Cannot open file: " + filePath;
        return r;
    }
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return Parse(content);
}

} // namespace Hkx
