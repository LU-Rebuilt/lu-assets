#pragma once
#include "netdevil/macros/scm/scm_types.h"

namespace lu::assets {

// Parse SCM from raw text data.
ScmFile scm_parse(std::span<const uint8_t> data);

} // namespace lu::assets
