#include "gamebryo/settings/settings_writer.h"
#include "common/binary_writer/binary_writer.h"

namespace lu::assets {

std::vector<uint8_t> settings_write(const SettingsFile& s) {
    BinaryWriter w;

    w.write_string8(s.version);

    // Header fields — same order as settings_parse.
    w.write_u32(s.header_flags);
    w.write_u32(s.model_filename_len);
    w.write_u32(s.unk_c);
    w.write_u8(s.unk_d);
    w.write_u32(s.unk_e);
    w.write_u8(s.unk_f);

    // Sequences section
    w.write_u32(s.section_type_sequences);
    w.write_string8("Sequences");
    w.write_u32(s.declared_sequence_count);
    for (const SettingsSequence& seq : s.sequences) {
        w.write_u32(seq.entry_type);
        w.write_string8(seq.name);
        w.write_u32(seq.entry_id);
    }

    // Sequence Groups section
    w.write_u32(s.section_type_groups);
    w.write_string8("Sequence Groups");
    w.write_u32(s.group_count);
    for (const SettingsGroupEntry& group : s.group_entries) {
        w.write_u32(group.entry_count);
        w.write_string8(group.name);
        w.write_u32(group.unk_trailing);
    }

    // Animation table
    w.write_u32(static_cast<uint32_t>(s.animations.size()));
    for (const SettingsAnimation& anim : s.animations) {
        w.write_u32(anim.event_code);
        w.write_u32(anim.num_transitions);
        w.write_u32(anim.unk_field);
        w.write_u8(anim.unk_byte_1);
        w.write_u8(anim.unk_byte_2);
        w.write_u8(anim.unk_byte_3);
    }

    // Footer — raw bytes preserved as read.
    w.write_bytes(s.footer_bytes.data(), s.footer_bytes.size());

    return std::move(w.data());
}

} // namespace lu::assets
