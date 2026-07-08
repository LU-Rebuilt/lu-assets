#include "gamebryo/kfm/kfm_writer.h"
#include "common/binary_writer/binary_writer.h"

namespace lu::assets {

std::vector<uint8_t> kfm_write(const KfmFile& kfm) {
    if (kfm.header_line.empty() || kfm.header_line.back() != '\n') {
        throw KfmError("KFM write: header_line must be the original text line ending in \\n");
    }

    BinaryWriter w;

    w.write_bytes(reinterpret_cast<const uint8_t*>(kfm.header_line.data()),
                  kfm.header_line.size());
    w.write_u8(kfm.endian);

    w.write_string32(kfm.model_path);
    w.write_string32(kfm.model_root);

    w.write_u32(kfm.default_sync_transition_type);
    w.write_u32(kfm.default_non_sync_transition_type);
    w.write_f32(kfm.default_sync_transition_duration);
    w.write_f32(kfm.default_non_sync_transition_duration);

    w.write_u32(static_cast<uint32_t>(kfm.sequences.size()));
    for (const KfmSequence& seq : kfm.sequences) {
        w.write_u32(seq.id);
        w.write_string32(seq.kf_filename);
        w.write_u32(seq.anim_index);

        w.write_u32(static_cast<uint32_t>(seq.transitions.size()));
        for (const KfmTransition& t : seq.transitions) {
            w.write_u32(t.dest_id);
            w.write_u32(t.stored_type);
            if (t.has_ext()) {
                w.write_f32(t.duration);
                w.write_u32(static_cast<uint32_t>(t.blend_pairs.size()));
                for (const KfmBlendPair& pair : t.blend_pairs) {
                    w.write_string32(pair.start_key);
                    w.write_string32(pair.target_key);
                }
                w.write_u32(static_cast<uint32_t>(t.chain_sequences.size()));
                for (const KfmChainSequence& link : t.chain_sequences) {
                    w.write_u32(link.sequence_id);
                    w.write_f32(link.duration);
                }
            }
        }
    }

    w.write_u32(static_cast<uint32_t>(kfm.sequence_groups.size()));
    for (const KfmSequenceGroup& group : kfm.sequence_groups) {
        w.write_u32(group.group_id);
        w.write_string32(group.name);
        w.write_u32(static_cast<uint32_t>(group.sequence_infos.size()));
        for (const KfmSequenceInfo& info : group.sequence_infos) {
            w.write_u32(info.sequence_id);
            w.write_s32(info.priority);
            w.write_f32(info.weight);
            w.write_f32(info.ease_in_time);
            w.write_f32(info.ease_out_time);
            w.write_u32(info.sync_sequence_id);
        }
    }

    return std::move(w.data());
}

} // namespace lu::assets
