#pragma once
#include "fmod/fev/fev_types.h"

namespace lu::assets {

FevFile fev_parse(std::span<const uint8_t> data);

// Verify that a FEV bank's fsb_checksum[2] matches the bank checksums stored in
// the corresponding decrypted FSB file header (the 8 reserved bytes at offset 24).
// Returns true if the checksums match, false if they differ (bank/FEV mismatch).
// Requires fsb_data to already be decrypted (call fsb_decrypt first if encrypted).
bool fev_verify_bank_checksum(const FevBank& bank, std::span<const uint8_t> decrypted_fsb_data);

} // namespace lu::assets
