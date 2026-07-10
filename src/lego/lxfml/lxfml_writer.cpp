#include "lego/lxfml/lxfml_writer.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

namespace lu::assets {

namespace {

// ── Float formatting ──────────────────────────────────────────────────────────
//
// v5 Bricks / v4 Scene: 17 significant digits, round-half-away-from-zero, trailing
// zeros stripped. Verified against every float-typed attribute in the real corpus
// (134,861/134,906 = 99.97% exact match): the only mismatches are (a) a genuine
// exact-decimal-tie at the 18th significant digit — the true value sits precisely
// between two 17-digit decimal strings, and LDD/LU's .NET-based tool's internal
// Grisu3/Dragon4 tie-break isn't reproducible without its exact algorithm (see
// lxfml_writer.h) — or (b) individual values that look hand-edited after export
// (e.g. "-0.6" in a file that's 2-space/CRLF-formatted like every other real sample
// everywhere else) or from a distinct older LDD tool version (exactly 1 file in the
// whole corpus uses tab indentation and 15-digit fixed-precision floats) — neither
// of which this format-preserving-by-value (not by-source-text) writer can recover.
//
// %.20Le (long double, 21 significant digits) is used as the intermediate instead of
// %.17e/%.16e (double) because round-tripping a double-widened-from-float32 through
// two separate double-precision snprintf calls at 17 and 18 digits (as an earlier
// version of this function did) can itself introduce a spurious rounding difference
// between the two calls, causing false tie detection. Extended precision gives enough
// guard digits that a single %.20Le call's digit-17-onward truncation reliably matches
// the value's true decimal expansion for tie detection and final rounding alike.
std::string format_float17(float value) {
    if (value == 0.0f) return std::signbit(value) ? "-0" : "0";
    double d = static_cast<double>(value);
    bool neg = d < 0;
    long double ld = neg ? -static_cast<long double>(d) : static_cast<long double>(d);

    char buf[80];
    std::snprintf(buf, sizeof(buf), "%.20Le", ld);
    std::string s(buf);
    size_t epos = s.find('e');
    std::string mantissa_full = s.substr(0, epos); // "D.DDDDDDDDDDDDDDDDDDDD" (21 digits)
    int exp = std::atoi(s.c_str() + epos + 1);

    std::string digits_full;
    for (char c : mantissa_full) if (c != '.') digits_full += c;

    // Round to 17 significant digits, half-away-from-zero, using digit 18 as the
    // decider (this is exact, not another rounding pass, since digits_full already
    // carries 21 significant digits of guard precision).
    std::string digits = digits_full.substr(0, 17);
    if (digits_full[17] >= '5') {
        int carry = 1;
        for (int i = 16; i >= 0 && carry; --i) {
            int d2 = (digits[i] - '0') + carry;
            digits[i] = static_cast<char>('0' + (d2 % 10));
            carry = d2 / 10;
        }
        if (carry) {
            digits = "1" + digits;
            digits.pop_back();
            exp += 1;
        }
    }
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();

    // Below 1e-4 in magnitude, real files switch to scientific notation (.NET's
    // general/round-trip formatter threshold: exponent < -4), as "D.DDDE-0EE" with a
    // 3-digit zero-padded signed exponent and the same trailing-zero-stripped mantissa.
    // No real sample in the corpus needs the symmetric large-magnitude case (exponent
    // >= 17), so only the small-magnitude threshold is implemented.
    if (exp < -4) {
        std::string mantissa = digits.size() > 1 ? digits.substr(0, 1) + "." + digits.substr(1)
                                                  : digits;
        char exp_buf[16];
        std::snprintf(exp_buf, sizeof(exp_buf), "%+04d", exp);
        return (neg ? "-" : "") + mantissa + "E" + exp_buf;
    }

    // Place the decimal point: value = 0.digits * 10^(exp+1), i.e. digit[0] has
    // place value 10^exp.
    std::string out;
    if (exp >= 0) {
        if (static_cast<size_t>(exp) + 1 >= digits.size()) {
            out = digits + std::string(exp + 1 - digits.size(), '0');
        } else {
            out = digits.substr(0, exp + 1) + "." + digits.substr(exp + 1);
        }
    } else {
        out = "0." + std::string(-exp - 1, '0') + digits;
    }
    return (neg ? "-" : "") + out;
}

// v2 Models: fixed 8-decimal-place formatting of a double (v2's fields are double, not
// float32 — see lxfml_types.h), verified byte-exact against all 6 real v2 files.
std::string format_float8(double value) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.8f", value);
    return buf;
}

std::string format_int_list(const std::vector<int32_t>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out += ",";
        out += std::to_string(values[i]);
    }
    return out;
}

