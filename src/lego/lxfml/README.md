# LXFML (LEGO XML Format Markup Language) Format

**Extension:** `.lxfml`
**Used by:** The LEGO Universe client (2,444 files in BrickModels/) to define brick model assemblies with part placement, materials, and physics articulation

## Overview

LXFML is LEGO's standard XML format for brick model definitions. LU uses version 5 (LDDExtended) with a Bricks/Part/Bone hierarchy for placement, while older version 4 (LDD) uses a Scene/Group/Part hierarchy. Files include camera definitions, rigid body systems for physics joints, and 4x3 transformation matrices.

## XML Structure

```xml
<LXFML versionMajor="5" versionMinor="0" name="model_name">
  <Meta>
    <Application name="LDDExtended" versionMajor="4" versionMinor="3"/>
    <Brand name="LU"/>
    <BrickSet version="1564.2"/>
  </Meta>
  <Cameras>
    <Camera refID="0" fieldOfView="80" distance="50"
            transformation="1,0,0,0,1,0,0,0,1,0,10,20"/>
  </Cameras>
  <Bricks cameraRef="0">
    <Brick refID="0" designID="3005">
      <Part refID="0" designID="3005" materials="21" decoration="">
        <Bone refID="0" transformation="1,0,0,0,1,0,0,0,1,0,0,0"/>
      </Part>
    </Brick>
  </Bricks>
  <RigidSystems>
    <RigidSystem>
      <Rigid refID="0" transformation="1,0,0,0,1,0,0,0,1,0,0,0" boneRefs="0,1"/>
      <Joint type="hinge">
        <RigidRef rigidRef="0" a="0,1,0" z="0,0,1" t="0,0.5,0"/>
      </Joint>
    </RigidSystem>
  </RigidSystems>
</LXFML>
```

### Key Elements

- **Bone transformation**: 12 comma-separated floats [r00,r01,r02, r10,r11,r12, r20,r21,r22, tx,ty,tz] (4x3 row-major matrix)
- **Camera Format A** (v4): angle-axis rotation (angle, ax, ay, az) + target (tx, ty, tz)
- **Camera Format B** (v5): 12-float transformation matrix (same as Bone)
- **Materials**: comma-separated LEGO material IDs (e.g. "21" or "26,1")
- **RigidRef**: constraint axis (a), z-axis (z), and anchor position (t) as 3-float vectors
- **Joint type**: "hinge" is the only observed type in client files

## Key Details

- Standard XML format parsed with pugixml
- Version 5 (LU) uses Bricks/Part/Bone hierarchy; version 4 uses Scene/Group/Part
- RigidSystems present in approximately 1,530 of 2,437 client files
- GroupSystems and BuildingInstructions elements are always empty in client files
- Design IDs reference the LEGO part catalog

## References

- lu-toolbox (github.com/Squareville/lu-toolbox) — LXFML import pipeline
- NexusDashboard (github.com/DarkflameUniverse/NexusDashboard) — LXFML parsing reference
