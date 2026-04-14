#pragma once
#include "gamebryo/settings/settings_types.h"

namespace lu::assets {

// Parse a Gamebryo .settings file from raw binary data.
// Throws SettingsError on malformed input.
SettingsFile settings_parse(std::span<const uint8_t> data);

} // namespace lu::assets