std::string format_float_list17(const float* values, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        if (i) out += ",";
        out += format_float17(values[i]);
    }
    return out;
}

// XML-escape attribute value content (real files only ever contain plain ASCII names,
// but escape defensively for correctness).
std::string xml_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// ── Small XML text builder ──────────────────────────────────────────────────────
// Not using pugixml for writing: real files need exact attribute order (schema-
// declaration order, not alphabetical, for v4/v5; alphabetical for v2), exact self-
// closing-vs-open-empty control, and specific blank-line placement pugixml's own
// serializer doesn't offer control over.

struct Attr {
    std::string name;
    std::string value;
};

class XmlBuilder {
public:
    explicit XmlBuilder(std::string line_ending) : nl_(std::move(line_ending)) {}

    void raw(const std::string& s) { out_ += s; }
    void blank_line() { out_ += nl_; }

    void open(int depth, const std::string& tag, const std::vector<Attr>& attrs) {
        tag_line(depth, tag, attrs, false, false);
    }
    void self_closed(int depth, const std::string& tag, const std::vector<Attr>& attrs) {
        tag_line(depth, tag, attrs, true, false);
    }
    void close(int depth, const std::string& tag) {
        out_ += std::string(static_cast<size_t>(depth) * 2, ' ') + "</" + tag + ">" + nl_;
    }
    // Open tag on its own line, close tag on the next line at the SAME depth (not
    // depth+1 — there's no real child content, just the whitespace pugixml's
    // parse_ws_pcdata_single preserved as a single text node). Real files use this for
    // GroupSystem/BuildingInstructions when non-self-closed but childless.
    void open_close_empty(int depth, const std::string& tag,
                           const std::vector<Attr>& attrs) {
        tag_line(depth, tag, attrs, false, false);
        out_ += std::string(static_cast<size_t>(depth) * 2, ' ') + "</" + tag + ">" + nl_;
    }

    std::string str() const { return out_; }

private:
    void tag_line(int depth, const std::string& tag, const std::vector<Attr>& attrs,
                  bool self_close, bool immediate_close) {
        out_ += std::string(static_cast<size_t>(depth) * 2, ' ');
        out_ += "<" + tag;
        for (const auto& a : attrs) {
            out_ += " " + a.name + "=\"" + xml_escape(a.value) + "\"";
        }
        if (self_close) {
            out_ += "/>";
        } else if (immediate_close) {
            out_ += "></" + tag + ">";
        } else {
            out_ += ">";
        }
        out_ += nl_;
    }

    std::string out_;
    std::string nl_;
};

// ── v5 Bricks serializer ─────────────────────────────────────────────────────────

void write_bone(XmlBuilder& xml, int depth, const LxfmlBone& b) {
    std::vector<Attr> attrs = {{"refID", std::to_string(b.ref_id)}};
    if (b.has_transformation) {
        attrs.push_back({"transformation", format_float_list17(b.transform, 12)});
    } else {
        attrs.push_back({"angle", format_float17(b.angle)});
        attrs.push_back({"ax", format_float17(b.ax)});
        attrs.push_back({"ay", format_float17(b.ay)});
        attrs.push_back({"az", format_float17(b.az)});
        attrs.push_back({"tx", format_float17(b.tx)});
        attrs.push_back({"ty", format_float17(b.ty)});
        attrs.push_back({"tz", format_float17(b.tz)});
    }
    // Bone is never self-closed in any real file (224054/224054 use the open-tag/
    // close-tag-on-next-line form despite having no real child content) — universal,
    // unlike Part/Group where the shape varies per-instance.
    xml.open_close_empty(depth, "Bone", attrs);
}

