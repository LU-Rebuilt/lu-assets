#include "fmod/fsb/fsb_reader.h"
#include "common/binary_reader/binary_reader.h"

#include <cstring>

namespace lu::assets {

bool fsb_decrypt(std::vector<uint8_t>& data, std::string_view key) {
    if (data.size() < 48) return false;
    if (key.empty()) return false;

    // FSB4 encryption (fmodex.dll FUN_10040d7e):
    //   plaintext[i] = bit_reverse(ciphertext[i]) XOR key[i % key_len]
    //
    // The cipher covers the ENTIRE file — headers AND audio sample data.
    // (Previously assumed only headers were encrypted, but testing confirmed
    // audio data also requires decryption to produce valid MP3 frames.)
    const size_t key_len = key.size();

    // Decrypt the first 48 bytes to validate the magic before committing
    // to a full-file decrypt.
    for (size_t i = 0; i < 48; ++i) {
        data[i] = fsb_bit_reverse(data[i]) ^ static_cast<uint8_t>(key[i % key_len]);
    }

    uint32_t magic;
    std::memcpy(&magic, data.data(), 4);
    if (magic != FSB4_MAGIC && magic != FSB5_MAGIC) return false;

    // Decrypt the rest of the file (bytes 48 onward).
    for (size_t i = 48; i < data.size(); ++i) {
        data[i] = fsb_bit_reverse(data[i]) ^ static_cast<uint8_t>(key[i % key_len]);
    }

    return true;
}

std::array<uint32_t, 2> fsb_read_bank_checksums(std::span<const uint8_t> decrypted_header) {
    if (decrypted_header.size() < 32) return {0, 0};
    uint32_t magic;
    std::memcpy(&magic, decrypted_header.data(), 4);
    if (magic != FSB4_MAGIC) return {0, 0};
    // FSB4 header layout: magic(4) + num_samples(4) + shdrsize(4) + datasize(4) +
    //                     version(4) + mode(4) + bank_checksums[2×4]
    // → bank_checksums at offset 24
    uint32_t ck0, ck1;
    std::memcpy(&ck0, decrypted_header.data() + 24, 4);
    std::memcpy(&ck1, decrypted_header.data() + 28, 4);
    return {ck0, ck1};
}

std::array<uint32_t, 2> fsb_extract_bank_checksums(std::span<const uint8_t> data,
                                                    std::string_view key) {
    if (data.size() < 48) return {0, 0};

    // Check if already decrypted
    uint32_t magic;
    std::memcpy(&magic, data.data(), 4);
    if (magic == FSB4_MAGIC || magic == FSB5_MAGIC) {
        return fsb_read_bank_checksums(data.first(32));
    }

    // Decrypt the main 48-byte header into a temporary buffer.
    // Checksums are at offset 24-31, entirely within those 48 bytes,
    // so we don't need to decrypt the sample headers here.
    const size_t key_len = key.size();
    std::array<uint8_t, 48> header_buf;
    for (size_t i = 0; i < 48; ++i) {
        header_buf[i] = fsb_bit_reverse(data[i]) ^ static_cast<uint8_t>(key[i % key_len]);
    }
    return fsb_read_bank_checksums(std::span<const uint8_t>(header_buf));
}

FsbFile fsb_parse(std::span<const uint8_t> data) {
    if (data.size() < 48) {
        throw FsbError("FSB: file too small for header");
    }

    BinaryReader r(data);
    FsbFile fsb;

    fsb.magic = r.read_u32();
    if (fsb.magic != FSB4_MAGIC && fsb.magic != FSB5_MAGIC) {
        fsb.encrypted = true;
        throw FsbError("FSB: invalid magic — file may be encrypted (use fsb_decrypt first)");
    }

    if (fsb.magic == FSB4_MAGIC) {
        // FSB4 header (48 bytes total)
        fsb.num_samples        = r.read_u32();
        fsb.sample_header_size = r.read_u32();
        fsb.data_size          = r.read_u32();
        fsb.version            = r.read_u32();
        fsb.mode               = r.read_u32();
        // Bank checksums at offset 24: two u32 values cross-checked by FMOD Event
        // against the FEV bank's fsb_checksum[2] field when loading the bank.
        // See fmodex.dll FUN_10040d7e; stored verbatim in FevBank::fsb_checksum[2].
        fsb.bank_checksums[0] = r.read_u32();
        fsb.bank_checksums[1] = r.read_u32();

        // Bytes 32–47: 16-byte bank hash/GUID used by FMOD's internal sound bank
        // cache (fmodex.dll FUN_10040d7e copies the full 48-byte header into a
        // cache-lookup struct and compares these 16 bytes via FUN_1001842b).
        // Not needed for application-level parsing. Sample headers begin at byte 48.
        r.seek(48);

        // Parse sample headers
        fsb.samples.reserve(fsb.num_samples);
        for (uint32_t i = 0; i < fsb.num_samples; ++i) {
            if (r.remaining() < 80) break;

            FsbSampleHeader sample;
            uint16_t header_size = r.read_u16();

            // Name: 30 bytes null-terminated
            auto name_bytes = r.read_bytes(30);
            sample.name = std::string(reinterpret_cast<const char*>(name_bytes.data()));
            auto null_pos = sample.name.find('\0');
            if (null_pos != std::string::npos) sample.name.resize(null_pos);

            sample.length_samples  = r.read_u32();
            sample.compressed_size = r.read_u32();
            sample.loop_start      = r.read_u32();
            sample.loop_end        = r.read_u32();
            sample.mode            = r.read_u32();
            sample.default_freq    = r.read_u32();
            // Normalize at parse time: vol is raw 0–255, pan is 0–255 with 128=center
            sample.default_vol     = r.read_u16() / 255.0f;
            sample.default_pan     = static_cast<int16_t>(r.read_u16()) - 128;
            sample.default_pri     = r.read_u16();
            sample.num_channels    = r.read_u16();
            sample.min_distance    = r.read_f32();
            sample.max_distance    = r.read_f32();
            sample.var_freq        = r.read_u32();
            sample.var_vol         = r.read_u16();
            sample.var_pan         = r.read_u16();

            // Store any extra header data beyond the 80-byte base.
            // Contains codec-specific fields (Vorbis/AT3) when header_size > 80.
            // All 98 LU client FSBs use exactly 80-byte headers, so this is empty.
            if (header_size > 80) {
                auto extra = r.read_bytes(header_size - 80);
                sample.extra_header_data.assign(extra.begin(), extra.end());
            }

            fsb.samples.push_back(std::move(sample));
        }

        fsb.data_offset = 48 + fsb.sample_header_size;
    }
    else if (fsb.magic == FSB5_MAGIC) {
        // FSB5 header (different layout — bank checksums not applicable)
        fsb.version            = r.read_u32();
        fsb.num_samples        = r.read_u32();
        fsb.sample_header_size = r.read_u32();
        fsb.data_size          = r.read_u32();
        fsb.mode               = r.read_u32(); // codec
        fsb.data_offset        = 60 + fsb.sample_header_size;
    }

    return fsb;
}

} // namespace lu::assets
