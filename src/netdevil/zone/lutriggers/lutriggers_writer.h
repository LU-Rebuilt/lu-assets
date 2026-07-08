#pragma once
#include "netdevil/zone/lutriggers/lutriggers_types.h"

#include <vector>

namespace lu::assets {

// Emit the file's preserved verbatim bytes — byte-identical for any LuTriggersFile produced by
// lutriggers_parse. XML has no canonical byte form, so structured edits require regenerating
// the document instead (see LuTriggersFile::raw).
std::vector<uint8_t> lutriggers_write(const LuTriggersFile& f);

} // namespace lu::assets
