#pragma once
#include "fmod/fev/fev_types.h"

#include <cstdint>
#include <vector>

namespace lu::assets {

// Serialize a FevFile back to its FEV binary format. Dispatches on FevFile::is_riff:
// a FEV1-origin file is written flat (magic "FEV1"), a RIFF-origin file is written
// through the RIFF container path (magic "RIFF"/"FEV "). Both are byte-identical to
// the source for any FevFile produced by fev_parse() on a file of that origin.
//
// FEV1: every field is written back in the exact order the reader consumes it, and
// the flag bitfields (which the reader keeps as raw[] bytes) are replayed verbatim
// rather than recomputed from the decoded bool fields.
//
// RIFF: the preserved raw LGCY/EPRP chunk bytes are emitted verbatim and the OBCT/
// PROP/STRR/LANG chunks plus the RIFF framing are reconstructed, so round-trip does
// not depend on the (best-effort) semantic LGCY decode. Hand-built RIFF FevFiles
// with no preserved bytes fall back to re-serializing LGCY/EPRP from the model.
// See src/fmod/fev/README.md for the RIFF container layout.
std::vector<uint8_t> fev_write(const FevFile& fev);

} // namespace lu::assets
