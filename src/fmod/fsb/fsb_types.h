#pragma once
// References:
//   - lcdr/lu_formats (github.com/lcdr/lu_formats) — FSB format analysis

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// FSB (FMOD Sound Bank) file parser.
// 98 .fsb files in the client's audio/ directory.
//
// LU's FSB files are encrypted with FMOD's cipher. The encrypted region covers
// the ENTIRE file — headers AND audio sample data. (Previously assumed only
// headers were encrypted, but sample extraction confirmed the audio data also
// requires decryption to produce valid MP3 frames.)
//
// Cipher (confirmed by RE of fmodex.dll FUN_10040d7e, verified against all 98 LU FSBs):
//   plaintext[i] = bit_reverse(ciphertext[i]) XOR key[i % key_len]
//
// LU password key: "1024442297" (10 ASCII bytes — the project integer password
// stored as its decimal string representation, as FMOD stores passwords in
// FMOD_ADVANCEDSETTINGS.ASIOSpeakerList / FSBMemoryKey).
//
// Decrypted files are standard FSB4 format:
//   [4  bytes] magic "FSB4"
//   [4  bytes] num_samples         — number of audio samples in this bank
//   [4  bytes] sample_header_size  — total size of all sample headers (bytes)
//   [4  bytes] data_size           — total size of audio sample data (bytes)
//   [4  bytes] version             — FSB format version (0x00040000 for FSB4)
//   [4  bytes] mode                — global mode flags (FMOD_MODE)
//   [8  bytes] bank_checksums      — two u32 values; these are also stored verbatim
//                                    in the FEV bank struct's fsb_checksum[2] field.
//                                    FMOD Event uses these to verify that the FSB
//                                    paired with a FEV bank is correct.
//   [sample_header_size bytes] sample headers
//   [data_size bytes]          audio sample data
//
// Each FSB4 sample header (80 bytes base):
//   [2  bytes] size            — total size of this header entry (inclusive, base=80)
//   [30 bytes] name            — null-terminated sample name
//   [4  bytes] length_samples  — duration in PCM samples
//   [4  bytes] compressed_size — compressed audio data size
//   [4  bytes] loop_start      — loop start in samples
//   [4  bytes] loop_end        — loop end in samples
//   [4  bytes] mode            — per-sample FMOD_MODE flags
//   [4  bytes] default_freq    — sample rate in Hz
//   [2  bytes] default_vol     — raw 0–255; stored in FsbSampleHeader as float 0.0–1.0
//   [2  bytes] default_pan     — raw 0–255 with 128=center; stored as int16_t (-128..127)
//   [2  bytes] default_pri     — default priority
//   [2  bytes] num_channels    — channel count
//   [4  bytes] min_distance    — 3D min distance (f32)
//   [4  bytes] max_distance    — 3D max distance (f32)
//   [4  bytes] var_freq        — frequency variation (u32; base 100 = no variation)
//   [2  bytes] var_vol         — volume variation (u16)
//   [2  bytes] var_pan         — pan variation (u16)
//   [size-80 bytes] extra data — extended fields when size > 80

// LU decryption key (decimal ASCII of the FMOD project password integer).
inline constexpr std::string_view FSB_LU_KEY = "1024442297";

inline constexpr uint32_t FSB4_MAGIC = 0x34425346; // "FSB4"
inline constexpr uint32_t FSB5_MAGIC = 0x35425346; // "FSB5"

struct FsbError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct FsbSampleHeader {
    std::string name;
    uint32_t length_samples  = 0;
    uint32_t compressed_size = 0;
    uint32_t loop_start      = 0;
    uint32_t loop_end        = 0;
    uint32_t mode            = 0;
    uint32_t default_freq    = 0;
    float    default_vol     = 1.0f; // Normalized: raw 0–255 divided by 255.0 in parser
    int16_t  default_pan     = 0;    // Signed: raw 0–255 with 128=center, normalized to -128..127 in parser
    uint16_t default_pri     = 0;
    uint16_t num_channels    = 0;
    float    min_distance    = 0;
    float    max_distance    = 0;
    uint32_t var_freq        = 0;
    uint16_t var_vol         = 0;
    uint16_t var_pan         = 0;
    // Extended header data beyond the 80-byte base (codec-specific fields for
    // Vorbis/AT3/etc.). Empty for all 98 LU client FSBs (all use 80-byte headers).
    std::vector<uint8_t> extra_header_data;
};

struct FsbFile {
    uint32_t magic              = 0; // FSB4_MAGIC or FSB5_MAGIC
    uint32_t num_samples        = 0;
    uint32_t sample_header_size = 0;
    uint32_t data_size          = 0;
    uint32_t version            = 0;
    uint32_t mode               = 0;
    // The 8 reserved bytes from the FSB4 header at offset 24.
    // These are the FSB-side bank checksums that FMOD Event cross-checks
    // against the FEV bank's fsb_checksum[2] field on bank load.
    std::array<uint32_t, 2> bank_checksums = {0, 0};
    std::vector<FsbSampleHeader> samples;
    size_t   data_offset        = 0;  // offset where audio data begins
    bool     encrypted          = false;
};

// Reverse all 8 bits of a byte (used in the FSB cipher).
// Matches the bit-reversal operation in fmodex.dll FUN_10040d7e.
inline uint8_t fsb_bit_reverse(uint8_t b) {
    b = static_cast<uint8_t>(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = static_cast<uint8_t>(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    b = static_cast<uint8_t>(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
    return b;
}
} // namespace lu::assets
