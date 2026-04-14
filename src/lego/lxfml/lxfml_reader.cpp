#include "lego/lxfml/lxfml_reader.h"

#include <pugixml.hpp>
#include <string>
#include <sstream>
#include <cstring>
#include <cmath>

namespace lu::assets {

namespace {

// Parse "f0,f1,...,f11" into N floats.
template<int N>
void parse_float_list(const char* str, float out[N]) {
    std::istringstream ss(str);
    for (int i = 0; i < N; ++i) {
        ss >> out[i];
        if (ss.peek() == ',') ss.ignore();
    }
}

// Parse "0,1,2" into a vector of ints.
std::vector<int32_t> parse_int_list(const char* str) {
    std::vector<int32_t> result;
    std::istringstream ss(str);
    int32_t v;
    while (ss >> v) {
        result.push_back(v);
        if (ss.peek() == ',') ss.ignore();
    }
    return result;
}

// Parse vec3 from "x,y,z" string into out[3].
void parse_vec3(const char* str, float out[3]) {
    parse_float_list<3>(str, out);
}

// ── Angle-axis → 4x3 matrix conversion ──────────────────────────────────────
// Builds rotation from angle (degrees) + axis, with direct (unrotated) translation.
// Layout: [n11,n12,n13, n21,n22,n23, n31,n32,n33, n41,n42,n43]
// Same convention as NexusDashboard's Matrix3D and lu-toolbox.

void angle_axis_to_transform(float angle_deg, float ax, float ay, float az,
                              float tx, float ty, float tz,
                              float out[12]) {
    float angle = angle_deg * 3.14159265358979323846f / 180.0f;
    float c = std::cos(angle);
    float s = std::sin(angle);
    float t = 1.0f - c;

    float tax = t * ax, tay = t * ay, taz = t * az;
    float sax = s * ax, say = s * ay, saz = s * az;

    out[0] = c + ax * tax;  out[1] = ay * tax + saz; out[2] = az * tax - say;
    out[3] = ax * tay - saz; out[4] = c + ay * tay;  out[5] = az * tay + sax;
    out[6] = ax * taz + say; out[7] = ay * taz - sax; out[8] = c + az * taz;

    out[9]  = tx;
    out[10] = ty;
    out[11] = tz;
}

// ── Parse helpers ────────────────────────────────────────────────────────────

LxfmlCamera parse_camera(const pugi::xml_node& node) {
    LxfmlCamera cam;
    cam.ref_id       = node.attribute("refID").as_int();
    cam.name         = node.attribute("name").as_string();
    cam.field_of_view = node.attribute("fieldOfView").as_float(80.0f);
    cam.distance     = node.attribute("distance").as_float(0.0f);

    auto transform_attr = node.attribute("transformation");
    if (transform_attr) {
        // Format B: 4x3 matrix as 12 comma-separated floats
        cam.has_transformation = true;
        parse_float_list<12>(transform_attr.as_string(), cam.transformation);
    } else {
        // Format A: separate angle-axis + look-at target
        cam.angle = node.attribute("angle").as_float(0.0f);
        cam.ax    = node.attribute("ax").as_float(0.0f);
        cam.ay    = node.attribute("ay").as_float(1.0f);
        cam.az    = node.attribute("az").as_float(0.0f);
        cam.tx    = node.attribute("tx").as_float(0.0f);
        cam.ty    = node.attribute("ty").as_float(0.0f);
        cam.tz    = node.attribute("tz").as_float(0.0f);
    }
    return cam;
}

LxfmlJoint parse_joint(const pugi::xml_node& node) {
    LxfmlJoint j;
    j.type = node.attribute("type").as_string();
    for (auto& child : node.children()) {
        std::string_view tag = child.name();
        if (tag == "RigidRef") {
            LxfmlRigidRef rr;
            rr.rigid_ref = child.attribute("rigidRef").as_int();
            parse_vec3(child.attribute("a").as_string("0,0,0"), rr.a);
            parse_vec3(child.attribute("z").as_string("0,0,1"), rr.z);
            parse_vec3(child.attribute("t").as_string("0,0,0"), rr.t);
            j.rigid_refs.push_back(std::move(rr));
        } else if (tag == "GroupRef") {
            LxfmlGroupRef gr;
            gr.group_ref = child.attribute("groupRef").as_int();
            gr.ax = child.attribute("ax").as_float(0.0f);
            gr.ay = child.attribute("ay").as_float(0.0f);
            gr.az = child.attribute("az").as_float(0.0f);
            gr.zx = child.attribute("zx").as_float(0.0f);
            gr.zy = child.attribute("zy").as_float(0.0f);
            gr.zz = child.attribute("zz").as_float(1.0f);
            gr.tx = child.attribute("tx").as_float(0.0f);
            gr.ty = child.attribute("ty").as_float(0.0f);
            gr.tz = child.attribute("tz").as_float(0.0f);
            j.group_refs.push_back(std::move(gr));
        }
    }
    return j;
}

LxfmlRigidSystem parse_rigid_system(const pugi::xml_node& node) {
    LxfmlRigidSystem rs;
    for (auto& child : node.children()) {
        std::string_view tag = child.name();
        if (tag == "Rigid") {
            LxfmlRigid r;
            r.ref_id = child.attribute("refID").as_int();
            auto t = child.attribute("transformation");
            if (t) parse_float_list<12>(t.as_string(), r.transformation);
            auto br = child.attribute("boneRefs");
            if (br) r.bone_refs = parse_int_list(br.as_string());
            rs.rigids.push_back(std::move(r));
        } else if (tag == "Joint") {
            rs.joints.push_back(parse_joint(child));
        }
    }
    return rs;
}

LxfmlSceneGroup parse_scene_group(const pugi::xml_node& node) {
    LxfmlSceneGroup g;
    g.ref_id = node.attribute("refID").as_int();
    g.name   = node.attribute("name").as_string();
    g.angle  = node.attribute("angle").as_float(0.0f);
    g.ax     = node.attribute("ax").as_float(0.0f);
    g.ay     = node.attribute("ay").as_float(1.0f);
    g.az     = node.attribute("az").as_float(0.0f);
    g.tx     = node.attribute("tx").as_float(0.0f);
    g.ty     = node.attribute("ty").as_float(0.0f);
    g.tz     = node.attribute("tz").as_float(0.0f);
    for (auto& child : node.children()) {
        std::string_view tag = child.name();
        if (tag == "Part") {
            LxfmlScenePart p;
            p.ref_id      = child.attribute("refID").as_int();
            p.name        = child.attribute("name").as_string();
            p.design_id   = child.attribute("designID").as_int();
            p.material_id = child.attribute("materialID").as_int();
            p.item_nos    = child.attribute("itemNos").as_string();
            p.angle       = child.attribute("angle").as_float(0.0f);
            p.ax          = child.attribute("ax").as_float(0.0f);
            p.ay          = child.attribute("ay").as_float(1.0f);
            p.az          = child.attribute("az").as_float(0.0f);
            p.tx          = child.attribute("tx").as_float(0.0f);
            p.ty          = child.attribute("ty").as_float(0.0f);
            p.tz          = child.attribute("tz").as_float(0.0f);
            g.parts.push_back(std::move(p));
        } else if (tag == "Joint") {
            g.joints.push_back(parse_joint(child));
        }
    }
    return g;
}

// ── Convert v4 Scene parts into unified bricks vector ────────────────────────
// Each ScenePart has its own absolute angle-axis transform (no group accumulation).
// Matches lu-toolbox: Scene Groups are parsed as Bricks, Parts get their own Bone.

void flatten_scene_group(const LxfmlSceneGroup& group,
                          std::vector<LxfmlBrick>& out) {
    for (const auto& sp : group.parts) {
        float xform[12];
        angle_axis_to_transform(sp.angle, sp.ax, sp.ay, sp.az,
                                        sp.tx, sp.ty, sp.tz, xform);

        LxfmlBrick brick;
        brick.ref_id = sp.ref_id;
        brick.design_id = sp.design_id;

        LxfmlPart part;
        part.ref_id = sp.ref_id;
        part.design_id = sp.design_id;
        part.materials = std::to_string(sp.material_id);

        LxfmlBone bone;
        bone.ref_id = 0;
        std::memcpy(bone.transform, xform, sizeof(xform));
        part.bones.push_back(std::move(bone));

        brick.parts.push_back(std::move(part));
        out.push_back(std::move(brick));
    }
}

// ── Convert v2 Models format into unified bricks vector ──────────────────────
// v2 uses <Models><Model><Group><Brick> with nested Groups.
// Each Brick has its own absolute angle-axis + position transform.
// Groups are organizational only — no transform accumulation.

void flatten_v2_node(const pugi::xml_node& node,
                      std::vector<LxfmlBrick>& out) {
    for (auto& child : node.children()) {
        std::string_view tag = child.name();
        if (tag == "Brick") {
            float bAngle = child.attribute("angle").as_float(0.0f);
            float bax = child.attribute("ax").as_float(0.0f);
            float bay = child.attribute("ay").as_float(0.0f);
            float baz = child.attribute("az").as_float(1.0f);
            float bx  = child.attribute("x").as_float(0.0f);
            float by  = child.attribute("y").as_float(0.0f);
            float bz  = child.attribute("z").as_float(0.0f);

            float xform[12];
            angle_axis_to_transform(bAngle, bax, bay, baz, bx, by, bz, xform);

            int brickID = child.attribute("brickID").as_int();
            int matID   = child.attribute("materialID").as_int(26);
            int refID   = child.attribute("brickRefID").as_int();

            LxfmlBrick brick;
            brick.ref_id = refID;
            brick.design_id = brickID;

            LxfmlPart part;
            part.ref_id = refID;
            part.design_id = brickID;
            part.materials = std::to_string(matID);

            LxfmlBone bone;
            bone.ref_id = 0;
            std::memcpy(bone.transform, xform, sizeof(xform));
            part.bones.push_back(std::move(bone));

            brick.parts.push_back(std::move(part));
            out.push_back(std::move(brick));
        } else if (tag == "Group") {
            flatten_v2_node(child, out);
        }
    }
}

} // anonymous namespace

LxfmlFile lxfml_parse(std::span<const uint8_t> data) {
    pugi::xml_document doc;
    auto result = doc.load_buffer(data.data(), data.size());
    if (!result) {
        throw LxfmlError("LXFML: XML parse error: " + std::string(result.description()));
    }

    auto root = doc.child("LXFML");
    if (!root) {
        throw LxfmlError("LXFML: missing root LXFML element");
    }

    LxfmlFile lxfml;

    // Handle both v4/v5 (versionMajor) and v2 (majorVersion) attribute names
    if (root.attribute("versionMajor")) {
        lxfml.version_major = root.attribute("versionMajor").as_int();
        lxfml.version_minor = root.attribute("versionMinor").as_int();
    } else if (root.attribute("majorVersion")) {
        lxfml.version_major = root.attribute("majorVersion").as_int();
        lxfml.version_minor = root.attribute("minorVersion").as_int();
    }
    lxfml.name = root.attribute("name").as_string();

    // <Meta>
    if (auto meta_node = root.child("Meta")) {
        if (auto app = meta_node.child("Application")) {
            lxfml.meta.application.name          = app.attribute("name").as_string();
            lxfml.meta.application.version_major = app.attribute("versionMajor").as_int();
            lxfml.meta.application.version_minor = app.attribute("versionMinor").as_int();
        }
        if (auto brand = meta_node.child("Brand")) {
            lxfml.meta.brand = brand.attribute("name").as_string();
        }
        if (auto bs = meta_node.child("BrickSet")) {
            lxfml.meta.brick_set_version = bs.attribute("version").as_string();
        }
    }

    // <Cameras>
    if (auto cams_node = root.child("Cameras")) {
        for (auto& cam_node : cams_node.children("Camera")) {
            lxfml.cameras.push_back(parse_camera(cam_node));
        }
    }

    // <Bricks> — v5 LU format with transformation matrices
    if (auto bricks_node = root.child("Bricks")) {
        lxfml.bricks_camera_ref = bricks_node.attribute("cameraRef").as_int(-1);
        for (auto brick_node : bricks_node.children("Brick")) {
            LxfmlBrick brick;
            brick.ref_id    = brick_node.attribute("refID").as_int();
            brick.design_id = brick_node.attribute("designID").as_int();

            for (auto part_node : brick_node.children("Part")) {
                LxfmlPart part;
                part.ref_id    = part_node.attribute("refID").as_int();
                part.design_id = part_node.attribute("designID").as_int();
                part.materials  = part_node.attribute("materials").as_string();
                part.decoration = part_node.attribute("decoration").as_string();

                for (auto bone_node : part_node.children("Bone")) {
                    LxfmlBone bone;
                    bone.ref_id = bone_node.attribute("refID").as_int();
                    auto t = bone_node.attribute("transformation");
                    if (t && t.as_string()[0] != '\0') {
                        parse_float_list<12>(t.as_string(), bone.transform);
                    }
                    part.bones.push_back(std::move(bone));
                }
                brick.parts.push_back(std::move(part));
            }
            lxfml.bricks.push_back(std::move(brick));
        }
    }

    // <Scene> — v4 LDD format with angle-axis transforms.
    // Convert to unified bricks vector so assembly code works for all formats.
    if (auto scene_node = root.child("Scene")) {
        lxfml.scene.camera_ref_id = scene_node.attribute("cameraRefID").as_int(-1);
        for (auto model_node : scene_node.children("Model")) {
            LxfmlSceneModel model;
            model.name = model_node.attribute("name").as_string();
            for (auto group_node : model_node.children("Group")) {
                model.groups.push_back(parse_scene_group(group_node));
            }
            lxfml.scene.models.push_back(std::move(model));
        }

        // Flatten scene parts into the bricks vector
        for (const auto& model : lxfml.scene.models) {
            for (const auto& group : model.groups) {
                flatten_scene_group(group, lxfml.bricks);
            }
        }
    }

    // <Models> — v2 LU-native format with nested Groups and angle-axis Bricks.
    // Brick transforms are absolute — groups are organizational only.
    if (auto models_node = root.child("Models")) {
        for (auto model_node : models_node.children("Model")) {
            if (lxfml.name.empty()) {
                lxfml.name = model_node.attribute("modelName").as_string();
            }
            flatten_v2_node(model_node, lxfml.bricks);
        }
    }

    // <RigidSystems>
    if (auto rs_node = root.child("RigidSystems")) {
        for (auto& rs : rs_node.children("RigidSystem")) {
            lxfml.rigid_systems.push_back(parse_rigid_system(rs));
        }
    }

    // <GroupSystems> — always empty in client files; count GroupSystem elements
    if (auto gs_node = root.child("GroupSystems")) {
        for (auto& _ : gs_node.children("GroupSystem")) {
            (void)_;
            ++lxfml.group_system_count;
        }
    }

    // <BuildingInstructions> — always empty in client files
    if (root.child("BuildingInstructions")) {
        lxfml.has_building_instructions = true;
    }

    return lxfml;
}

} // namespace lu::assets
