#pragma once

#include "netdevil/common/ldf/ldf_types.h"
#include "common/binary_writer/binary_writer.h"

#include <string>
#include <vector>

namespace lu::assets {

// Serialize LDF entries back to the text format used in LVL config strings.
// Uses raw_value to preserve original formatting for faithful round-trips.
std::string ldf_write_text(const std::vector<LdfEntry>& entries);

// Write binary LDF config (LUZ waypoint format): u32 count + (u1_wstr key + u1_wstr value) pairs.
void ldf_write_binary(BinaryWriter& w, const LdfConfig& config);

} // namespace lu::assets
