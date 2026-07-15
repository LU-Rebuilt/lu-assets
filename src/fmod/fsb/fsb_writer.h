#pragma once
#include "fmod/fsb/fsb_types.h"

#include <span>

namespace lu::assets {

// Serialize an FsbFile back to decrypted FSB4 bytes. Byte-identical to the
// decrypted source for any FsbFile produced by fsb_parse(): every header field
// (including bank_checksums and header_reserved, both preserved verbatim rather
// than recomputed — see fsb_types.h for why) is written back as read.
//
// The audio sample data itself is not modeled on FsbFile (only per-sample metadata
// is), so the caller must supply `original_decrypted_data` — the same buffer that
// was passed to fsb_parse() (or the output of fsb_decrypt()) — and the writer slices
// each sample's audio bytes out of it via FsbFile::data_offset and each sample's
// compressed_size, mirroring how fsb_extract's own extraction logic locates sample
// data. This follows the same pattern as psb_write()'s `original_data` parameter.
std::vector<uint8_t> fsb_write(const FsbFile& fsb, std::span<const uint8_t> original_decrypted_data);

// Encrypt an FSB4 buffer (the inverse of fsb_decrypt) using the given key:
//   ciphertext[i] = bit_reverse(plaintext[i] XOR key[i % key_len])
// This is NOT the same operation run twice — decrypt applies bit_reverse first and
// XORs after (plaintext[i] = bit_reverse(ciphertext[i]) XOR key[i % key_len]), so
// naively calling fsb_decrypt() on plaintext does not produce valid ciphertext (the
// two operations only coincidentally agree ~7% of the time per-byte). Encrypts the
// entire buffer (header and audio data alike), matching fsb_decrypt()'s full-file
// coverage. Round-trips: fsb_decrypt(fsb_encrypt(data)) == data.
std::vector<uint8_t> fsb_encrypt(std::span<const uint8_t> decrypted_data,
                                  std::string_view key = FSB_LU_KEY);

} // namespace lu::assets
