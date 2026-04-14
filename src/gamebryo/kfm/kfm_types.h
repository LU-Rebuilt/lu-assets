#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <stdexcept>

namespace lu::assets {

// References:
//   - kfm.xml (github.com/niftools/kfmxml) — KFM format field definitions

// KFM (Keyframe Manager) file parser.
// Gamebryo KFM files are simple wrappers that reference a NIF mesh file
// and its animation sequences.
//
// Format verified from NifSkope KFM format, kfm.xml spec, and file analysis.
// Header: ";Gamebryo KFM File Version X.X.X.Xb\n"
// After newline:
//   [u8 has_text_keys]  — Always 1 in LU client (text key extra data present)
//   [u32 path_length]   — Length of NIF path string
//   [path_length chars] — Relative path to the associated NIF mesh file
//
// KF files are standard NIF format containing NiControllerSequence blocks
// and can be parsed directly with nif_parse().

struct KfmError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct KfmFile {
    uint8_t has_text_keys = 0;  // Text key extra data flag (always 1 in LU)
    std::string nif_path;       // Relative path to the associated NIF mesh file
};
} // namespace lu::assets
