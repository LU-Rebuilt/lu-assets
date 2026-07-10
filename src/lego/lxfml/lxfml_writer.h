#pragma once
#include "lego/lxfml/lxfml_types.h"

#include <string>

namespace lu::assets {

// Serialize an LxfmlFile back to LXFML XML text.
//
// Byte-perfect for the overwhelming majority of real content: v5 Bricks and v4 Scene
// float attributes are formatted with 17 significant digits and round-half-away-from-
// zero tie-breaking, matching real files exactly except in the rare case (~0.05% of
// values in the real corpus) where the true value sits precisely on a decimal tie at
// the 17th digit — LEGO Digital Designer/LU's own tool (almost certainly .NET) breaks
// those ties using its internal Grisu3/Dragon4 algorithm's implementation-specific
// state, which isn't derivable without running that exact algorithm; our output may
// differ from the original by 1 in the last decimal digit in those cases (the value
// itself is unchanged — both strings parse back to the identical double).
//
// v2 Models format uses fixed 8-decimal-place float formatting and alphabetically-
// sorted attributes, both verified byte-exact against all 6 real v2 files.
std::string lxfml_write(const LxfmlFile& lxfml);

} // namespace lu::assets