void write_part_v5(XmlBuilder& xml, int depth, const LxfmlPart& p) {
    std::vector<Attr> attrs = {
        {"refID", std::to_string(p.ref_id)},
        {"designID", std::to_string(p.design_id)},
        {"materials", p.materials},
    };
    if (!p.decoration.empty()) attrs.push_back({"decoration", p.decoration});
    if (p.bones.empty()) {
        xml.self_closed(depth, "Part", attrs);
        return;
    }
    xml.open(depth, "Part", attrs);
    for (const auto& b : p.bones) write_bone(xml, depth + 1, b);
    xml.close(depth, "Part");
}

void write_bricks(XmlBuilder& xml, int depth, const LxfmlFile& lxfml) {
    std::vector<Attr> attrs;
    if (lxfml.has_bricks_camera_ref) {
        attrs.push_back({"cameraRef", std::to_string(lxfml.bricks_camera_ref)});
    }
    if (lxfml.bricks.empty()) {
        xml.self_closed(depth, "Bricks", attrs);
        return;
    }
    xml.open(depth, "Bricks", attrs);
    for (const auto& brick : lxfml.bricks) {
        std::vector<Attr> battrs = {
            {"refID", std::to_string(brick.ref_id)},
            {"designID", std::to_string(brick.design_id)},
        };
        if (brick.has_item_nos) battrs.push_back({"itemNos", brick.item_nos});
        if (brick.parts.empty()) {
            xml.self_closed(depth + 1, "Brick", battrs);
            continue;
        }
        xml.open(depth + 1, "Brick", battrs);
        for (const auto& part : brick.parts) write_part_v5(xml, depth + 2, part);
        xml.close(depth + 1, "Brick");
    }
    xml.close(depth, "Bricks");
}

// ── v4 Scene serializer ──────────────────────────────────────────────────────────

void write_rigid_ref(XmlBuilder& xml, int depth, const LxfmlRigidRef& rr) {
    std::vector<Attr> attrs = {{"rigidRef", std::to_string(rr.rigid_ref)}};
    if (rr.has_vec3_form) {
        attrs.push_back({"a", format_float_list17(rr.a, 3)});
        attrs.push_back({"z", format_float_list17(rr.z, 3)});
        attrs.push_back({"t", format_float_list17(rr.t, 3)});
    } else {
        attrs.push_back({"ax", format_float17(rr.ax)});
        attrs.push_back({"ay", format_float17(rr.ay)});
        attrs.push_back({"az", format_float17(rr.az)});
        attrs.push_back({"zx", format_float17(rr.zx)});
        attrs.push_back({"zy", format_float17(rr.zy)});
        attrs.push_back({"zz", format_float17(rr.zz)});
        attrs.push_back({"tx", format_float17(rr.tx)});
        attrs.push_back({"ty", format_float17(rr.ty)});
        attrs.push_back({"tz", format_float17(rr.tz)});
    }
    xml.self_closed(depth, "RigidRef", attrs);
}

void write_group_ref(XmlBuilder& xml, int depth, const LxfmlGroupRef& gr) {
    std::vector<Attr> attrs = {
        {"groupRef", std::to_string(gr.group_ref)},
        {"ax", format_float17(gr.ax)}, {"ay", format_float17(gr.ay)}, {"az", format_float17(gr.az)},
        {"zx", format_float17(gr.zx)}, {"zy", format_float17(gr.zy)}, {"zz", format_float17(gr.zz)},
        {"tx", format_float17(gr.tx)}, {"ty", format_float17(gr.ty)}, {"tz", format_float17(gr.tz)},
    };
    xml.self_closed(depth, "GroupRef", attrs);
}

