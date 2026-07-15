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
//   [8  bytes] bank_checksums      — two u32 values, together an 8-byte truncated
//                                    MD5 digest of the first sample's source
//                                    filename (with a "_et_al" suffix appended when
//                                    the bank has more than one subsound). Confirmed
//                                    via RE of fmod_designer.exe's FSB4 writer
//                                    (FUN_006e4c50/FUN_006e2dc0/FUN_006e2cc0) —
//                                    corrects an earlier assumption that this was an
//                                    FMOD-Event/FEV cross-check value; it is not.
//                                    Preserved verbatim regardless of what it means.
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
    // The 8 bytes from the FSB4 header at offset 24. NOT an FMOD-Event/FEV
    // cross-check value (an earlier assumption, now corrected) — see the
    // module-level comment above: this is an 8-byte truncated MD5 of the first
    // sample's source filename, confirmed via RE of fmod_designer.exe's writer.
    std::array<uint32_t, 2> bank_checksums = {0, 0};
    // The 16 bytes at header offset 32-47 (FSB4 only). Confirmed via RE of
    // fmod_designer.exe (functions FUN_006e2390/FUN_006e2aa0/FUN_006e23f0/
    // FUN_006e2b70) to be a genuine, standard MD5 digest — not a random GUID: the
    // exact reference MD5 init constants, round constants, and finalization/padding
    // logic were all matched byte-for-byte in the disassembly. The MD5 context is a
    // persistent member of the FSB-bank-build object, fed the 48-byte header buffer
    // (with this field still zero) once before finalizing.
    // What remains UNCONFIRMED: the precise bytes hashed. MD5(header bytes 0-31) does
    // NOT reproduce the real value in any sampled file, so something else is fed into
    // the same persistent hash context earlier in the build (most likely during the
    // per-subsound audio-encoding pass) that hasn't been identified via static
    // analysis alone. Confirming it would require live debugging fmod_designer.exe
    // (breakpoint at the header-write call, dump the hash context's accumulated
    // state) — attempted but blocked by lack of a real Windows debugging environment
    // (pybag/dbgeng needs genuine dbgmodel.dll, which Wine does not implement and
    // isn't present on this system). See project memory: project_fsb_checksum_re.
    // Preserved verbatim for byte-perfect round-trip regardless of the exact input.
    std::array<uint8_t, 16> header_reserved = {};
    std::vector<FsbSampleHeader> samples;
    // Zero-padding bytes between the true end of the sample header block (sum of
    // each sample's own declared 2-byte size field) and where sample_header_size
    // says the block ends. Genuinely real, not a parsing artifact: confirmed across
    // the full 98-file corpus this gap is either exactly 0 or exactly 16 bytes, in
    // both cases all-zero, with no num_samples/mode/filename correlation found for
    // which files get the 16-byte gap. Preserved verbatim rather than assumed.
    std::vector<uint8_t> sample_header_padding;
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
