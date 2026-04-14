# FXO (DirectX 9 Effect Object) Format

**Extension:** `.fxo` (also `.fxp` for effect pools)
**Used by:** The LEGO Universe client for precompiled HLSL shaders (32 .fxo + 2 .fxp files in shaders/precompile/)

## Overview

FXO files are D3D9 Effect binary format produced by fxc.exe / D3DXCompileEffect. They contain compiled HLSL vertex and pixel shader bytecode along with parameter metadata (names, types, semantics, register bindings) and technique definitions. These files are DirectX 9 specific and cannot be used cross-platform.

## Binary Layout

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       signature — 0xFEFF0901 (D3DX9 Effect magic)
0x04    4     u32       meta_size — size of metadata section (params + techniques)
0x08    4     u32       unk_08 — always 0 in LU
0x0C    4     u32       unk_0c — always 3 in LU (effect pool header value)
0x10    4     u32       unk_10 — always 2 in LU
0x14    4     u32       unk_14 — always 0x60 in LU (header/pool section size)
0x18    4     u32       unk_18 — always 0x78 in LU
0x1C-0x5F     padding   pool data (all zero in LU; .fxp pool files are empty)

--- Parameter Table (at 0x60) ---
0x60    4     u32       param_table_header — always 0
0x64    ...             parameter blocks (variable count and size)

--- Per Parameter Block ---
+0x00   4     u32       preamble — always 0 in LU
+0x04   4     u32       name_len — including null terminator
+0x08   N     char[N]   name — padded to 4-byte boundary
+var    4     u32       semantic_len — including null terminator
+var    M     char[M]   semantic — padded to 4-byte boundary
+var    4     u32       type — D3DXPARAMETER_TYPE
+var    4     u32       class — D3DXPARAMETER_CLASS
+var    4     u32       reg_offset — constant register start index
+var    4     u32       reg_count — number of registers used
+var    4     u32       ann_count — annotation count (always 0 in LU)
+var    4     u32       rows — matrix/vector row count (1-4)
+var    4     u32       cols — matrix/vector column count (1-4)
+var    var   u8[]      tail — default values and per-register data

--- Technique Blocks ---
+0x00   16    u8[16]    block_header — pre-name fields (pass_count etc.)
+0x10   4     u32       name_len
+0x14   N     char[N]   name — e.g. "Technique_Basic_Lighting"
+var    ...             pass data with compiled shader bytecode
```

### D3DXPARAMETER_CLASS

```
Value  Class
-----  -----
0      Scalar
1      Vector
2      MatrixRows (row-major)
3      MatrixCols (column-major)
4      Object (texture/sampler/string)
5      Struct
```

### D3DXPARAMETER_TYPE (subset used in LU)

```
Value  Type
-----  ----
0      Void         3      Float        7      Texture2D
1      Bool         4      String       12     Sampler2D
2      Int          5      Texture
```

## Version

FXO files are identified by the D3DX9 Effect signature `0xFEFF0901` at offset 0x00. There is no separate version field -- the signature itself encodes the format version (D3DX9 shader model). All 34 LU client effect files (32 .fxo + 2 .fxp) use this same signature. The format is DirectX 9 specific and does not vary between files.

## Key Details

- Little-endian byte order
- Signature: `0xFEFF0901` (D3DX9 Effect binary magic)
- All technique names start with "Technique_" in LU client files
- Annotation count is always 0 in LU files
- .fxp effect pool files (common.fxp, legoppcommon.fxp) are 0-byte/empty
- NOT cross-platform: contains precompiled DirectX 9 shader bytecode
- Parameter metadata useful as reference for rewriting shaders in GLSL/SPIR-V/Metal

## References

- Microsoft D3DX Effect documentation
- Ghidra RE of legouniverse.exe (D3DXCreateEffectFromFile usage)
