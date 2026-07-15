#include <gtest/gtest.h>
#include "lego/lxfml/lxfml_reader.h"
#include "lego/lxfml/lxfml_writer.h"
#include "lego/lxfml/lxfml_convert.h"

#include <cmath>
#include <string>
#include <vector>

using namespace lu::assets;

namespace {

std::vector<uint8_t> to_bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

const char* MINIMAL_LXFML = R"(<?xml version="1.0" encoding="UTF-8"?>
<LXFML versionMajor="4" versionMinor="0" name="TestModel">
  <Bricks>
    <Brick refID="0" designID="3005">
      <Part refID="0" designID="3005" materials="21">
        <Bone refID="0" transformation="1,0,0,0,1,0,0,0,1,10,20,30"/>
      </Part>
    </Brick>
  </Bricks>
</LXFML>
)";

const char* MULTI_BRICK_LXFML = R"(<?xml version="1.0" encoding="UTF-8"?>
<LXFML versionMajor="4" versionMinor="1" name="MultiBrick">
  <Bricks>
    <Brick refID="0" designID="3001">
      <Part refID="0" designID="3001" materials="1,5">
        <Bone refID="0" transformation="1,0,0,0,1,0,0,0,1,0,0,0"/>
      </Part>
    </Brick>
    <Brick refID="1" designID="3003">
      <Part refID="0" designID="3003" materials="26">
        <Bone refID="0" transformation="0,1,0,-1,0,0,0,0,1,5,10,15"/>
      </Part>
    </Brick>
  </Bricks>
</LXFML>
)";

} // anonymous namespace

TEST(LXFML, ParseMinimal) {
    auto data = to_bytes(MINIMAL_LXFML);
    auto lxfml = lxfml_parse(data);

    EXPECT_EQ(lxfml.version_major, 4);
    EXPECT_EQ(lxfml.version_minor, 0);
    EXPECT_EQ(lxfml.name, "TestModel");

    ASSERT_EQ(lxfml.bricks.size(), 1u);
    EXPECT_EQ(lxfml.bricks[0].ref_id, 0);
    EXPECT_EQ(lxfml.bricks[0].design_id, 3005);

    ASSERT_EQ(lxfml.bricks[0].parts.size(), 1u);
    EXPECT_EQ(lxfml.bricks[0].parts[0].design_id, 3005);
    EXPECT_EQ(lxfml.bricks[0].parts[0].materials, "21");

    ASSERT_EQ(lxfml.bricks[0].parts[0].bones.size(), 1u);
    const auto& bone = lxfml.bricks[0].parts[0].bones[0];
    EXPECT_EQ(bone.ref_id, 0);

    // Transform: 1,0,0, 0,1,0, 0,0,1, 10,20,30
    // Rotation part (identity)
    EXPECT_FLOAT_EQ(bone.transform[0], 1.0f);
    EXPECT_FLOAT_EQ(bone.transform[1], 0.0f);
    EXPECT_FLOAT_EQ(bone.transform[4], 1.0f);
    EXPECT_FLOAT_EQ(bone.transform[8], 1.0f);
    // Translation part
    EXPECT_FLOAT_EQ(bone.transform[9], 10.0f);
    EXPECT_FLOAT_EQ(bone.transform[10], 20.0f);
    EXPECT_FLOAT_EQ(bone.transform[11], 30.0f);
}

TEST(LXFML, ParseMultipleBricks) {
    auto data = to_bytes(MULTI_BRICK_LXFML);
    auto lxfml = lxfml_parse(data);

    ASSERT_EQ(lxfml.bricks.size(), 2u);
    EXPECT_EQ(lxfml.bricks[0].design_id, 3001);
    EXPECT_EQ(lxfml.bricks[1].design_id, 3003);
    EXPECT_EQ(lxfml.bricks[1].parts[0].materials, "26");

    // Second brick bone: rotation is 90-degree about Z
    const auto& bone = lxfml.bricks[1].parts[0].bones[0];
    EXPECT_FLOAT_EQ(bone.transform[0], 0.0f);
    EXPECT_FLOAT_EQ(bone.transform[1], 1.0f);
    EXPECT_FLOAT_EQ(bone.transform[3], -1.0f);
    // Translation
    EXPECT_FLOAT_EQ(bone.transform[9], 5.0f);
    EXPECT_FLOAT_EQ(bone.transform[10], 10.0f);
    EXPECT_FLOAT_EQ(bone.transform[11], 15.0f);
}

TEST(LXFML, EmptyDataThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(lxfml_parse(empty), LxfmlError);
}

