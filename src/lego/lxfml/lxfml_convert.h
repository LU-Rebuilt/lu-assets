#pragma once
#include "lego/lxfml/lxfml_types.h"

namespace lu::assets {

// Upconvert a parsed LXFML file to the modern v5 "Bricks" format
// (versionMajor="5"), the layout the LU client and current LDD use. Accepts a file
// in any format:
//   - Bricks (v5): returned essentially unchanged (already v5).
//   - Scene (v4): each Part's angle-axis placement is converted to a v5 Bone 4x3
//     transformation matrix.
//   - Models (v2): each Brick's placement (v2 stores a redundant angle-axis form
//     alongside its rotation-pivot quaternion) is converted the same way.
//
// The geometry conversion reuses the exact angle-axis -> matrix math the parser
// already applies when it flattens v2/v4 files into the unified `bricks` vector for
// rendering (see lxfml_reader.cpp), so no new 3D math is introduced here.
//
// This is a one-directional, intentionally lossy transform: v4/v2 carry structure a
// flat v5 brick list can't represent (Scene Groups/Joints, v2 assembly grouping and
// SubBrick overrides, GroupSystems, RigidSystems). Those are dropped, matching how the
// flattened `bricks` view already discards them. What's kept is the placed-brick
// geometry (design IDs, materials, decorations, transforms) plus Meta/Cameras.
//
// Returns a new LxfmlFile with format == Bricks. Throws LxfmlError if the input has
// no placement data to convert (format == None).
LxfmlFile lxfml_upconvert_to_v5(const LxfmlFile& src);

} // namespace lu::assets
