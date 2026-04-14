# AST (Asset List) Format

**Extension:** `.ast`
**Used by:** The LEGO Universe client as a prefixed asset manifest for resource loading

## Overview

AST files are plain-text asset lists where each entry is prefixed with `A:` followed by a file path. Used for asset dependency tracking and preloading.

## Text Layout

```
# Comment line
A:res\audio\file.fsb
A:res\textures\texture.dds
A:res\mesh\model.nif
```

## Version

No versioning -- plain text format with no header or version field. A single format is used by all LU client files.

## Key Details

- Plain text format, one entry per line
- Each asset line starts with `A:` prefix (stripped by the reader)
- Lines starting with `#` are comments
- Empty lines are skipped
- Backslash-separated paths (normalized to forward slashes by the reader)
- Paths are relative to the client's resource root

## References

- lcdr/lu_formats (github.com/lcdr/lu_formats)
