#pragma once

#include "netdevil/zone/luz/luz_types.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// References:
//   - lu-toolbox (github.com/Squareville/lu-toolbox) — LXFML import pipeline and transform conventions
//   - NexusDashboard (github.com/DarkflameUniverse/NexusDashboard) — LXFML parsing reference

// LXFML (LEGO XML Format Markup Language) brick model parser.
// 2,444 .lxfml files in the client's BrickModels/ directory.
// Standard XML format — parsed with pugixml.
//
// Top-level structure:
//   <LXFML versionMajor="4|5" versionMinor="..." name="...">
//     <Meta>
//       <Application name="..." versionMajor="..." versionMinor="..."/>
//       <Brand name="..."/>
//       <BrickSet version="..."/>
//     </Meta>
//     <Cameras>
//       <!-- Format A (v4 LDD): angle-axis + target -->
//       <Camera refID="..." fieldOfView="..." distance="..." angle="..."
//               ax="..." ay="..." az="..." tx="..." ty="..." tz="..."/>
//       <!-- Format B (v5 LDDExt/LU): 4x3 matrix, same layout as Bone -->
//       <Camera refID="..." fieldOfView="..." distance="..."
//               transformation="r00,...,r22,tx,ty,tz"/>
//     </Cameras>
//     <!-- v5 LU: Bricks/Part/Bone hierarchy -->
//     <Bricks cameraRef="...">
//       <Brick refID="..." designID="...">
//         <Part refID="..." designID="..." materials="..." decoration="...">
//           <Bone refID="..." transformation="12 floats"/>
//         </Part>
//       </Brick>
//     </Bricks>
//     <!-- v4 LDD: Scene placement hierarchy -->
//     <Scene cameraRefID="...">
//       <Model name="...">
//         <Group refID="..." [name] angle ax ay az tx ty tz>
//           <Part refID="..." [name] designID materialID angle ax ay az tx ty tz/>
//           <Joint type="hinge">
//             <GroupRef groupRef="..." ax ay az zx zy zz tx ty tz/>
//           </Joint>
//         </Group>
//       </Model>
//     </Scene>
//     <!-- RigidSystems: articulated rigid body groups with physics joints -->
//     <RigidSystems>
//       <RigidSystem>
//         <Rigid refID="..." transformation="12 floats" boneRefs="0,1,..."/>
//         <Joint type="hinge">
//           <RigidRef rigidRef="..." a="x,y,z" z="x,y,z" t="x,y,z"/>
//         </Joint>
//       </RigidSystem>
//     </RigidSystems>
//     <!-- GroupSystems: always empty in all client files -->
//     <GroupSystems><GroupSystem/></GroupSystems>
//     <!-- BuildingInstructions: always empty in client files -->
//     <BuildingInstructions/>
//   </LXFML>

struct LxfmlError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ── Meta ──────────────────────────────────────────────────────────────────────

struct LxfmlApplication {
    std::string name;
    int32_t version_major = 0;
    int32_t version_minor = 0;
};

struct LxfmlMeta {
    LxfmlApplication application;
    std::string brand;              // <Brand name="..."/>
    std::string brick_set_version;  // <BrickSet version="..."/>
};

// ── Cameras ───────────────────────────────────────────────────────────────────

struct LxfmlCamera {
    int32_t ref_id = 0;
    std::string name;           // optional attribute
    float field_of_view = 80.0f;
    float distance = 0.0f;

    // Format B (v5): 4x3 matrix [r00,r01,r02, r10,r11,r12, r20,r21,r22, tx,ty,tz]
    // Same layout as LxfmlBone::transform. Present when transformation="" attr exists.
    bool has_transformation = false;
    float transformation[12] = {1,0,0, 0,1,0, 0,0,1, 0,0,0};

    // Format A (v4): separate angle-axis rotation + look-at target.
    // angle = rotation angle (degrees), ax/ay/az = rotation axis (normalized),
    // tx/ty/tz = camera target (look-at) position.
    float angle = 0.0f;
    float ax = 0.0f, ay = 1.0f, az = 0.0f;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
};

