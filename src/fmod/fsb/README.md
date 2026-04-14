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
0x18    8     u32[2]    bank_checksums — cross-checked against FEV bank checksums
0x20    ...             (48 bytes total header)
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

## Version

All 98 LU client FSB files are **FSB4** format:
- Magic: `"FSB4"` (`0x34425346`)
- Version field (offset 0x10): `0x00040000`

FSB3 (magic `"FSB3"`) uses a different header layout and is not present in the LU client. The reader only supports FSB4.

## Key Details

- Little-endian byte order
- Magic (decrypted): `0x34425346` ("FSB4")
- All 98 LU client FSBs use 80-byte sample headers (no extended codec fields)
- Full-file encryption: both headers and audio data must be decrypted
- Bank checksums at offset 0x18 must match the paired FEV bank's fsb_checksum field
- Volume is normalized from raw 0-255 to float 0.0-1.0 by the reader
- Pan is converted from raw 0-255 (128=center) to signed -128..127

## References

- lcdr/lu_formats (github.com/lcdr/lu_formats) — FSB format analysis
- Ghidra RE of fmodex.dll FUN_10040d7e — encryption cipher
