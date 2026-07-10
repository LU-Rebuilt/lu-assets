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
            if (child.attribute("a")) {
                rr.has_vec3_form = true;
                parse_vec3(child.attribute("a").as_string("0,0,0"), rr.a);
                parse_vec3(child.attribute("z").as_string("0,0,1"), rr.z);
                parse_vec3(child.attribute("t").as_string("0,0,0"), rr.t);
            } else {
                rr.has_vec3_form = false;
                rr.ax = child.attribute("ax").as_float(0.0f);
                rr.ay = child.attribute("ay").as_float(0.0f);
                rr.az = child.attribute("az").as_float(0.0f);
                rr.zx = child.attribute("zx").as_float(0.0f);
                rr.zy = child.attribute("zy").as_float(0.0f);
                rr.zz = child.attribute("zz").as_float(1.0f);
                rr.tx = child.attribute("tx").as_float(0.0f);
                rr.ty = child.attribute("ty").as_float(0.0f);
                rr.tz = child.attribute("tz").as_float(0.0f);
            }
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
            if (auto t = child.attribute("transformation")) {
                r.has_transformation = true;
                parse_float_list<12>(t.as_string(), r.transformation);
            } else {
                r.has_transformation = false;
                r.angle = child.attribute("angle").as_float(0.0f);
                r.ax = child.attribute("ax").as_float(0.0f);
                r.ay = child.attribute("ay").as_float(1.0f);
                r.az = child.attribute("az").as_float(0.0f);
                r.tx = child.attribute("tx").as_float(0.0f);
                r.ty = child.attribute("ty").as_float(0.0f);
                r.tz = child.attribute("tz").as_float(0.0f);
            }
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
            if (auto a = child.attribute("assemblyID")) {
                p.has_assembly = true;
                p.assembly_id = a.as_int();
                p.assembly_ref_id = child.attribute("assemblyRefID").as_int();
            }
            p.angle       = child.attribute("angle").as_float(0.0f);
            p.ax          = child.attribute("ax").as_float(0.0f);
            p.ay          = child.attribute("ay").as_float(1.0f);
            p.az          = child.attribute("az").as_float(0.0f);
            p.tx          = child.attribute("tx").as_float(0.0f);
            p.ty          = child.attribute("ty").as_float(0.0f);
            p.tz          = child.attribute("tz").as_float(0.0f);
            for (auto& sm : child.children("SubMaterial")) {
                LxfmlSubMaterial m;
                m.surface_id  = sm.attribute("surfaceID").as_int();
                m.material_id = sm.attribute("materialID").as_int();
                p.sub_materials.push_back(m);
            }
            for (auto& dec : child.children("Decoration")) {
                LxfmlDecoration d;
                d.surface_id    = dec.attribute("surfaceID").as_int();
                d.decoration_id = dec.attribute("decorationID").as_int();
                p.decorations.push_back(d);
            }
            // When there's no real sub-element, real files split roughly 89%/11%
            // between self-closed and open-tag/close-tag-on-next-line — a genuine
            // per-instance choice. Detected the same way as BuildingInstructions: a
            // self-closed element has zero children; open-empty has exactly one
            // whitespace-only PCDATA child (parse_ws_pcdata_single, see load_buffer).
            if (p.sub_materials.empty() && p.decorations.empty()) {
                p.empty_self_closed = !child.first_child();
            }
            g.parts.push_back(std::move(p));
        } else if (tag == "Joint") {
            g.joints.push_back(parse_joint(child));
        }
    }
    if (g.parts.empty() && g.joints.empty()) {
        g.empty_self_closed = !node.first_child();
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

// ── v2 (LDD-native <Models>) parsing ──────────────────────────────────────────
// Distinct schema from v4/v5: rotPivW/X/Y/Z (quaternion) + x/y/z, per-brick assembly
// grouping, SubBrick material overrides. See lxfml_types.h for the full field list.

LxfmlSubBrick parse_v2_subbrick(const pugi::xml_node& node) {
    LxfmlSubBrick sb;
    sb.material_id = node.attribute("materialID").as_int();
    sb.surface_id  = node.attribute("surfaceID").as_int();
    return sb;
}

LxfmlV2Brick parse_v2_brick(const pugi::xml_node& node) {
    LxfmlV2Brick b;
    b.angle = node.attribute("angle").as_double(0.0);
    b.ax = node.attribute("ax").as_double(0.0);
    b.ay = node.attribute("ay").as_double(0.0);
    b.az = node.attribute("az").as_double(0.0);
    if (auto a = node.attribute("assemblyID")) {
        b.has_assembly = true;
        b.assembly_id = a.as_int();
        b.assembly_ref_id = node.attribute("assemblyRefID").as_int();
    }
    b.brick_id = node.attribute("brickID").as_int();
    b.brick_name = node.attribute("brickName").as_string();
    b.brick_ref_id = node.attribute("brickRefID").as_int();
    if (auto mi = node.attribute("materialID")) {
        b.has_material_id = true;
        b.material_id = mi.as_int();
    } else {
        b.has_material_id = false;
    }
    b.object_unique_id = node.attribute("objectUniqueID").as_int();
    b.rot_piv_w = node.attribute("rotPivW").as_double(1.0);
    b.rot_piv_x = node.attribute("rotPivX").as_double(0.0);
    b.rot_piv_y = node.attribute("rotPivY").as_double(0.0);
    b.rot_piv_z = node.attribute("rotPivZ").as_double(0.0);
    b.x = node.attribute("x").as_double(0.0);
    b.y = node.attribute("y").as_double(0.0);
    b.z = node.attribute("z").as_double(0.0);
    for (auto& child : node.children("SubBrick")) {
        b.sub_bricks.push_back(parse_v2_subbrick(child));
    }
    return b;
}

LxfmlV2Group parse_v2_group(const pugi::xml_node& node) {
    LxfmlV2Group g;
    g.angle = node.attribute("angle").as_double(0.0);
    g.ax = node.attribute("ax").as_double(0.0);
    g.ay = node.attribute("ay").as_double(0.0);
    g.az = node.attribute("az").as_double(0.0);
    if (auto a = node.attribute("assemblyID")) {
        g.has_assembly = true;
        g.assembly_id = a.as_int();
    }
    g.group_id = node.attribute("groupID").as_int();
    g.group_name = node.attribute("groupName").as_string();
    g.group_ref_id = node.attribute("groupRefID").as_int();
    g.object_unique_id = node.attribute("objectUniqueID").as_int();
    g.rot_piv_w = node.attribute("rotPivW").as_double(1.0);
    g.rot_piv_x = node.attribute("rotPivX").as_double(0.0);
    g.rot_piv_y = node.attribute("rotPivY").as_double(0.0);
    g.rot_piv_z = node.attribute("rotPivZ").as_double(0.0);
    g.x = node.attribute("x").as_double(0.0);
    g.y = node.attribute("y").as_double(0.0);
    g.z = node.attribute("z").as_double(0.0);
    for (auto& child : node.children()) {
        std::string_view tag = child.name();
        if (tag == "Brick") {
            g.bricks.push_back(parse_v2_brick(child));
            g.child_order.push_back({LxfmlV2ChildRef::Kind::Brick, g.bricks.size() - 1});
        } else if (tag == "Group") {
            g.groups.push_back(parse_v2_group(child));
            g.child_order.push_back({LxfmlV2ChildRef::Kind::Group, g.groups.size() - 1});
        }
    }
    return g;
}

LxfmlV2Model parse_v2_model(const pugi::xml_node& node) {
    LxfmlV2Model m;
    m.angle = node.attribute("angle").as_double(0.0);
    m.ax = node.attribute("ax").as_double(0.0);
    m.ay = node.attribute("ay").as_double(0.0);
    m.az = node.attribute("az").as_double(0.0);
    m.model_name = node.attribute("modelName").as_string();
    m.rot_piv_w = node.attribute("rotPivW").as_double(1.0);
    m.rot_piv_x = node.attribute("rotPivX").as_double(0.0);
    m.rot_piv_y = node.attribute("rotPivY").as_double(0.0);
    m.rot_piv_z = node.attribute("rotPivZ").as_double(0.0);
    m.x = node.attribute("x").as_double(0.0);
    m.y = node.attribute("y").as_double(0.0);
    m.z = node.attribute("z").as_double(0.0);
    for (auto& child : node.children()) {
        std::string_view tag = child.name();
        if (tag == "Group") {
            m.groups.push_back(parse_v2_group(child));
            m.child_order.push_back({LxfmlV2ChildRef::Kind::Group, m.groups.size() - 1});
        } else if (tag == "Brick") {
            m.bricks.push_back(parse_v2_brick(child));
            m.child_order.push_back({LxfmlV2ChildRef::Kind::Brick, m.bricks.size() - 1});
        }
    }
    return m;
}

// Convert v2 Models into the unified bricks vector so downstream assembly code (which
// only understands LxfmlBrick/LxfmlPart/LxfmlBone) works for all three formats. Lossy
// in the same way the v4 Scene flattening is (angle-axis -> matrix); the original v2
// representation is preserved separately in LxfmlFile::v2_models for round-trip.
void flatten_v2_brick(const LxfmlV2Brick& vb, std::vector<LxfmlBrick>& out) {
    float xform[12];
    angle_axis_to_transform(vb.angle, vb.ax, vb.ay, vb.az, vb.x, vb.y, vb.z, xform);

    LxfmlBrick brick;
    brick.ref_id = vb.brick_ref_id;
    brick.design_id = vb.brick_id;

    LxfmlPart part;
    part.ref_id = vb.brick_ref_id;
    part.design_id = vb.brick_id;
    part.materials = std::to_string(vb.material_id);

    LxfmlBone bone;
    bone.ref_id = 0;
    std::memcpy(bone.transform, xform, sizeof(xform));
    part.bones.push_back(std::move(bone));

    brick.parts.push_back(std::move(part));
    out.push_back(std::move(brick));
}

void flatten_v2_group(const LxfmlV2Group& group, std::vector<LxfmlBrick>& out) {
    for (const auto& vb : group.bricks) flatten_v2_brick(vb, out);
    for (const auto& sub : group.groups) flatten_v2_group(sub, out);
}

// ── GroupSystems <Group> parsing (recursive; see LxfmlGroupSystemGroup) ─────────

LxfmlGroupSystemGroup parse_group_system_group(const pugi::xml_node& node) {
    LxfmlGroupSystemGroup g;
    auto t = node.attribute("transformation");
    if (t) parse_float_list<12>(t.as_string(), g.transformation);
    auto p = node.attribute("pivot");
    if (p) parse_vec3(p.as_string(), g.pivot);
    auto pr = node.attribute("partRefs");
    if (pr && pr.as_string()[0] != '\0') g.part_refs = parse_int_list(pr.as_string());
    for (auto& child : node.children("Group")) {
        g.children.push_back(parse_group_system_group(child));
    }
    return g;
}

// ── BuildingInstructions content (real but very rare — see lxfml_types.h) ────────

LxfmlStep parse_step(const pugi::xml_node& node) {
    LxfmlStep s;
    s.name = node.attribute("name").as_string();
    if (auto cr = node.attribute("cameraRefID")) {
        s.has_camera_ref_id = true;
        s.camera_ref_id = cr.as_int();
    }
    for (auto& child : node.children()) {
        std::string_view tag = child.name();
        if (tag == "PartRef") {
            LxfmlPartRef pr;
            pr.part_ref_id = child.attribute("partRefID").as_int();
            s.part_refs.push_back(pr);
        } else if (tag == "Step") {
            s.steps.push_back(parse_step(child));
        }
    }
    return s;
}

} // anonymous namespace