// ── Bricks (main LU placement format) ────────────────────────────────────────

struct LxfmlBone {
    int32_t ref_id = 0;
    // Most Bone elements use a 4x3 transformation matrix (12 comma-separated floats:
    // [r00,r01,r02, r10,r11,r12, r20,r21,r22, tx,ty,tz]), but a real minority
    // (796/227322 in the client corpus) instead use separate angle-axis + translation
    // attributes, matching Camera's two formats. Exactly one of the two is present.
    bool has_transformation = true;
    float transform[12] = {1,0,0, 0,1,0, 0,0,1, 0,0,0};
    float angle = 0.0f;
    float ax = 0.0f, ay = 1.0f, az = 0.0f;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
};

struct LxfmlPart {
    int32_t ref_id = 0;
    int32_t design_id = 0;
    std::string materials;       // comma-separated material IDs, e.g. "21" or "26,1"
    std::string decoration;      // optional: comma-separated decoration IDs
    std::vector<LxfmlBone> bones;
};

struct LxfmlBrick {
    int32_t ref_id = 0;
    int32_t design_id = 0;
    bool has_item_nos = false;
    std::string item_nos;
    std::vector<LxfmlPart> parts;
};

// ── RigidSystems (physics articulation) ───────────────────────────────────────

// Reference to a Rigid element within a Joint constraint. Most use the compact
// a/z/t vec3 form; a real minority (96/30732 in the client corpus) instead use the
// same 9-scalar ax/ay/az/zx/zy/zz/tx/ty/tz form as GroupRef. Exactly one is present.
struct LxfmlRigidRef {
    int32_t rigid_ref = 0;
    bool has_vec3_form = true;
    float a[3] = {};    // constraint primary axis (vec3 form)
    float z[3] = {};    // constraint z-axis (vec3 form)
    float t[3] = {};    // anchor position (vec3 form)
    float ax = 0.0f, ay = 0.0f, az = 0.0f;  // constraint axis (scalar form)
    float zx = 0.0f, zy = 0.0f, zz = 1.0f;  // constraint z-axis (scalar form)
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;  // anchor position (scalar form)
};

// Reference to a Scene Group within a Joint constraint.
struct LxfmlGroupRef {
    int32_t group_ref = 0;
    float ax = 0.0f, ay = 0.0f, az = 0.0f;  // constraint axis
    float zx = 0.0f, zy = 0.0f, zz = 1.0f;  // constraint z-axis
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;  // anchor position
};

// Physics joint between two rigid bodies or scene groups.
// type="hinge" is the only observed type in client files.
struct LxfmlJoint {
    std::string type;
    std::vector<LxfmlRigidRef> rigid_refs;   // present in RigidSystem joints
    std::vector<LxfmlGroupRef> group_refs;   // present in Scene/GroupSystem joints
};

// A rigid body consisting of one or more bones sharing a transformation.
// boneRefs lists the refID values of Bone elements that make up this rigid. Most use
// a transformation matrix; a real minority (347/15601 in the client corpus) instead
// use angle-axis + translation, matching Bone's two formats.
struct LxfmlRigid {
    int32_t ref_id = 0;
    bool has_transformation = true;
    float transformation[12] = {1,0,0, 0,1,0, 0,0,1, 0,0,0};
    float angle = 0.0f;
    float ax = 0.0f, ay = 1.0f, az = 0.0f;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    std::vector<int32_t> bone_refs;  // parsed from "0,1,2" boneRefs attribute
};

// One articulated rigid body system — a set of rigids connected by joints.
struct LxfmlRigidSystem {
    std::vector<LxfmlRigid> rigids;
    std::vector<LxfmlJoint> joints;
};