TEST(LXFML, InvalidXmlThrows) {
    auto data = to_bytes("<not-lxfml></not-lxfml>");
    EXPECT_THROW(lxfml_parse(data), LxfmlError);
}

TEST(LXFML, GarbageDataThrows) {
    auto data = to_bytes("this is not xml at all {{{");
    EXPECT_THROW(lxfml_parse(data), LxfmlError);
}

TEST(LXFML, MetaParsed) {
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="5" versionMinor="0">
  <Meta>
    <Application name="LEGO Universe" versionMajor="0" versionMinor="3"/>
    <Brand name="LEGOUniverse"/>
    <BrickSet version="61"/>
  </Meta>
  <Bricks/>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    EXPECT_EQ(lxfml.meta.application.name, "LEGO Universe");
    EXPECT_EQ(lxfml.meta.application.version_major, 0);
    EXPECT_EQ(lxfml.meta.application.version_minor, 3);
    EXPECT_EQ(lxfml.meta.brand, "LEGOUniverse");
    EXPECT_EQ(lxfml.meta.brick_set_version, "61");
}

TEST(LXFML, CameraFormatBTransformation) {
    // Format B: 12-float transformation matrix
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="5" versionMinor="0">
  <Cameras>
    <Camera refID="0" fieldOfView="80" distance="44.0"
            transformation="0.7,0,-0.7, 0,1,0, 0.7,0,0.7, 10,20,30"/>
  </Cameras>
  <Bricks/>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    ASSERT_EQ(lxfml.cameras.size(), 1u);
    const auto& cam = lxfml.cameras[0];
    EXPECT_EQ(cam.ref_id, 0);
    EXPECT_FLOAT_EQ(cam.field_of_view, 80.0f);
    EXPECT_FLOAT_EQ(cam.distance, 44.0f);
    EXPECT_TRUE(cam.has_transformation);
    EXPECT_FLOAT_EQ(cam.transformation[9],  10.0f);
    EXPECT_FLOAT_EQ(cam.transformation[10], 20.0f);
    EXPECT_FLOAT_EQ(cam.transformation[11], 30.0f);
}

TEST(LXFML, CameraFormatAAngleAxis) {
    // Format A: separate angle-axis + look-at target
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="4" versionMinor="0">
  <Cameras>
    <Camera refID="0" fieldOfView="80" distance="69.28"
            angle="150.5" ax="-0.08" ay="-0.95" az="-0.30"
            tx="-29.1" ty="40.0" tz="-48.5"/>
  </Cameras>
  <Bricks/>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    ASSERT_EQ(lxfml.cameras.size(), 1u);
    const auto& cam = lxfml.cameras[0];
    EXPECT_FALSE(cam.has_transformation);
    EXPECT_FLOAT_EQ(cam.angle, 150.5f);
    EXPECT_NEAR(cam.ay, -0.95f, 1e-4f);
    EXPECT_NEAR(cam.tx, -29.1f, 1e-4f);
}

TEST(LXFML, BricksCameraRefAndDecoration) {
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="5" versionMinor="0">
  <Bricks cameraRef="0">
    <Brick refID="0" designID="93062">
      <Part refID="0" designID="93062" materials="26,1" decoration="0">
        <Bone refID="0" transformation="1,0,0,0,1,0,0,0,1,0,0,0"/>
      </Part>
    </Brick>
  </Bricks>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    EXPECT_EQ(lxfml.bricks_camera_ref, 0);
    ASSERT_EQ(lxfml.bricks.size(), 1u);
    EXPECT_EQ(lxfml.bricks[0].parts[0].materials, "26,1");
    EXPECT_EQ(lxfml.bricks[0].parts[0].decoration, "0");
}

TEST(LXFML, RigidSystemsParsed) {
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="5" versionMinor="0">
  <Bricks/>
  <RigidSystems>
    <RigidSystem>
      <Rigid refID="0" transformation="1,0,0,0,1,0,0,0,1,1,2,3" boneRefs="0,2"/>
      <Rigid refID="1" transformation="1,0,0,0,1,0,0,0,1,4,5,6" boneRefs="1"/>
      <Joint type="hinge">
        <RigidRef rigidRef="0" a="1,0,0" z="0,0,1" t="0,1,0"/>
        <RigidRef rigidRef="1" a="1,0,0" z="0,0,1" t="0,0,0"/>
      </Joint>
    </RigidSystem>
  </RigidSystems>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    ASSERT_EQ(lxfml.rigid_systems.size(), 1u);
    const auto& rs = lxfml.rigid_systems[0];
    ASSERT_EQ(rs.rigids.size(), 2u);
    EXPECT_EQ(rs.rigids[0].ref_id, 0);
    EXPECT_FLOAT_EQ(rs.rigids[0].transformation[9], 1.0f);
    EXPECT_EQ(rs.rigids[0].bone_refs.size(), 2u);
    EXPECT_EQ(rs.rigids[0].bone_refs[0], 0);
    EXPECT_EQ(rs.rigids[0].bone_refs[1], 2);
    ASSERT_EQ(rs.joints.size(), 1u);
    EXPECT_EQ(rs.joints[0].type, "hinge");
    ASSERT_EQ(rs.joints[0].rigid_refs.size(), 2u);
    EXPECT_EQ(rs.joints[0].rigid_refs[0].rigid_ref, 0);
    EXPECT_FLOAT_EQ(rs.joints[0].rigid_refs[0].t[1], 1.0f);
}

