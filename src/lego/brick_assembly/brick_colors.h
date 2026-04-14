#pragma once
// brick_colors.h — LEGO material color ID -> RGBA lookup.
// Color values sourced from lu-toolbox materials database (linear color space).
// Covers opaque, transparent, metallic, and glow material categories.
//
// References:
//   - lu-toolbox (github.com/Squareville/lu-toolbox) — materials database (color ID -> RGBA values)

#include <cstdint>

namespace lu::assets {

struct BrickColor {
    float r, g, b, a;
};

// Look up a LEGO material color by its integer ID.
// Returns black (ID 26) for unknown IDs.
BrickColor brick_color_lookup(int material_id);

} // namespace lu::assets
