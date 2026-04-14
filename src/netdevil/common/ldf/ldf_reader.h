#pragma once
#include "netdevil/common/ldf/ldf_types.h"
#include "common/binary_reader/binary_reader.h"

#include <utility>

namespace lu::assets {

// LUZ binary LDF config: u32 count, then (wstr8 key, wstr8 value) pairs.
// Used in LUZ path waypoint configs. Returns raw string key-value pairs.
using LdfConfig = std::vector<std::pair<std::string, std::string>>;
LdfConfig ldf_parse_binary(BinaryReader& r);

// Parse a text LDF string into a vector of typed entries.
//
// Input is the ASCII-decoded content of the UTF-16LE config field, e.g.:
//   "create_physics=7:1\nadd_to_navmesh=7:1\ncustom_config_names=0:\n"
//
// Entries with unrecognised type_ids are stored with LdfType::Unknown;
// `raw_value` holds the verbatim value string for inspection.
std::vector<LdfEntry> ldf_parse(std::string_view text);

// Convert a LdfEntry's value back to its text representation ("type:value").
// Useful for round-trip checks or debug output.
std::string ldf_entry_to_string(const LdfEntry& e, bool include_key = true);

} // namespace lu::assets