TEST(LXFML, SceneParsed) {
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="4" versionMinor="0">
  <Scene cameraRefID="0">
    <Model name="Duck">
      <Group refID="0" angle="90" ax="0" ay="1" az="0" tx="1" ty="2" tz="3">
        <Part refID="0" designID="3676" materialID="24"
              angle="0" ax="0" ay="1" az="0" tx="0" ty="-0.96" tz="0"/>
      </Group>
    </Model>
  </Scene>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    EXPECT_EQ(lxfml.scene.camera_ref_id, 0);
    ASSERT_EQ(lxfml.scene.models.size(), 1u);
    EXPECT_EQ(lxfml.scene.models[0].name, "Duck");
    ASSERT_EQ(lxfml.scene.models[0].groups.size(), 1u);
    const auto& g = lxfml.scene.models[0].groups[0];
    EXPECT_FLOAT_EQ(g.angle, 90.0f);
    EXPECT_FLOAT_EQ(g.tx, 1.0f);
    ASSERT_EQ(g.parts.size(), 1u);
    EXPECT_EQ(g.parts[0].design_id, 3676);
    EXPECT_EQ(g.parts[0].material_id, 24);
    EXPECT_NEAR(g.parts[0].ty, -0.96f, 1e-4f);
}

TEST(LXFML, GroupSystemsEmptyAndBuildingInstructionsSelfClosed) {
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="5" versionMinor="0">
  <Bricks/>
  <GroupSystems>
    <GroupSystem></GroupSystem>
    <GroupSystem></GroupSystem>
  </GroupSystems>
  <BuildingInstructions/>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    EXPECT_TRUE(lxfml.has_group_systems);
    ASSERT_EQ(lxfml.group_systems.size(), 2u);
    EXPECT_TRUE(lxfml.group_systems[0].empty());
    EXPECT_TRUE(lxfml.group_systems[1].empty());
    EXPECT_EQ(lxfml.building_instructions, LxfmlFile::ElementShape::SelfClosed);
}

TEST(LXFML, BuildingInstructionsOpenEmptyShape) {
    const char* xml = "<?xml version=\"1.0\"?>\n"
        "<LXFML versionMajor=\"5\" versionMinor=\"0\">\r\n"
        "  <Bricks/>\r\n"
        "  <BuildingInstructions>\r\n  </BuildingInstructions>\r\n"
        "</LXFML>";
    auto lxfml = lxfml_parse(to_bytes(xml));
    EXPECT_EQ(lxfml.building_instructions, LxfmlFile::ElementShape::OpenEmpty);
}

TEST(LXFML, BuildingInstructionsAbsent) {
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="5" versionMinor="0">
  <Bricks/>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    EXPECT_EQ(lxfml.building_instructions, LxfmlFile::ElementShape::Absent);
    EXPECT_FALSE(lxfml.has_group_systems);
}

TEST(LXFML, GroupSystemsWithRealGroupData) {
    // 82/1966 real client files have actual <Group> grouping data inside GroupSystems,
    // contrary to the old "always empty" assumption.
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="5" versionMinor="0">
  <Bricks/>
  <GroupSystems>
    <GroupSystem>
      <Group transformation="1,0,0,0,1,0,0,0,1,0,0,0" pivot="0,0,0" partRefs="1,2,3">
        <Group transformation="1,0,0,0,1,0,0,0,1,0,0,0" pivot="0,0,0" partRefs="4,5"/>
      </Group>
    </GroupSystem>
  </GroupSystems>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    ASSERT_EQ(lxfml.group_systems.size(), 1u);
    ASSERT_EQ(lxfml.group_systems[0].size(), 1u);
    const auto& g = lxfml.group_systems[0][0];
    EXPECT_FLOAT_EQ(g.transformation[9], 0.0f);
    ASSERT_EQ(g.part_refs.size(), 3u);
    EXPECT_EQ(g.part_refs[0], 1);
    EXPECT_EQ(g.part_refs[2], 3);
    ASSERT_EQ(g.children.size(), 1u);
    ASSERT_EQ(g.children[0].part_refs.size(), 2u);
    EXPECT_EQ(g.children[0].part_refs[1], 5);
}