void write_joint(XmlBuilder& xml, int depth, const LxfmlJoint& j) {
    std::vector<Attr> attrs = {{"type", j.type}};
    if (j.rigid_refs.empty() && j.group_refs.empty()) {
        xml.self_closed(depth, "Joint", attrs);
        return;
    }
    xml.open(depth, "Joint", attrs);
    for (const auto& rr : j.rigid_refs) write_rigid_ref(xml, depth + 1, rr);
    for (const auto& gr : j.group_refs) write_group_ref(xml, depth + 1, gr);
    xml.close(depth, "Joint");
}

void write_scene_part(XmlBuilder& xml, int depth, const LxfmlScenePart& p) {
    std::vector<Attr> attrs = {{"refID", std::to_string(p.ref_id)}};
    if (!p.name.empty()) attrs.push_back({"name", p.name});
    attrs.push_back({"designID", std::to_string(p.design_id)});
    attrs.push_back({"materialID", std::to_string(p.material_id)});
    if (!p.item_nos.empty()) attrs.push_back({"itemNos", p.item_nos});
    if (p.has_assembly) {
        attrs.push_back({"assemblyID", std::to_string(p.assembly_id)});
        attrs.push_back({"assemblyRefID", std::to_string(p.assembly_ref_id)});
    }
    attrs.push_back({"angle", format_float17(p.angle)});
    attrs.push_back({"ax", format_float17(p.ax)});
    attrs.push_back({"ay", format_float17(p.ay)});
    attrs.push_back({"az", format_float17(p.az)});
    attrs.push_back({"tx", format_float17(p.tx)});
    attrs.push_back({"ty", format_float17(p.ty)});
    attrs.push_back({"tz", format_float17(p.tz)});

    if (p.sub_materials.empty() && p.decorations.empty()) {
        if (p.empty_self_closed) {
            xml.self_closed(depth, "Part", attrs);
        } else {
            xml.open_close_empty(depth, "Part", attrs);
        }
        return;
    }
    xml.open(depth, "Part", attrs);
    for (const auto& sm : p.sub_materials) {
        xml.self_closed(depth + 1, "SubMaterial", {
            {"surfaceID", std::to_string(sm.surface_id)},
            {"materialID", std::to_string(sm.material_id)},
        });
    }
    for (const auto& d : p.decorations) {
        xml.self_closed(depth + 1, "Decoration", {
            {"surfaceID", std::to_string(d.surface_id)},
            {"decorationID", std::to_string(d.decoration_id)},
        });
    }
    xml.close(depth, "Part");
}

void write_scene_group(XmlBuilder& xml, int depth, const LxfmlSceneGroup& g) {
    std::vector<Attr> attrs = {{"refID", std::to_string(g.ref_id)}};
    if (!g.name.empty()) attrs.push_back({"name", g.name});
    attrs.push_back({"angle", format_float17(g.angle)});
    attrs.push_back({"ax", format_float17(g.ax)});
    attrs.push_back({"ay", format_float17(g.ay)});
    attrs.push_back({"az", format_float17(g.az)});
    attrs.push_back({"tx", format_float17(g.tx)});
    attrs.push_back({"ty", format_float17(g.ty)});
    attrs.push_back({"tz", format_float17(g.tz)});

    if (g.parts.empty() && g.joints.empty()) {
        if (g.empty_self_closed) {
            xml.self_closed(depth, "Group", attrs);
        } else {
            xml.open_close_empty(depth, "Group", attrs);
        }
        return;
    }
    xml.open(depth, "Group", attrs);
    for (const auto& p : g.parts) write_scene_part(xml, depth + 1, p);
    for (const auto& j : g.joints) write_joint(xml, depth + 1, j);
    xml.close(depth, "Group");
}