// ── GroupSystems (real data, contrary to the old "always empty" assumption) ─────
//
// A logical grouping of parts by reference, with its own transform/pivot — distinct
// from both LxfmlSceneGroup (v4) and LxfmlV2Group (v2); this is the <Group> element
// found inside <GroupSystems><GroupSystem>. Recursively nestable (a Group's children
// can themselves be Group elements — real files exercise this). Present with real
// data in 82/1966 real client files; every other file has either no GroupSystems
// element or exactly one empty <GroupSystem> child (see LxfmlFile::group_system_count).
struct LxfmlGroupSystemGroup {
    float transformation[12] = {1,0,0, 0,1,0, 0,0,1, 0,0,0};
    float pivot[3] = {0,0,0};
    std::vector<int32_t> part_refs;   // parsed from "18,22,0,..." partRefs attribute
    std::vector<LxfmlGroupSystemGroup> children; // nested Group elements
};

// ── Models (v2 LDD-native placement format, 6/1966 real client files) ───────
//
// A wholly distinct sub-format from v4 Scene / v5 Bricks: uses rotation-pivot
// attributes (rotPivW/X/Y/Z, a quaternion) instead of angle-axis or a matrix, has
// per-brick assembly grouping and per-part SubBrick material overrides, and formats
// every float as fixed 8-decimal-place ("%.8f"-style) rather than the 17-significant-
// digit scheme v4/v5 use. Real files show attributes emitted in strict alphabetical
// order (confirmed against all 6 real samples, including optional attrs like
// assemblyID/assemblyRefID slotting into their alphabetical position) — so the writer
// doesn't need a hardcoded per-element attribute order for this format, just "sort
// present attributes alphabetically."

struct LxfmlSubBrick {
    int32_t material_id = 0;
    int32_t surface_id = 0;
};

// Note: all float fields on LxfmlV2Brick/LxfmlV2Group/LxfmlV2Model below are DOUBLE
// precision, unlike every other LXFML struct (which uses float32 matching the file
// format's 17-significant-digit round-trip scheme). Verified against real v2 files:
// values like "rotPivX=\"-6.40000001\"" only round-trip exactly as a double — the
// nearest float32 is a genuinely different value ("-6.400000095367431640625" exactly,
// vs the file's exact "-6.40000001000000029..."), so v2's fixed-8-decimal-place format
// must be authored from double-precision floats, not float32 like v4/v5.
struct LxfmlV2Brick {
    double angle = 0.0;
    double ax = 0.0, ay = 0.0, az = 0.0;
    bool has_assembly = false;
    int32_t assembly_id = 0;
    int32_t assembly_ref_id = 0;
    int32_t brick_id = 0;
    std::string brick_name;
    int32_t brick_ref_id = 0;
    bool has_material_id = true; // rare real files omit this attribute entirely
    int32_t material_id = 0;
    int32_t object_unique_id = 0;
    double rot_piv_w = 1.0, rot_piv_x = 0.0, rot_piv_y = 0.0, rot_piv_z = 0.0;
    double x = 0.0, y = 0.0, z = 0.0;
    std::vector<LxfmlSubBrick> sub_bricks;
};

// Records the interleaving of Brick/Group children within a v2 Group or Model — real
// files freely mix "some Bricks, then a Group, then more Bricks" siblings in a single
// parent, so a plain "all bricks then all groups" split (like every other LXFML variant
// uses) loses real ordering information needed for byte-perfect write-back.
struct LxfmlV2ChildRef {
    enum class Kind { Brick, Group } kind;
    size_t index; // index into the parent's bricks or groups vector
};

struct LxfmlV2Group {
    double angle = 0.0;
    double ax = 0.0, ay = 0.0, az = 0.0;
    bool has_assembly = false;
    int32_t assembly_id = 0;
    int32_t group_id = 0;
    std::string group_name;
    int32_t group_ref_id = 0;
    int32_t object_unique_id = 0;
    double rot_piv_w = 1.0, rot_piv_x = 0.0, rot_piv_y = 0.0, rot_piv_z = 0.0;
    double x = 0.0, y = 0.0, z = 0.0;
    std::vector<LxfmlV2Brick> bricks;
    std::vector<LxfmlV2Group> groups; // Groups can nest inside Groups
    std::vector<LxfmlV2ChildRef> child_order; // real document order of the above two
};