TEST(LXFML, V2ModelsFormatParsed) {
    // v2 LDD-native format: rotPiv* quaternion, per-brick assembly grouping,
    // SubBrick material overrides. Alphabetical attribute order in real files.
    const char* xml = R"(<?xml version="1.0"?>
<LXFML majorVersion="2" minorVersion="1">
  <Models>
    <Model angle="0.00000000" ax="0.00000000" ay="0.00000000" az="1.00000000" modelName="TESTMODEL" rotPivW="1.00000000" rotPivX="0.00000000" rotPivY="0.00000000" rotPivZ="0.00000000" x="0.00000000" y="0.00000000" z="0.00000000">
      <Group angle="0.00000000" ax="0.00000000" ay="0.00000000" az="1.00000000" groupID="0" groupName="TestGroup" groupRefID="0" objectUniqueID="0" rotPivW="1.00000000" rotPivX="0.00000000" rotPivY="0.00000000" rotPivZ="0.00000000" x="0.00000000" y="0.00000000" z="0.00000000">
        <Brick angle="90.00000000" ax="0.00000000" ay="1.00000000" az="0.00000000" brickID="3666" brickName="test_brick" brickRefID="0" materialID="192" objectUniqueID="12234" rotPivW="1.00000000" rotPivX="0.00000000" rotPivY="0.00000000" rotPivZ="0.00000000" x="0.00000000" y="-0.32000000" z="0.00000000">
          <SubBrick materialID="192" surfaceID="0"/>
        </Brick>
      </Group>
    </Model>
  </Models>
  <BuildingInstructions/>
  <Prepacks/>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    EXPECT_EQ(lxfml.format, LxfmlFormat::Models);
    EXPECT_TRUE(lxfml.has_prepacks);
    ASSERT_EQ(lxfml.v2_models.size(), 1u);
    const auto& m = lxfml.v2_models[0];
    EXPECT_EQ(m.model_name, "TESTMODEL");
    ASSERT_EQ(m.groups.size(), 1u);
    EXPECT_EQ(m.groups[0].group_name, "TestGroup");
    ASSERT_EQ(m.groups[0].bricks.size(), 1u);
    const auto& b = m.groups[0].bricks[0];
    EXPECT_EQ(b.brick_id, 3666);
    EXPECT_EQ(b.brick_name, "test_brick");
    EXPECT_FALSE(b.has_assembly);
    ASSERT_EQ(b.sub_bricks.size(), 1u);
    EXPECT_EQ(b.sub_bricks[0].material_id, 192);

    // Flattened into the unified bricks vector too.
    ASSERT_EQ(lxfml.bricks.size(), 1u);
    EXPECT_EQ(lxfml.bricks[0].design_id, 3666);
}

TEST(LXFML, V2BrickAssemblyAttributes) {
    const char* xml = R"(<?xml version="1.0"?>
<LXFML majorVersion="2" minorVersion="1">
  <Models>
    <Model angle="0.00000000" ax="0.00000000" ay="0.00000000" az="1.00000000" modelName="M" rotPivW="1.00000000" rotPivX="0.00000000" rotPivY="0.00000000" rotPivZ="0.00000000" x="0.00000000" y="0.00000000" z="0.00000000">
      <Group angle="0.00000000" ax="0.00000000" ay="0.00000000" az="1.00000000" groupID="0" groupName="G" groupRefID="0" objectUniqueID="0" rotPivW="1.00000000" rotPivX="0.00000000" rotPivY="0.00000000" rotPivZ="0.00000000" x="0.00000000" y="0.00000000" z="0.00000000">
        <Brick angle="0.00000000" assemblyID="75535" assemblyRefID="75535" ax="0.00000000" ay="0.00000000" az="1.00000000" brickID="6220" brickName="b" brickRefID="0" materialID="192" objectUniqueID="1" rotPivW="1.00000000" rotPivX="0.00000000" rotPivY="0.00000000" rotPivZ="0.00000000" x="0.00000000" y="0.00000000" z="0.00000000"/>
      </Group>
    </Model>
  </Models>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    const auto& b = lxfml.v2_models[0].groups[0].bricks[0];
    EXPECT_TRUE(b.has_assembly);
    EXPECT_EQ(b.assembly_id, 75535);
    EXPECT_EQ(b.assembly_ref_id, 75535);
}

