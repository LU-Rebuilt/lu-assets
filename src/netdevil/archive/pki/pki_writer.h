#pragma once
#include "netdevil/archive/pki/pki_types.h"

#include <vector>

namespace lu::assets {

// Serialize a PkiFile back to the primary.pki binary format. Byte-identical to the
// source file for any PkiFile produced by pki_parse: pack paths keep their original
// backslash separators and entries keep the raw is_compressed word (including the
// original packer's uninitialized upper bytes).
std::vector<uint8_t> pki_write(const PkiFile& pki);

} // namespace lu::assets
