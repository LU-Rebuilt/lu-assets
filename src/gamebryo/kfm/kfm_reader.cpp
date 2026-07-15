#include "gamebryo/kfm/kfm_reader.h"
#include "common/binary_reader/binary_reader.h"

#include <algorithm>
#include <string>

namespace lu::assets {

namespace {

// u4_str with a sanity bound — KFM strings are short paths and text-key names.
std::string read_kfm_string(BinaryReader& r, const char* what) {
    uint32_t len = r.read_u32();
    if (len > 4096) {
        throw KfmError(std::string("KFM: unreasonable ") + what + " length: " +
                       std::to_string(len));
    }
    auto bytes = r.read_bytes(len);
    return std::string(reinterpret_cast<const char*>(bytes.data()), len);
}

KfmTransition parse_transition(BinaryReader& r) {
    KfmTransition t;
    t.dest_id = r.read_u32();
    t.stored_type = r.read_u32();

    // Extended payload only when the transition doesn't defer to the file-level
    // defaults (kfm.ksy: stored_type != default_sync && != default_non_sync).
    if (t.has_ext()) {
        t.duration = r.read_f32();

        uint32_t num_blend_pairs = r.read_u32();
        if (num_blend_pairs > 100000) throw KfmError("KFM: unreasonable blend pair count");
        t.blend_pairs.reserve(num_blend_pairs);
        for (uint32_t i = 0; i < num_blend_pairs; ++i) {
            KfmBlendPair pair;
            pair.start_key = read_kfm_string(r, "blend pair start key");
            pair.target_key = read_kfm_string(r, "blend pair target key");
            t.blend_pairs.push_back(std::move(pair));
        }

        uint32_t num_chain = r.read_u32();
        if (num_chain > 100000) throw KfmError("KFM: unreasonable chain sequence count");
        t.chain_sequences.reserve(num_chain);
        for (uint32_t i = 0; i < num_chain; ++i) {
            KfmChainSequence link;
            link.sequence_id = r.read_u32();
            link.duration = r.read_f32();
            t.chain_sequences.push_back(link);
        }
    }
    return t;
}

} // anonymous namespace

KfmFile kfm_parse(std::span<const uint8_t> data) {
    // Text header line: ";Gamebryo KFM File Version 2.2.0.0b\n" (';' included).
    size_t nl_pos = 0;
    for (size_t i = 0; i < std::min(data.size(), size_t(256)); ++i) {
        if (data[i] == 0x0A) { nl_pos = i + 1; break; }
    }
    if (nl_pos == 0) throw KfmError("KFM: could not find header newline");

    KfmFile kfm;
    kfm.header_line.assign(reinterpret_cast<const char*>(data.data()), nl_pos);

    BinaryReader r(data);
    r.seek(nl_pos);

    kfm.endian = r.read_u8(); // 1 = little-endian (always, in LU files)

    kfm.model_path = read_kfm_string(r, "model path");
    kfm.model_root = read_kfm_string(r, "model root");

    kfm.default_sync_transition_type = r.read_u32();
    kfm.default_non_sync_transition_type = r.read_u32();
    kfm.default_sync_transition_duration = r.read_f32();
    kfm.default_non_sync_transition_duration = r.read_f32();

    uint32_t num_sequences = r.read_u32();
    if (num_sequences > 100000) throw KfmError("KFM: unreasonable sequence count");
    kfm.sequences.reserve(num_sequences);
    for (uint32_t i = 0; i < num_sequences; ++i) {
        KfmSequence seq;
        seq.id = r.read_u32();
        seq.kf_filename = read_kfm_string(r, "kf filename");
        seq.anim_index = r.read_u32();

        uint32_t num_transitions = r.read_u32();
        if (num_transitions > 100000) throw KfmError("KFM: unreasonable transition count");
        seq.transitions.reserve(num_transitions);
        for (uint32_t j = 0; j < num_transitions; ++j) {
            seq.transitions.push_back(parse_transition(r));
        }
        kfm.sequences.push_back(std::move(seq));
    }

    uint32_t num_groups = r.read_u32();
    if (num_groups > 100000) throw KfmError("KFM: unreasonable sequence group count");
    kfm.sequence_groups.reserve(num_groups);
    for (uint32_t i = 0; i < num_groups; ++i) {
        KfmSequenceGroup group;
        group.group_id = r.read_u32();
        group.name = read_kfm_string(r, "sequence group name");

        uint32_t num_infos = r.read_u32();
        if (num_infos > 100000) throw KfmError("KFM: unreasonable sequence info count");
        group.sequence_infos.reserve(num_infos);
        for (uint32_t j = 0; j < num_infos; ++j) {
            KfmSequenceInfo info;
            info.sequence_id = r.read_u32();
            info.priority = r.read_s32();
            info.weight = r.read_f32();
            info.ease_in_time = r.read_f32();
            info.ease_out_time = r.read_f32();
            info.sync_sequence_id = r.read_u32();
            group.sequence_infos.push_back(info);
        }
        kfm.sequence_groups.push_back(std::move(group));
    }

    return kfm;
}

} // namespace lu::assets
