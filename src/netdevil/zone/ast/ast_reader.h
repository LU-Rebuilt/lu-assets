#pragma once
#include "netdevil/zone/ast/ast_types.h"

namespace lu::assets {

// Parse AST from raw text data.
AstFile ast_parse(std::span<const uint8_t> data);

} // namespace lu::assets
