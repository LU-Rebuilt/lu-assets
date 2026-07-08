#pragma once
#include "gamebryo/settings/settings_types.h"

#include <vector>

namespace lu::assets {

// Serialize a SettingsFile back to the NiKFMTool compiled-binary .settings format.
// Byte-identical to the source file for any SettingsFile produced by settings_parse:
// every header field (including the unreliable declared_sequence_count) and the raw
// footer bytes are written back verbatim.
std::vector<uint8_t> settings_write(const SettingsFile& settings);

} // namespace lu::assets