TEST(LXFML, FormatDetection) {
    auto lxfml_bricks = lxfml_parse(to_bytes(MINIMAL_LXFML));
    EXPECT_EQ(lxfml_bricks.format, LxfmlFormat::Bricks);

    const char* scene_xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="4" versionMinor="0">
  <Scene cameraRefID="0">
    <Model name="D"><Group refID="0" angle="0" ax="0" ay="1" az="0" tx="0" ty="0" tz="0"/></Model>
  </Scene>
</LXFML>)";
    auto lxfml_scene = lxfml_parse(to_bytes(scene_xml));
    EXPECT_EQ(lxfml_scene.format, LxfmlFormat::Scene);
}

// ── Upconversion to v5 ──────────────────────────────────────────────────────

TEST(LXFML, UpconvertSceneToV5) {
    // A v4 Scene: one Group-placed Part with an angle-axis transform.
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="4" versionMinor="0">
  <Scene cameraRefID="0">
    <Model name="Duck">
      <Group refID="0" angle="0" ax="0" ay="1" az="0" tx="0" ty="0" tz="0">
        <Part refID="0" designID="3676" materialID="24"
              angle="90" ax="0" ay="1" az="0" tx="0" ty="-0.96" tz="0"/>
      </Group>
    </Model>
  </Scene>
</LXFML>)";
    auto src = lxfml_parse(to_bytes(xml));
    ASSERT_EQ(src.format, LxfmlFormat::Scene);
    ASSERT_EQ(src.bricks.size(), 1u); // parser flattens Scene into the unified bricks

    auto v5 = lxfml_upconvert_to_v5(src);
    EXPECT_EQ(v5.format, LxfmlFormat::Bricks);
    EXPECT_EQ(v5.version_major, 5);
    EXPECT_EQ(v5.version_minor, 0);
    ASSERT_EQ(v5.bricks.size(), 1u);
    EXPECT_EQ(v5.bricks[0].design_id, 3676);
    ASSERT_EQ(v5.bricks[0].parts.size(), 1u);
    EXPECT_EQ(v5.bricks[0].parts[0].materials, "24"); // materialID int -> materials string

    // The v5 Bone matrix must exactly match the flattened source transform.
    ASSERT_EQ(v5.bricks[0].parts[0].bones.size(), 1u);
    for (int i = 0; i < 12; ++i) {
        EXPECT_FLOAT_EQ(v5.bricks[0].parts[0].bones[0].transform[i],
                        src.bricks[0].parts[0].bones[0].transform[i]);
    }

    // The converted file must serialize to valid, re-parseable, self-consistent v5.
    auto out = lxfml_write(v5);
    std::vector<uint8_t> out_bytes(out.begin(), out.end());
    auto reparsed = lxfml_parse(out_bytes);
    EXPECT_EQ(reparsed.format, LxfmlFormat::Bricks);
    EXPECT_EQ(reparsed.version_major, 5);
    ASSERT_EQ(reparsed.bricks.size(), 1u);
    EXPECT_EQ(reparsed.bricks[0].design_id, 3676);
    // And it must round-trip byte-perfectly (proves it's well-formed v5).
    auto out2 = lxfml_write(reparsed);
    EXPECT_EQ(std::vector<uint8_t>(out2.begin(), out2.end()), out_bytes);
}

TEST(LXFML, UpconvertBricksIsIdentityGeometry) {
    // Upconverting an already-v5 Bricks file keeps its bricks unchanged.
    auto src = lxfml_parse(to_bytes(MINIMAL_LXFML));
    ASSERT_EQ(src.format, LxfmlFormat::Bricks);
    auto v5 = lxfml_upconvert_to_v5(src);
    EXPECT_EQ(v5.format, LxfmlFormat::Bricks);
    EXPECT_EQ(v5.version_major, 5);
    ASSERT_EQ(v5.bricks.size(), src.bricks.size());
    for (size_t i = 0; i < src.bricks.size(); ++i) {
        EXPECT_EQ(v5.bricks[i].design_id, src.bricks[i].design_id);
    }
}

TEST(LXFML, UpconvertRejectsFormatNone) {
    // A meta-only file with no placement element cannot be upconverted.
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="5" versionMinor="0">
  <Meta><Brand name="LEGOUniverse"/></Meta>
</LXFML>)";
    auto src = lxfml_parse(to_bytes(xml));
    ASSERT_EQ(src.format, LxfmlFormat::None);
    EXPECT_THROW(lxfml_upconvert_to_v5(src), LxfmlError);
}