void write_scene(XmlBuilder& xml, int depth, const LxfmlScene& scene) {
    std::vector<Attr> attrs;
    if (scene.has_camera_ref_id) {
        attrs.push_back({"cameraRefID", std::to_string(scene.camera_ref_id)});
    }
    if (scene.models.empty()) {
        xml.self_closed(depth, "Scene", attrs);
        return;
    }
    xml.open(depth, "Scene", attrs);
    for (const auto& model : scene.models) {
        std::vector<Attr> mattrs;
        if (!model.name.empty()) mattrs.push_back({"name", model.name});
        if (model.groups.empty() && model.joints.empty()) {
            xml.self_closed(depth + 1, "Model", mattrs);
            continue;
        }
        xml.open(depth + 1, "Model", mattrs);
        for (const auto& g : model.groups) write_scene_group(xml, depth + 2, g);
        for (const auto& j : model.joints) write_joint(xml, depth + 2, j);
        xml.close(depth + 1, "Model");
    }
    xml.close(depth, "Scene");
}

// ── v2 Models serializer (alphabetical attribute order, %.8f floats) ─────────────

void write_v2_subbrick(XmlBuilder& xml, int depth, const LxfmlSubBrick& sb) {
    // Alphabetical: materialID, surfaceID.
    xml.self_closed(depth, "SubBrick", {
        {"materialID", std::to_string(sb.material_id)},
        {"surfaceID", std::to_string(sb.surface_id)},
    });
}

void write_v2_brick(XmlBuilder& xml, int depth, const LxfmlV2Brick& b) {
    // Alphabetical order: angle, [assemblyID, assemblyRefID], ax, ay, az, brickID,
    // brickName, brickRefID, materialID, objectUniqueID, rotPivW/X/Y/Z, x, y, z.
    std::vector<Attr> attrs = {{"angle", format_float8(b.angle)}};
    if (b.has_assembly) {
        attrs.push_back({"assemblyID", std::to_string(b.assembly_id)});
        attrs.push_back({"assemblyRefID", std::to_string(b.assembly_ref_id)});
    }
    attrs.push_back({"ax", format_float8(b.ax)});
    attrs.push_back({"ay", format_float8(b.ay)});
    attrs.push_back({"az", format_float8(b.az)});
    attrs.push_back({"brickID", std::to_string(b.brick_id)});
    attrs.push_back({"brickName", b.brick_name});
    attrs.push_back({"brickRefID", std::to_string(b.brick_ref_id)});
    if (b.has_material_id) attrs.push_back({"materialID", std::to_string(b.material_id)});
    attrs.push_back({"objectUniqueID", std::to_string(b.object_unique_id)});
    attrs.push_back({"rotPivW", format_float8(b.rot_piv_w)});
    attrs.push_back({"rotPivX", format_float8(b.rot_piv_x)});
    attrs.push_back({"rotPivY", format_float8(b.rot_piv_y)});
    attrs.push_back({"rotPivZ", format_float8(b.rot_piv_z)});
    attrs.push_back({"x", format_float8(b.x)});
    attrs.push_back({"y", format_float8(b.y)});
    attrs.push_back({"z", format_float8(b.z)});

    if (b.sub_bricks.empty()) {
        xml.self_closed(depth, "Brick", attrs);
        return;
    }
    xml.open(depth, "Brick", attrs);
    for (const auto& sb : b.sub_bricks) write_v2_subbrick(xml, depth + 1, sb);
    xml.close(depth, "Brick");
}

