# ZAL (Zone Asset List) Format

**Extension:** `.zal`
**Used by:** The LEGO Universe client to list all assets required by a zone for preloading

## Overview

ZAL files are plain-text lists of file paths, one per line, enumerating every asset a zone needs. Used by the client for asset preloading and dependency tracking.

## Text Layout

```
path\to\asset1.nif
path\to\asset2.dds
path\to\asset3.kfm
...
```

## Key Details

- Plain text format, one file path per line
- Backslash-separated paths (normalized to forward slashes by the reader)
- No header, no magic number
- Empty lines are ignored
- Paths are relative to the client's resource root

## References

- lcdr/lu_formats (github.com/lcdr/lu_formats)
