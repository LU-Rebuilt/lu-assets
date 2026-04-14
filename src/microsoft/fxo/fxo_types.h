#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// FXO (DirectX 9 Effect Object) / FXP (Effect Pool) precompiled shader parser.
// 32 .fxo + 2 .fxp files in the client's shaders/precompile/ directory.
//
// FXO files are D3D9 Effect binary format produced by fxc.exe / D3DXCompileEffect.
// D3DX9 magic: 0xFEFF0901 (little-endian).
//
// Binary layout (confirmed by statistical analysis of all 32 client FXOs):
//
//   [0x00] u32  signature  = 0xFEFF0901
//   [0x04] u32  meta_size  — size of metadata section (params + techniques), NOT file size
//   [0x08] u32  unk_08     — always 0 in LU client
//   [0x0C] u32  unk_0c     — always 3 in LU client (effect pool header value)
//   [0x10] u32  unk_10     — always 2 in LU client (effect pool header value)
//   [0x14] u32  unk_14     — always 0x60 in LU client (header size / pool section size)
//   [0x18] u32  unk_18     — always 0x78 in LU client
//   [0x1C-0x5F] — padding/pool data (all zero in LU client; common.fxp and
//                 legoppcommon.fxp are both 0-byte files, so pool is empty)
//
//   [0x60] u32  param_table_header  — always 0; exact meaning not RE'd
//   [0x64] ...  parameter blocks (variable count, variable size each):
//
// Parameter block layout:
//   u32  preamble    — always 0 in LU client (pool/type tag, unknown meaning)
//   u32  name_len    — name length including null terminator
//   char name[]      — name_len bytes, padded to 4-byte boundary
//   u32  sem_len     — semantic length including null terminator
//   char semantic[]  — sem_len bytes, padded to 4-byte boundary
//   u32  type        — D3DXPARAMETER_TYPE (3 = matrix/float, etc.)
//   u32  class       — D3DXPARAMETER_CLASS (0=scalar, 1=vector, 2=matrix_rows, etc.)
//   u32  reg_offset  — constant register start index
//   u32  reg_count   — number of registers used
//   u32  ann_count   — annotation count (always 0 in LU client)
//   u32  rows        — matrix/vector row count (1–4)
//   u32  cols        — matrix/vector column count (1–4)
//   u8   tail[]      — variable-length tail: default values, per-register data.
//                      Size depends on type (4×4 matrix → more, scalar → less).
//                      Not fully RE'd; stored in raw_data.
//
// Technique block (appears after all parameter blocks):
//   u8   block_hdr[16]    — pre-name fields (pass_count etc.), not fully RE'd
//   u32  name_len
//   char name[]
//   ... pass data follows (variable, not fully RE'd)
//
// Technique names all start with "Technique_" in the LU client.
// Pass data includes compiled HLSL vertex and pixel shader bytecode.
//
// NOTE: These files can NOT be used cross-platform — they contain precompiled
// DirectX 9 shader bytecode. The metadata (parameter names/semantics/types)
// serves as a reference guide for rewriting these shaders in GLSL/SPIR-V/Metal.
//
// TODO: RE from legouniverse.exe: locate D3DXCreateEffectFromFile call and
//       trace the effect parameter binding code to verify field layout.

struct FxoError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// D3D9 Effect signature (first 4 bytes, little-endian)
inline constexpr uint32_t FXO_SIGNATURE = 0xFEFF0901;

// D3DXPARAMETER_CLASS values
enum class FxoParamClass : uint32_t {
    Scalar      = 0,  // Single float/int/bool
    Vector      = 1,  // Float2/3/4
    MatrixRows  = 2,  // Row-major matrix
    MatrixCols  = 3,  // Column-major matrix
    Object      = 4,  // Texture/sampler/string
    Struct      = 5,
};

// D3DXPARAMETER_TYPE values (subset used in LU)
enum class FxoParamType : uint32_t {
    Void        = 0,
    Bool        = 1,
    Int         = 2,
    Float       = 3,
    String      = 4,
    Texture     = 5,
    Texture1D   = 6,
    Texture2D   = 7,
    Texture3D   = 8,
    TextureCube = 9,
    Sampler     = 10,
    Sampler1D   = 11,
    Sampler2D   = 12,
    Sampler3D   = 13,
    SamplerCube = 14,
};

struct FxoParameter {
    std::string  name;
    std::string  semantic;
    uint32_t     type        = 0;  // D3DXPARAMETER_TYPE
    uint32_t     param_class = 0;  // D3DXPARAMETER_CLASS
    uint32_t     reg_offset  = 0;  // constant register index
    uint32_t     reg_count   = 0;  // registers consumed
    uint32_t     ann_count   = 0;  // annotation count (always 0 in LU)
    uint32_t     rows        = 0;  // matrix rows (1-4)
    uint32_t     cols        = 0;  // matrix cols (1-4)
};

struct FxoTechnique {
    std::string name;          // e.g. "Technique_Basic_Lighting"
};

struct FxoFile {
    uint32_t signature  = 0;  // 0xFEFF0901
    uint32_t meta_size  = 0;  // size of metadata section
    uint32_t unk_08     = 0;
    uint32_t unk_0c     = 0;  // always 3 in LU
    uint32_t unk_10     = 0;  // always 2 in LU
    uint32_t unk_14     = 0;  // always 0x60 in LU
    uint32_t unk_18     = 0;  // always 0x78 in LU

    std::vector<FxoParameter> parameters;  // shared pool + file-specific params
    std::vector<FxoTechnique> techniques;

    std::vector<uint8_t> raw_data;  // full file bytes stored for shader bytecode access
    size_t total_size = 0;
};
} // namespace lu::assets