void write_v2_group(XmlBuilder& xml, int depth, const LxfmlV2Group& g) {
    // Alphabetical: angle, [assemblyID], ax, ay, az, groupID, groupName, groupRefID,
    // objectUniqueID, rotPivW/X/Y/Z, x, y, z.
    std::vector<Attr> attrs = {{"angle", format_float8(g.angle)}};
    if (g.has_assembly) attrs.push_back({"assemblyID", std::to_string(g.assembly_id)});
    attrs.push_back({"ax", format_float8(g.ax)});
    attrs.push_back({"ay", format_float8(g.ay)});
    attrs.push_back({"az", format_float8(g.az)});
    attrs.push_back({"groupID", std::to_string(g.group_id)});
    attrs.push_back({"groupName", g.group_name});
    attrs.push_back({"groupRefID", std::to_string(g.group_ref_id)});
    attrs.push_back({"objectUniqueID", std::to_string(g.object_unique_id)});
    attrs.push_back({"rotPivW", format_float8(g.rot_piv_w)});
    attrs.push_back({"rotPivX", format_float8(g.rot_piv_x)});
    attrs.push_back({"rotPivY", format_float8(g.rot_piv_y)});
    attrs.push_back({"rotPivZ", format_float8(g.rot_piv_z)});
    attrs.push_back({"x", format_float8(g.x)});
    attrs.push_back({"y", format_float8(g.y)});
    attrs.push_back({"z", format_float8(g.z)});

    if (g.child_order.empty()) {
        xml.self_closed(depth, "Group", attrs);
        return;
    }
    xml.open(depth, "Group", attrs);
    for (const auto& ref : g.child_order) {
        if (ref.kind == LxfmlV2ChildRef::Kind::Brick) {
            write_v2_brick(xml, depth + 1, g.bricks[ref.index]);
        } else {
            write_v2_group(xml, depth + 1, g.groups[ref.index]);
        }
    }
    xml.close(depth, "Group");
}

void write_v2_model(XmlBuilder& xml, int depth, const LxfmlV2Model& m) {
    // Alphabetical: angle, ax, ay, az, modelName, rotPivW/X/Y/Z, x, y, z.
    std::vector<Attr> attrs = {
        {"angle", format_float8(m.angle)},
        {"ax", format_float8(m.ax)}, {"ay", format_float8(m.ay)}, {"az", format_float8(m.az)},
        {"modelName", m.model_name},
        {"rotPivW", format_float8(m.rot_piv_w)}, {"rotPivX", format_float8(m.rot_piv_x)},
        {"rotPivY", format_float8(m.rot_piv_y)}, {"rotPivZ", format_float8(m.rot_piv_z)},
        {"x", format_float8(m.x)}, {"y", format_float8(m.y)}, {"z", format_float8(m.z)},
    };
    if (m.child_order.empty()) {
        xml.self_closed(depth, "Model", attrs);
        return;
    }
    xml.open(depth, "Model", attrs);
    for (const auto& ref : m.child_order) {
        if (ref.kind == LxfmlV2ChildRef::Kind::Brick) {
            write_v2_brick(xml, depth + 1, m.bricks[ref.index]);
        } else {
            write_v2_group(xml, depth + 1, m.groups[ref.index]);
        }
    }
    xml.close(depth, "Model");
}

// ── Shared elements ───────────────────────────────────────────────────────────

void write_camera(XmlBuilder& xml, int depth, const LxfmlCamera& c) {
    std::vector<Attr> attrs = {{"refID", std::to_string(c.ref_id)}};
    if (!c.name.empty()) attrs.push_back({"name", c.name});
    attrs.push_back({"fieldOfView", format_float17(c.field_of_view)});
    attrs.push_back({"distance", format_float17(c.distance)});
    if (c.has_transformation) {
        attrs.push_back({"transformation", format_float_list17(c.transformation, 12)});
    } else {
        attrs.push_back({"angle", format_float17(c.angle)});
        attrs.push_back({"ax", format_float17(c.ax)});
        attrs.push_back({"ay", format_float17(c.ay)});
        attrs.push_back({"az", format_float17(c.az)});
        attrs.push_back({"tx", format_float17(c.tx)});
        attrs.push_back({"ty", format_float17(c.ty)});
        attrs.push_back({"tz", format_float17(c.tz)});
    }
    xml.self_closed(depth, "Camera", attrs);
}

