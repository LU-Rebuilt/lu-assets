#pragma once
#include "netdevil/zone/aud/aud_types.h"

#include <vector>

namespace lu::assets {

// Emit the file's preserved verbatim bytes — byte-identical for any AudFile produced by
// aud_parse. XML has no canonical byte form, so structured edits require regenerating
// the document instead (see AudFile::raw).
std::vector<uint8_t> aud_write(const AudFile& f);

} // namespace lu::assets
