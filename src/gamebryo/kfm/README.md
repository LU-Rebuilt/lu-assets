# KFM (Keyframe Manager) Format

**Extension:** `.kfm`
**Used by:** The LEGO Universe client to associate a NIF mesh with its animation sequences

## Overview

KFM files are Gamebryo's animation manager format. Each KFM is a lightweight wrapper that references a NIF mesh file and its associated animation data. The actual animation sequences are stored in .kf files (which are standard NIF format).

## Binary Layout

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    var   ASCII     text_header — ";Gamebryo KFM File Version X.X.X.Xb\n"
                        (newline-terminated)
+0x00   1     u8        has_text_keys — always 1 in LU client
+0x01   4     u32       path_length — length of NIF path string
+0x05   N     char[N]   nif_path — relative path to the associated NIF mesh file
```

## Version

LU client KFM files use version **2.2.0.0b**. The version is encoded in the ASCII text header:

```
;Gamebryo KFM File Version 2.2.0.0b\n
```

The header is newline-terminated. The version string is parsed but not used for conditional logic -- all LU KFM files share the same binary layout after the header.

## Key Details

- Text header is ASCII, terminated by newline character
- has_text_keys is always 1 in LU client files (text key extra data present)
- The nif_path points to the associated .nif mesh file (relative path)
- Animation .kf files are standard NIF format and parsed with the NIF reader
- KFM files work in conjunction with .settings files for animation sequence management

## References

- kfm.xml (github.com/niftools/kfmxml) — KFM format field definitions
- NifSkope (github.com/niftools/nifskope)
