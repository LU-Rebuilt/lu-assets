#include "lego/lxfml/lxfml_convert.h"

namespace lu::assets {

LxfmlFile lxfml_upconvert_to_v5(const LxfmlFile& src) {
    if (src.format == LxfmlFormat::None) {
        throw LxfmlError("LXFML upconvert: source has no placement data (format == None)");
    }

    LxfmlFile out;

    // v5 Bricks uses versionMajor="5" versionMinor="0" (v4 Scene is "4"/"0",
    // v2 Models is majorVersion="2"/minorVersion="1"). Force the v5 header.
    out.version_major = 5;
    out.version_minor = 0;
    out.name = src.name;
    // Use the plain "\r\n" line ending — the double-CR variant only appears on a
    // specific subset of already-v5 files and isn't a property we want to synthesize.
    out.uses_double_cr_line_endings = false;

    out.format = LxfmlFormat::Bricks;

    // Metadata carries over unchanged.
    out.has_meta = src.has_meta;
    out.meta = src.meta;
    out.has_cameras = src.has_cameras;
    out.cameras = src.cameras;

    // The parser already flattens every format (Scene/Models as well as Bricks) into
    // the unified `bricks` vector using the same angle-axis -> 4x3-matrix math a real
    // v5 Bone uses, so the placed-brick geometry is ready to emit as-is.
    out.bricks = src.bricks;

    // A camera reference on the <Bricks> element only exists in the v5 form; carry it
    // through when the source was already v5, otherwise leave it unset.
    if (src.format == LxfmlFormat::Bricks) {
        out.has_bricks_camera_ref = src.has_bricks_camera_ref;
        out.bricks_camera_ref = src.bricks_camera_ref;
    }

    // Everything else (Scene Groups/Joints, v2 assembly grouping / SubBrick overrides,
    // GroupSystems, RigidSystems, BuildingInstructions, Prepacks) has no representation
    // in a flat v5 brick list and is intentionally dropped — the same information the
    // flattened `bricks` view already discards. out's has_* flags stay false by default.

    return out;
}

} // namespace lu::assets
