#pragma once
#include "netdevil/zone/ast/ast_types.h"

#include <vector>

namespace lu::assets {

// Serialize back to the on-disk text format. Byte-identical to the source file for any
// AstFile produced by ast_parse: emits the preserved verbatim lines.
std::vector<uint8_t> ast_write(const AstFile& f);

} // namespace lu::assets
