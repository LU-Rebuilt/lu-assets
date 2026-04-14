#pragma once
#include "fmod/fsb/fsb_types.h"

namespace lu::assets {

// Decrypt an FSB file in-place using the given key string.
//
// FMOD cipher (fmodex.dll FUN_10040d7e):
//   plaintext[i] = bit_reverse(ciphertext[i]) XOR key[i % key_len]
//
// The encrypted region is the 48-byte main header plus all sample headers
// (bytes 0 .. 48+shdrsize). Audio data beyond that is plaintext.
// Returns true if decryption produced a valid FSB4/FSB5 magic.
bool fsb_decrypt(std::vector<uint8_t>& data, std::string_view key = FSB_LU_KEY);

// Parse a decrypted FSB file (call fsb_decrypt first if encrypted).
FsbFile fsb_parse(std::span<const uint8_t> data);

// Read the two u32 bank checksums from a decrypted FSB4 header.
// These are the 8 bytes at offset 24 (the "reserved" field in FSB4).
// They match verbatim the fsb_checksum[2] field stored in the paired FEV bank.
//
// Returns {0, 0} if the data is too short or not FSB4.
std::array<uint32_t, 2> fsb_read_bank_checksums(std::span<const uint8_t> decrypted_header);

// Decrypt FSB header (if needed) and return the bank checksums.
// Convenience wrapper: decrypts into a local buffer, extracts checksums.
std::array<uint32_t, 2> fsb_extract_bank_checksums(std::span<const uint8_t> data,
                                                    std::string_view key = FSB_LU_KEY);

} // namespace lu::assets