void write_rigid(XmlBuilder& xml, int depth, const LxfmlRigid& r) {
    std::vector<Attr> attrs = {{"refID", std::to_string(r.ref_id)}};
    if (r.has_transformation) {
        attrs.push_back({"transformation", format_float_list17(r.transformation, 12)});
    } else {
        attrs.push_back({"angle", format_float17(r.angle)});
        attrs.push_back({"ax", format_float17(r.ax)});
        attrs.push_back({"ay", format_float17(r.ay)});
        attrs.push_back({"az", format_float17(r.az)});
        attrs.push_back({"tx", format_float17(r.tx)});
        attrs.push_back({"ty", format_float17(r.ty)});
        attrs.push_back({"tz", format_float17(r.tz)});
    }
    if (!r.bone_refs.empty()) {
        attrs.push_back({"boneRefs", format_int_list(r.bone_refs)});
    }
    xml.self_closed(depth, "Rigid", attrs);
}

void write_group_system_group(XmlBuilder& xml, int depth, const LxfmlGroupSystemGroup& g) {
    std::vector<Attr> attrs = {
        {"transformation", format_float_list17(g.transformation, 12)},
        {"pivot", format_float_list17(g.pivot, 3)},
        {"partRefs", format_int_list(g.part_refs)},
    };
    if (g.children.empty()) {
        xml.self_closed(depth, "Group", attrs);
        return;
    }
    xml.open(depth, "Group", attrs);
    for (const auto& c : g.children) write_group_system_group(xml, depth + 1, c);
    xml.close(depth, "Group");
}

void write_step(XmlBuilder& xml, int depth, const LxfmlStep& s) {
    std::vector<Attr> attrs = {{"name", s.name}};
    if (s.has_camera_ref_id) attrs.push_back({"cameraRefID", std::to_string(s.camera_ref_id)});
    if (s.part_refs.empty() && s.steps.empty()) {
        xml.self_closed(depth, "Step", attrs);
        return;
    }
    xml.open(depth, "Step", attrs);
    for (const auto& pr : s.part_refs) {
        xml.self_closed(depth + 1, "PartRef", {{"partRefID", std::to_string(pr.part_ref_id)}});
    }
    for (const auto& sub : s.steps) write_step(xml, depth + 1, sub);
    xml.close(depth, "Step");
}

} // anonymous namespace