LxfmlFile lxfml_parse(std::span<const uint8_t> data) {
    pugi::xml_document doc;
    // parse_ws_pcdata_single keeps a whitespace-only text node as a single PCDATA child
    // instead of dropping it — needed to tell "<Tag/>" (self-closed, 0 children) apart
    // from "<Tag>\r\n  </Tag>" (open-empty, 1 whitespace PCDATA child) for
    // BuildingInstructions, which real files use both forms of.
    auto result = doc.load_buffer(data.data(), data.size(),
                                   pugi::parse_default | pugi::parse_ws_pcdata_single);
    if (!result) {
        throw LxfmlError("LXFML: XML parse error: " + std::string(result.description()));
    }

    auto root = doc.child("LXFML");
    if (!root) {
        throw LxfmlError("LXFML: missing root LXFML element");
    }

    LxfmlFile lxfml;

    // Detect the whole-file "\r\r\n" line-ending quirk (see lxfml_types.h) from the
    // declaration line alone — the pattern is always whole-file-consistent (0 mixed
    // files in the real corpus), and pugixml normalizes line endings during parsing, so
    // this information isn't recoverable from the parsed tree.
    {
        std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
        size_t decl_end = text.find("?>");
        if (decl_end != std::string_view::npos) {
            lxfml.uses_double_cr_line_endings =
                text.substr(decl_end, 5) == "?>\r\r\n";
        }
    }

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
        lxfml.has_meta = true;
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
        lxfml.has_cameras = true;
        for (auto& cam_node : cams_node.children("Camera")) {
            lxfml.cameras.push_back(parse_camera(cam_node));
        }
    }

    // <Bricks> — v5 LU format with transformation matrices
    if (auto bricks_node = root.child("Bricks")) {
        lxfml.format = LxfmlFormat::Bricks;
        if (auto cr = bricks_node.attribute("cameraRef")) {
            lxfml.has_bricks_camera_ref = true;
            lxfml.bricks_camera_ref = cr.as_int();
        }
        for (auto brick_node : bricks_node.children("Brick")) {
            LxfmlBrick brick;
            brick.ref_id    = brick_node.attribute("refID").as_int();
            brick.design_id = brick_node.attribute("designID").as_int();
            if (auto in = brick_node.attribute("itemNos")) {
                brick.has_item_nos = true;
                brick.item_nos = in.as_string();
            }

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
                        bone.has_transformation = true;
                        parse_float_list<12>(t.as_string(), bone.transform);
                    } else {
                        bone.has_transformation = false;
                        bone.angle = bone_node.attribute("angle").as_float(0.0f);
                        bone.ax = bone_node.attribute("ax").as_float(0.0f);
                        bone.ay = bone_node.attribute("ay").as_float(1.0f);
                        bone.az = bone_node.attribute("az").as_float(0.0f);
                        bone.tx = bone_node.attribute("tx").as_float(0.0f);
                        bone.ty = bone_node.attribute("ty").as_float(0.0f);
                        bone.tz = bone_node.attribute("tz").as_float(0.0f);
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
        lxfml.format = LxfmlFormat::Scene;
        if (auto cr = scene_node.attribute("cameraRefID")) {
            lxfml.scene.has_camera_ref_id = true;
            lxfml.scene.camera_ref_id = cr.as_int();
        } else {
            lxfml.scene.has_camera_ref_id = false;
        }
        for (auto model_node : scene_node.children("Model")) {
            LxfmlSceneModel model;
            model.name = model_node.attribute("name").as_string();
            // Group and Joint can both appear as direct Model children (some real
            // files put Joints alongside Groups rather than nested inside one) —
            // iterate all children once rather than children("Group") only, so
            // top-level Joints aren't silently dropped.
            for (auto& child : model_node.children()) {
                std::string_view tag = child.name();
                if (tag == "Group") {
                    model.groups.push_back(parse_scene_group(child));
                } else if (tag == "Joint") {
                    model.joints.push_back(parse_joint(child));
                }
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
        lxfml.format = LxfmlFormat::Models;
        for (auto model_node : models_node.children("Model")) {
            LxfmlV2Model model = parse_v2_model(model_node);
            if (lxfml.name.empty()) {
                lxfml.name = model.model_name;
            }
            for (const auto& group : model.groups) {
                flatten_v2_group(group, lxfml.bricks);
            }
            for (const auto& brick : model.bricks) {
                flatten_v2_brick(brick, lxfml.bricks);
            }
            lxfml.v2_models.push_back(std::move(model));
        }
    }

    // <RigidSystems>
    if (auto rs_node = root.child("RigidSystems")) {
        lxfml.has_rigid_systems = true;
        for (auto& rs : rs_node.children("RigidSystem")) {
            lxfml.rigid_systems.push_back(parse_rigid_system(rs));
        }
    }

    // <GroupSystems> — one entry per <GroupSystem> child; each may hold real <Group>
    // grouping data (82/1966 real files) or be empty (the common case).
    if (auto gs_node = root.child("GroupSystems")) {
        lxfml.has_group_systems = true;
        for (auto& gs : gs_node.children("GroupSystem")) {
            std::vector<LxfmlGroupSystemGroup> groups;
            for (auto& group_node : gs.children("Group")) {
                groups.push_back(parse_group_system_group(group_node));
            }
            lxfml.group_systems.push_back(std::move(groups));
        }
    }

    // <BuildingInstructions> — 3 real shapes: self-closed, open-empty, or (in exactly 1
    // real client file) genuine nested <BuildingInstruction><Step> content. Distinguished
    // via parse_ws_pcdata_single (see load_buffer call above): a self-closed element has
    // zero children, an open-empty one has exactly one whitespace-only PCDATA child.
    if (auto bi = root.child("BuildingInstructions")) {
        lxfml.building_instructions = bi.first_child()
            ? LxfmlFile::ElementShape::OpenEmpty
            : LxfmlFile::ElementShape::SelfClosed;
        for (auto& bi_node : bi.children("BuildingInstruction")) {
            LxfmlBuildingInstruction inst;
            for (auto& step_node : bi_node.children("Step")) {
                inst.steps.push_back(parse_step(step_node));
            }
            lxfml.building_instruction_data.push_back(std::move(inst));
        }
    }

    // <Prepacks> — v2-format files only; always self-closed and empty in real samples.
    if (root.child("Prepacks")) {
        lxfml.has_prepacks = true;
    }

    return lxfml;
}

} // namespace lu::assets
