# FSB (FMOD Sound Bank) Format

**Extension:** `.fsb`
**Used by:** The LEGO Universe client (98 files in audio/) to store audio sample data (MP3, PCM, etc.)

## Overview

FSB files are FMOD's sound bank format containing one or more audio samples. All 98 LU FSB files are encrypted with FMOD's cipher. After decryption, they are standard FSB4 format with sample headers and audio data.

## Encryption

LU's FSB files are fully encrypted (headers AND audio data).

```
Cipher: plaintext[i] = bit_reverse(ciphertext[i]) XOR key[i % key_len]
Key:    "1024442297" (10 ASCII bytes — FMOD project password as decimal string)
```

bit_reverse reverses all 8 bits of a byte (confirmed from fmodex.dll FUN_10040d7e).

## Binary Layout (after decryption)

### FSB4 Header (48 bytes)

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    4     u32       magic — 0x34425346 ("FSB4")
0x04    4     u32       num_samples — number of audio samples
0x08    4     u32       sample_header_size — total size of all sample headers
0x0C    4     u32       data_size — total size of audio data
0x10    4     u32       version — 0x00040000 for FSB4
0x14    4     u32       mode — global FMOD_MODE flags
0x18    8     u32[2]    bank_checksums — 8-byte truncated MD5 of the first sample's
                        source filename (see "Header checksums" below)
0x20    16    u8[16]    header_reserved — full 16-byte MD5 digest, exact hashed input
                        not yet confirmed (see "Header checksums" below)
0x30    ...             (48 bytes total header)
```

### Per Sample Header (80 bytes base)

```
Offset  Size  Type      Field
------  ----  ----      -----
0x00    2     u16       size — total header entry size (base = 80)
0x02    30    char[30]  name — null-terminated sample name
0x20    4     u32       length_samples — duration in PCM samples
0x24    4     u32       compressed_size — compressed audio data size
0x28    4     u32       loop_start — in samples
0x2C    4     u32       loop_end — in samples
0x30    4     u32       mode — per-sample FMOD_MODE flags
0x34    4     u32       default_freq — sample rate in Hz
0x38    2     u16       default_vol — volume (0-255 raw)
0x3A    2     u16       default_pan — pan (0-255, 128=center)
0x3C    2     u16       default_pri — priority
0x3E    2     u16       num_channels — channel count
0x40    4     f32       min_distance — 3D min distance
0x44    4     f32       max_distance — 3D max distance
0x48    4     u32       var_freq — frequency variation (base 100)
0x4C    2     u16       var_vol — volume variation
0x4E    2     u16       var_pan — pan variation
```

When `size > 80`, extra bytes follow for codec-specific extended fields (not used in LU).

### Audio Data

Immediately follows all sample headers. Each sample's audio data is at an offset computed by summing previous samples' compressed_size values.

## Header checksums

Two adjacent fields at header offset 0x18-0x2F were investigated via RE of `fmod_designer.exe` (the FSB4 writer, `fsbank_writer_fsb4.cpp` per an embedded assert string):

- **`bank_checksums`** (offset 0x18, 8 bytes): confirmed to be an 8-byte truncated MD5 digest of the *first sample's source filename* (with a `"_et_al"` suffix appended when the bank has more than one subsound) — traced through `FUN_006e4c50` → `FUN_006e2dc0` → `FUN_006e2cc0` in the 4.44.64 build. This corrects an earlier assumption (still visible in some older notes) that it was an FMOD-Event/FEV cross-check value paired against the FEV bank's own `fsb_checksum` field — it is not; the two formats don't actually cross-validate this way.
- **`header_reserved`** (offset 0x20, 16 bytes): confirmed to be a genuine, standard MD5 digest — the exact reference MD5 init constants, round constants, and finalize/padding logic were matched byte-for-byte in `fmod_designer.exe` (`FUN_006e2390`/`FUN_006e2aa0`/`FUN_006e23f0`/`FUN_006e2b70`). The hash context is a persistent member of the bank-build object, explicitly fed the 48-byte header (with this field still zero) once, right before the header is written to disk. What's **not** confirmed: MD5(header bytes 0x00-0x1F) does not reproduce the real digest in any sampled file, so something else must be fed into the same hash context earlier in the build (most likely during per-subsound audio encoding) via a code path not yet traced statically. Resolving this exactly would need live debugging (a real Windows environment with `pybag`/`dbgeng` — attempted via Wine/Proton, blocked by `dbgmodel.dll` not being available or implementable there).

Both fields are preserved verbatim by the writer regardless of their exact derivation.

## Version

All 98 LU client FSB files are **FSB4** format:
- Magic: `"FSB4"` (`0x34425346`)
- Version field (offset 0x10): `0x00040000`

FSB3 (magic `"FSB3"`) uses a different header layout and is not present in the LU client. The reader only supports FSB4.

## Sample header padding and audio layout

Two layout details matter for byte-perfect round-trip, both found while building the writer:

- **Sample-header padding**: `sample_header_size` sometimes overstates the actual bytes consumed by the sample headers (the sum of each sample's own 2-byte `size` field). The remainder is a zero-padding gap between the last sample header and the audio data. Across the 98-file corpus this gap is always either exactly 0 or exactly 16 bytes, all-zero, with no correlation found (to sample count, mode, or filename) for which files get the 16-byte gap. Captured on `FsbFile::sample_header_padding` and replayed verbatim.
- **Per-sample audio alignment**: the audio region's total size (`data_size`) is consistently a little larger than the sum of all samples' `compressed_size` — each sample's audio starts on a codec-alignment boundary, with an irregular number of zero bytes between samples that isn't derivable from the header fields alone. Rather than reconstruct this, the writer preserves the entire `[data_offset, data_offset + data_size)` audio region as one opaque blob (raw-block preservation, same approach as NIF/HKX). `FsbFile` intentionally models only per-sample metadata, not the audio bytes.

Also note: a sample name that is exactly 30 characters fills the fixed name field with **no** NUL terminator (e.g. `GF_Gorilla_Breathing_Mad_1.wav`), so the reader bounds the name to 30 bytes rather than scanning for a NUL (which would overrun into the next field).

## Key Details

- Little-endian byte order
- Magic (decrypted): `0x34425346` ("FSB4")
- All 98 LU client FSBs use 80-byte sample headers (no extended codec fields)
- Full-file encryption: both headers and audio data must be decrypted
- Header checksums at offset 0x18-0x2F are MD5-derived (see "Header checksums" above), not a cross-check against the paired FEV bank
- Volume is normalized from raw 0-255 to float 0.0-1.0 by the reader
- Pan is converted from raw 0-255 (128=center) to signed -128..127

## References

- lcdr/lu_formats (github.com/lcdr/lu_formats) — FSB format analysis
- Ghidra RE of fmodex.dll FUN_10040d7e — encryption cipher
