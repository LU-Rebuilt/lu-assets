#include <gtest/gtest.h>
#include "lego/lxfml/lxfml_reader.h"

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

TEST(LXFML, GroupSystemsAndBuildingInstructions) {
    const char* xml = R"(<?xml version="1.0"?>
<LXFML versionMajor="5" versionMinor="0">
  <Bricks/>
  <GroupSystems>
    <GroupSystem/>
    <GroupSystem/>
  </GroupSystems>
  <BuildingInstructions/>
</LXFML>)";
    auto lxfml = lxfml_parse(to_bytes(xml));
    EXPECT_EQ(lxfml.group_system_count, 2u);
    EXPECT_TRUE(lxfml.has_building_instructions);
}