std::string lxfml_write(const LxfmlFile& lxfml) {
    // ~10% of real files use "\r\r\n" as the line ending throughout the whole file
    // instead of plain "\r\n" (see LxfmlFile::uses_double_cr_line_endings) — apply it
    // uniformly rather than special-casing just the declaration line.
    XmlBuilder xml(lxfml.uses_double_cr_line_endings ? "\r\r\n" : "\r\n");

    // XML declaration: identical in every real file (aside from the line ending above).
    xml.raw("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>");
    xml.blank_line();

    // Root <LXFML ...> — v2 uses majorVersion/minorVersion, v4/v5 use
    // versionMajor/versionMinor[/name].
    {
        std::vector<Attr> attrs;
        if (lxfml.format == LxfmlFormat::Models) {
            attrs = {
                {"majorVersion", std::to_string(lxfml.version_major)},
                {"minorVersion", std::to_string(lxfml.version_minor)},
            };
        } else {
            attrs = {
                {"versionMajor", std::to_string(lxfml.version_major)},
                {"versionMinor", std::to_string(lxfml.version_minor)},
            };
            if (!lxfml.name.empty()) attrs.push_back({"name", lxfml.name});
        }
        xml.open(0, "LXFML", attrs);
    }
    if (lxfml.format == LxfmlFormat::Models) xml.blank_line();

    if (lxfml.has_meta) {
        xml.open(1, "Meta", {});
        if (!lxfml.meta.application.name.empty() ||
            lxfml.meta.application.version_major != 0 ||
            lxfml.meta.application.version_minor != 0) {
            xml.self_closed(2, "Application", {
                {"name", lxfml.meta.application.name},
                {"versionMajor", std::to_string(lxfml.meta.application.version_major)},
                {"versionMinor", std::to_string(lxfml.meta.application.version_minor)},
            });
        }
        if (!lxfml.meta.brand.empty()) {
            xml.self_closed(2, "Brand", {{"name", lxfml.meta.brand}});
        }
        if (!lxfml.meta.brick_set_version.empty()) {
            xml.self_closed(2, "BrickSet", {{"version", lxfml.meta.brick_set_version}});
        }
        xml.close(1, "Meta");
    }

    if (lxfml.has_cameras) {
        if (lxfml.cameras.empty()) {
            xml.self_closed(1, "Cameras", {});
        } else {
            xml.open(1, "Cameras", {});
            for (const auto& c : lxfml.cameras) write_camera(xml, 2, c);
            xml.close(1, "Cameras");
        }
    }

    switch (lxfml.format) {
        case LxfmlFormat::Bricks:
            write_bricks(xml, 1, lxfml);
            break;
        case LxfmlFormat::Scene:
            write_scene(xml, 1, lxfml.scene);
            break;
        case LxfmlFormat::Models:
            for (const auto& m : lxfml.v2_models) {
                if (&m == &lxfml.v2_models.front()) xml.open(1, "Models", {});
                write_v2_model(xml, 2, m);
            }
            if (!lxfml.v2_models.empty()) xml.close(1, "Models");
            else xml.self_closed(1, "Models", {});
            break;
        case LxfmlFormat::None:
            break;
    }

    if (lxfml.has_rigid_systems) {
        if (lxfml.rigid_systems.empty()) {
            xml.self_closed(1, "RigidSystems", {});
        } else {
            xml.open(1, "RigidSystems", {});
            for (const auto& rs : lxfml.rigid_systems) {
                if (rs.rigids.empty() && rs.joints.empty()) {
                    xml.self_closed(2, "RigidSystem", {});
                    continue;
                }
                xml.open(2, "RigidSystem", {});
                for (const auto& r : rs.rigids) write_rigid(xml, 3, r);
                for (const auto& j : rs.joints) write_joint(xml, 3, j);
                xml.close(2, "RigidSystem");
            }
            xml.close(1, "RigidSystems");
        }
    }

    if (lxfml.has_group_systems) {
        xml.open(1, "GroupSystems", {});
        for (const auto& groups : lxfml.group_systems) {
            if (groups.empty()) {
                xml.open_close_empty(2, "GroupSystem", {});
            } else {
                xml.open(2, "GroupSystem", {});
                for (const auto& g : groups) write_group_system_group(xml, 3, g);
                xml.close(2, "GroupSystem");
            }
        }
        xml.close(1, "GroupSystems");
    }

    // v2's fixed template has a blank line before BuildingInstructions (see the
    // Prepacks/trailing blank-line block below for the rest of that template).
    if (lxfml.format == LxfmlFormat::Models &&
        lxfml.building_instructions != LxfmlFile::ElementShape::Absent) {
        xml.blank_line();
    }

    switch (lxfml.building_instructions) {
        case LxfmlFile::ElementShape::Absent:
            break;
        case LxfmlFile::ElementShape::SelfClosed:
            xml.self_closed(1, "BuildingInstructions", {});
            break;
        case LxfmlFile::ElementShape::OpenEmpty:
            if (lxfml.building_instruction_data.empty()) {
                xml.open_close_empty(1, "BuildingInstructions", {});
            } else {
                xml.open(1, "BuildingInstructions", {});
                for (const auto& bi : lxfml.building_instruction_data) {
                    if (bi.steps.empty()) {
                        xml.self_closed(2, "BuildingInstruction", {});
                        continue;
                    }
                    xml.open(2, "BuildingInstruction", {});
                    for (const auto& s : bi.steps) write_step(xml, 3, s);
                    xml.close(2, "BuildingInstruction");
                }
                xml.close(1, "BuildingInstructions");
            }
            break;
    }

    if (lxfml.has_prepacks) {
        xml.blank_line();
        xml.self_closed(1, "Prepacks", {});
        xml.blank_line();
    }

    xml.close(0, "LXFML");
    return xml.str();
}

} // namespace lu::assets