struct LxfmlV2Model {
    double angle = 0.0;
    double ax = 0.0, ay = 0.0, az = 0.0;
    std::string model_name;
    double rot_piv_w = 1.0, rot_piv_x = 0.0, rot_piv_y = 0.0, rot_piv_z = 0.0;
    double x = 0.0, y = 0.0, z = 0.0;
    std::vector<LxfmlV2Group> groups;
    // A real minority of v2 files (1/6 in the client corpus) has Brick elements as
    // DIRECT children of Model, with no intermediate Group wrapper at all.
    std::vector<LxfmlV2Brick> bricks;
    std::vector<LxfmlV2ChildRef> child_order; // real document order of the above two
};

// ── Scene (v4 LDD placement format) ──────────────────────────────────────────

// Per-surface material override on a Scene Part. Real but rare (24 occurrences
// across the client corpus).
struct LxfmlSubMaterial {
    int32_t surface_id = 0;
    int32_t material_id = 0;
};

// Per-surface decoration override on a Scene Part. Real but rare (9 occurrences
// across the client corpus).
struct LxfmlDecoration {
    int32_t surface_id = 0;
    int32_t decoration_id = 0;
};

// A brick placed in the Scene hierarchy. Uses angle-axis + translation,
// not a Bone transformation matrix.
struct LxfmlScenePart {
    int32_t ref_id = 0;
    std::string name;         // optional
    int32_t design_id = 0;
    int32_t material_id = 0;
    std::string item_nos;     // optional stock number string
    bool has_assembly = false;  // real but uncommon: assembly grouping, same idea as
    int32_t assembly_id = 0;    // v2's Brick assemblyID/assemblyRefID (not observed to
    int32_t assembly_ref_id = 0; // co-occur with item_nos in the real corpus)
    float angle = 0.0f;       // rotation angle (degrees)
    float ax = 0.0f, ay = 1.0f, az = 0.0f;  // rotation axis
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;  // translation
    std::vector<LxfmlSubMaterial> sub_materials;
    std::vector<LxfmlDecoration> decorations;

    // When there are no sub_materials/decorations, real files write this element as
    // either self-closed (89.3% of childless instances) or open-tag/close-tag-on-next-
    // line with no real content (10.7%) — a genuine per-instance choice, not something
    // derivable from the part's other fields. Ignored when sub_materials/decorations
    // is non-empty (always open in that case).
    bool empty_self_closed = true;
};

// A logical group of parts in the Scene with a shared transform.
struct LxfmlSceneGroup {
    int32_t ref_id = 0;
    std::string name;
    float angle = 0.0f;
    float ax = 0.0f, ay = 1.0f, az = 0.0f;
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;
    std::vector<LxfmlScenePart> parts;
    std::vector<LxfmlJoint> joints;

    // Same per-instance self-closed-vs-open-empty choice as LxfmlScenePart::
    // empty_self_closed — applies when parts and joints are both empty.
    bool empty_self_closed = true;
};

struct LxfmlSceneModel {
    std::string name;
    std::vector<LxfmlSceneGroup> groups;
    std::vector<LxfmlJoint> joints; // Joint elements at the Model level (sibling of
                                     // Group, not nested inside one) — real files use
                                     // both placements.
};

struct LxfmlScene {
    bool has_camera_ref_id = true; // real files omit this attribute 311/715 of the time
    int32_t camera_ref_id = -1;
    std::vector<LxfmlSceneModel> models;
};

// ── BuildingInstructions content (real but very rare: 1/1966 real client files) ──
//
// Contrary to the old "always empty" assumption, BuildingInstructions can contain a
// real, recursively-nested build sequence: <BuildingInstructions><BuildingInstruction>
// <Step name=... [cameraRefID=...]><PartRef partRefID=.../>...(nested Step)...</Step>.

