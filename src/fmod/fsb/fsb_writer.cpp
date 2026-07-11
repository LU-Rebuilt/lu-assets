#include "fmod/fsb/fsb_writer.h"
#include "common/binary_writer/binary_writer.h"

#include <cstring>
#include <string>

namespace lu::assets {

std::vector<uint8_t> fsb_write(const FsbFile& fsb, std::span<const uint8_t> original_decrypted_data) {
    if (fsb.magic != FSB4_MAGIC) {
        throw FsbError("FSB write: only FSB4 is supported");
    }
    if (fsb.samples.size() != fsb.num_samples) {
        throw FsbError("FSB write: samples.size() does not match num_samples");
    }

    BinaryWriter w;

    // --- 48-byte FSB4 header ---
    w.write_u32(fsb.magic);
    w.write_u32(fsb.num_samples);
    w.write_u32(fsb.sample_header_size);
    w.write_u32(fsb.data_size);
    w.write_u32(fsb.version);
    w.write_u32(fsb.mode);
    w.write_u32(fsb.bank_checksums[0]);
    w.write_u32(fsb.bank_checksums[1]);
    w.write_bytes(fsb.header_reserved.data(), fsb.header_reserved.size());

    // --- Sample headers ---
    for (const FsbSampleHeader& s : fsb.samples) {
        // A name of exactly 30 bytes is valid — it fills the fixed 30-byte field with
        // no NUL terminator (real files do this, e.g. "GF_Gorilla_Breathing_Mad_1.wav").
        // write_fixed_str copies min(size, 30) bytes then zero-pads the remainder.
        if (s.name.size() > 30) {
            throw FsbError("FSB write: sample name '" + s.name +
                           "' exceeds the 30-byte name field");
        }
        uint16_t header_size = static_cast<uint16_t>(80 + s.extra_header_data.size());
        w.write_u16(header_size);
        w.write_fixed_str(s.name, 30);
        w.write_u32(s.length_samples);
        w.write_u32(s.compressed_size);
        w.write_u32(s.loop_start);
        w.write_u32(s.loop_end);
        w.write_u32(s.mode);
        w.write_u32(s.default_freq);
        // Reverse the parser's normalization: float 0.0-1.0 -> raw 0-255,
        // signed -128..127 -> raw 0-255 with 128=center.
        w.write_u16(static_cast<uint16_t>(s.default_vol * 255.0f + 0.5f));
        w.write_u16(static_cast<uint16_t>(s.default_pan + 128));
        w.write_u16(s.default_pri);
        w.write_u16(s.num_channels);
        w.write_f32(s.min_distance);
        w.write_f32(s.max_distance);
        w.write_u32(s.var_freq);
        w.write_u16(s.var_vol);
        w.write_u16(s.var_pan);
        if (!s.extra_header_data.empty()) {
            w.write_bytes(s.extra_header_data.data(), s.extra_header_data.size());
        }
    }

    // A real, occasionally-present trailing zero-padding gap between the last
    // sample header and the audio data — see FsbFile::sample_header_padding.
    if (!fsb.sample_header_padding.empty()) {
        w.write_bytes(fsb.sample_header_padding.data(), fsb.sample_header_padding.size());
    }

    // --- Audio sample data ---
    // Preserved as one opaque blob: the whole [data_offset, data_offset + data_size)
    // region of the original decrypted buffer, copied verbatim. This is deliberately
    // NOT reconstructed sample-by-sample from each sample's compressed_size, because
    // the per-sample on-disk layout includes codec-alignment padding between samples
    // (an irregular number of zero bytes so each sample's audio starts on a boundary
    // FMOD's decoder requires) that is not derivable from the header fields alone —
    // the sum of compressed_size is always slightly less than data_size. Since this
    // module models only per-sample metadata (not the audio bytes) and its job is
    // byte-perfect round-trip, replaying the exact original region sidesteps needing
    // to reproduce that padding rule, matching the raw-block-preservation approach
    // used for NIF/HKX.
    if (fsb.data_offset + fsb.data_size > original_decrypted_data.size()) {
        throw FsbError("FSB write: audio data region (data_offset " +
                       std::to_string(fsb.data_offset) + " + data_size " +
                       std::to_string(fsb.data_size) +
                       ") extends past the end of original_decrypted_data");
    }
    w.write_bytes(original_decrypted_data.data() + fsb.data_offset, fsb.data_size);

    return std::move(w.data());
}

std::vector<uint8_t> fsb_encrypt(std::span<const uint8_t> decrypted_data, std::string_view key) {
    if (key.empty()) {
        throw FsbError("FSB encrypt: key must not be empty");
    }
    std::vector<uint8_t> out(decrypted_data.begin(), decrypted_data.end());
    const size_t key_len = key.size();
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = fsb_bit_reverse(out[i] ^ static_cast<uint8_t>(key[i % key_len]));
    }
    return out;
}

} // namespace lu::assets
