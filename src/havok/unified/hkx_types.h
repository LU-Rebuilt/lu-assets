#pragma once

#include "havok/packfile/hkx_packfile_types.h"
#include "havok/tagged/hkx_tagged_binary_types.h"

#include <stdexcept>
#include <variant>

namespace lu::assets {

// Thrown when the input is neither a binary packfile nor a tagged-binary HKX file
// (e.g. XML format, or unrelated data). XML has no real-file round-trip support in
// this project — see havok/packfile/README.md and havok/tagged/README.md for why.
struct HkxFormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Either of the two byte-perfect-round-trip-supported HKX container formats,
// distinguished by which alternative is active. Use std::holds_alternative<...> or
// std::get_if<...> to branch on which format a parsed file turned out to be.
// Named HkxAny (not HkxFile) to avoid colliding in spirit with the pre-existing,
// unrelated Hkx::HkxFile class (the lossy semantic-analysis reader in
// havok/reader/) — different namespace, but same name would read as confusingly
// similar for a completely different kind of thing (a variant of two byte-preserving
// structs vs. a stateful parser class).
using HkxAny = std::variant<HkxPackfile, HkxTaggedBinary>;

} // namespace lu::assets
