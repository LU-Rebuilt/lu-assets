#pragma once
#include "netdevil/macros/scm/scm_types.h"

#include <vector>

namespace lu::assets {

// Serialize back to the on-disk text format. Byte-identical to the source file for any
// ScmFile produced by scm_parse: emits the preserved verbatim lines.
std::vector<uint8_t> scm_write(const ScmFile& f);

} // namespace lu::assets