struct LxfmlPartRef {
    int32_t part_ref_id = 0;
};

struct LxfmlStep {
    std::string name;
    bool has_camera_ref_id = false;
    int32_t camera_ref_id = 0;
    std::vector<LxfmlPartRef> part_refs;
    std::vector<LxfmlStep> steps; // nested sub-steps
};

struct LxfmlBuildingInstruction {
    std::vector<LxfmlStep> steps;
};

// ── File root ─────────────────────────────────────────────────────────────────
//
// Presence of each optional top-level element is tracked explicitly (has_* flags)
// rather than inferred from "is the vector non-empty", since e.g. a real v5 file could
// legitimately have zero cameras yet still contain a (self-closing) <Cameras/> element
// — the writer needs to know whether to emit the element at all, separately from how
// many children it has.

// Which top-level placement format this file uses. Exactly one is ever present in a
// real file (1244 Bricks / 715 Scene / 6 Models out of 1966 real client files) — the
// writer picks the matching serializer based on this rather than re-deriving it from
// which vector happens to be non-empty.
enum class LxfmlFormat {
    Bricks,   // v5: <Bricks><Brick><Part><Bone transformation="matrix"/>
    Scene,    // v4: <Scene><Model><Group><Part angle=... ax=... .../>
    Models,   // v2: <Models><Model><Group><Brick rotPivW=... .../>
    None,     // no placement element at all (seen in a handful of meta-only files)
};

struct LxfmlFile {
    int32_t version_major = 0;
    int32_t version_minor = 0;
    std::string name;

    // 250/2429 real files use "\r\r\n" as the line ending EVERYWHERE in the file
    // (not just after the declaration); the other 2179 use plain "\r\n" — a clean,
    // whole-file, binary split with zero mixed files, evidently from a distinct save
    // path in whatever wrote these particular files (all still Windows-newline-based,
    // just with an extra stray \r). Preserved for byte-perfect round-trip.
    bool uses_double_cr_line_endings = false;

    LxfmlFormat format = LxfmlFormat::None;

    bool has_meta = false;
    LxfmlMeta meta;

    bool has_cameras = false;
    std::vector<LxfmlCamera> cameras;

    // v5 LU brick placement (format == Bricks)
    bool has_bricks_camera_ref = false;
    int32_t bricks_camera_ref = -1;  // cameraRef attribute on <Bricks>
    std::vector<LxfmlBrick> bricks;

    // v4 LDD scene placement (format == Scene)
    LxfmlScene scene;

    // v2 LDD-native placement (format == Models)
    std::vector<LxfmlV2Model> v2_models;

    bool has_rigid_systems = false;
    std::vector<LxfmlRigidSystem> rigid_systems;

    // GroupSystems: one entry per <GroupSystem> child (always exactly 1 in every real
    // file, but modeled as a vector rather than assumed). Each GroupSystem holds zero
    // or more top-level <Group> elements (see LxfmlGroupSystemGroup) — real files are
    // usually empty here (contrary to the format's old "always empty" assumption; see
    // LxfmlGroupSystemGroup's doc comment), but 82/1966 have real grouping data.
    bool has_group_systems = false;
    std::vector<std::vector<LxfmlGroupSystemGroup>> group_systems;

    // BuildingInstructions: present in 3 shapes in real files — self-closed
    // (<BuildingInstructions/>), open-empty with whitespace, or fully absent. The vast
    // majority of real files (1965/1966) are one of these 3 shapes with no real content;
    // exactly 1 real file has genuine <BuildingInstruction><Step>... data (see
    // LxfmlBuildingInstruction), in which case the shape is OpenEmpty and
    // building_instruction_data holds the parsed content.
    enum class ElementShape { Absent, SelfClosed, OpenEmpty };
    ElementShape building_instructions = ElementShape::Absent;
    std::vector<LxfmlBuildingInstruction> building_instruction_data;

    // Prepacks: v2-format files only, always self-closed and empty in real samples.
    bool has_prepacks = false;
};
} // namespace lu::assets
