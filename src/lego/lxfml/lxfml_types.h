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
    // 4x3 transformation matrix stored as 12 comma-separated floats
    // [r00,r01,r02, r10,r11,r12, r20,r21,r22, tx,ty,tz]
    float transform[12] = {1,0,0, 0,1,0, 0,0,1, 0,0,0};
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
    std::vector<LxfmlPart> parts;
};

// ── RigidSystems (physics articulation) ───────────────────────────────────────

// Reference to a Rigid element within a Joint constraint.
// a = constraint axis (normalized vec3), z = constraint z-axis,
// t = constraint anchor position in world space.
struct LxfmlRigidRef {
    int32_t rigid_ref = 0;
    float a[3] = {};    // constraint primary axis
    float z[3] = {};    // constraint z-axis
    float t[3] = {};    // anchor position
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
// boneRefs lists the refID values of Bone elements that make up this rigid.
struct LxfmlRigid {
    int32_t ref_id = 0;
    float transformation[12] = {1,0,0, 0,1,0, 0,0,1, 0,0,0};
    std::vector<int32_t> bone_refs;  // parsed from "0,1,2" boneRefs attribute
};

// One articulated rigid body system — a set of rigids connected by joints.
struct LxfmlRigidSystem {
    std::vector<LxfmlRigid> rigids;
    std::vector<LxfmlJoint> joints;
};

// ── Scene (v4 LDD placement format) ──────────────────────────────────────────

// A brick placed in the Scene hierarchy. Uses angle-axis + translation,
// not a Bone transformation matrix.
struct LxfmlScenePart {
    int32_t ref_id = 0;
    std::string name;         // optional
    int32_t design_id = 0;
    int32_t material_id = 0;
    std::string item_nos;     // optional stock number string
    float angle = 0.0f;       // rotation angle (degrees)
    float ax = 0.0f, ay = 1.0f, az = 0.0f;  // rotation axis
    float tx = 0.0f, ty = 0.0f, tz = 0.0f;  // translation
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
};

struct LxfmlSceneModel {
    std::string name;
    std::vector<LxfmlSceneGroup> groups;
};

struct LxfmlScene {
    int32_t camera_ref_id = -1;
    std::vector<LxfmlSceneModel> models;
};

// ── File root ─────────────────────────────────────────────────────────────────

struct LxfmlFile {
    int32_t version_major = 0;
    int32_t version_minor = 0;
    std::string name;

    LxfmlMeta meta;
    std::vector<LxfmlCamera> cameras;

    // v5 LU brick placement
    int32_t bricks_camera_ref = -1;  // cameraRef attribute on <Bricks>
    std::vector<LxfmlBrick> bricks;

    // v4 LDD scene placement (absent in most LU v5 files)
    LxfmlScene scene;

    // Articulated rigid body systems (present in ~1530/2437 client files)
    std::vector<LxfmlRigidSystem> rigid_systems;

    // GroupSystems: always empty elements in all client files
    uint32_t group_system_count = 0;

    // BuildingInstructions: always empty in all client files
    bool has_building_instructions = false;
};
} // namespace lu::assets
