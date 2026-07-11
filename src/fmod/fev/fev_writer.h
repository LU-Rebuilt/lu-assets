#pragma once
#include "fmod/fev/fev_types.h"

#include <cstdint>
#include <vector>

namespace lu::assets {

// Serialize a FevFile back to the FEV1 binary format. Byte-identical to the source
// for any FevFile produced by fev_parse() on a FEV1 file: every field is written
// back in the exact order the reader consumes it, and the flag bitfields (which the
// reader keeps as raw[] bytes) are replayed verbatim rather than recomputed from the
// decoded bool fields.
//
// Only the FEV1 format is written (magic "FEV1"); the RIFF-wrapped FEV variant
// (FMOD Designer 4.45) has zero real client files and is out of scope. fev_parse()
// populates the same FevFile from either format, but fev_write() always emits FEV1
// — round-trip fidelity is only guaranteed for FevFiles that came from a FEV1 source
// (every real client .fev file).
std::vector<uint8_t> fev_write(const FevFile& fev);

} // namespace lu::assets
