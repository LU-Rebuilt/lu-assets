#include "gamebryo/settings/settings_reader.h"
#include "common/binary_reader/binary_reader.h"
#include <string>

namespace lu::assets {

// Sequence Groups section marker signature: u32(4) + u8(15) + "Sequence Groups"
static constexpr uint8_t kSequenceGroupsSignature[] = {
    0x04, 0x00, 0x00, 0x00,  // section_type_groups = 4
    0x0f,                     // string length = 15
    'S','e','q','u','e','n','c','e',' ','G','r','o','u','p','s'
};
static constexpr size_t kSequenceGroupsSigLen = sizeof(kSequenceGroupsSignature);

// Check if the data at the current reader position matches the "Sequence Groups"
// section marker. Does NOT advance the reader.
static bool at_sequence_groups_marker(const BinaryReader& r) {
    if (r.remaining() < kSequenceGroupsSigLen) return false;
    auto window = r.peek_bytes(kSequenceGroupsSigLen);
    for (size_t i = 0; i < kSequenceGroupsSigLen; ++i) {
        if (window[i] != kSequenceGroupsSignature[i]) return false;
    }
    return true;
}

SettingsFile settings_parse(std::span<const uint8_t> data) {
    if (data.empty()) {
        throw SettingsError("Settings: empty data");
    }

    BinaryReader r(data);
    SettingsFile s;

    // Version string: u8 length prefix + ASCII chars
    s.version = r.read_string8();
    if (s.version.empty()) {
        throw SettingsError("Settings: empty version string");
    }

    // --- Header fields (22 bytes) ---
    // Identified from NiKFMTool::ReadBinaryKFM debug symbols and kfm.xml spec.
    if (r.remaining() < 4 + 4 + 4 + 1 + 4 + 1) {
        throw SettingsError("Settings: truncated before header fields");
    }
    s.header_flags       = r.read_u32();  // kfm.xml "Unknown Byte" as u32; always 1
    s.model_filename_len = r.read_u32();  // NIF model filename length; always 0 (empty)
    s.unk_c              = r.read_u32();  // varies
    s.unk_d              = r.read_u8();   // varies (0, 63, 64, or 192)
    s.unk_e              = r.read_u32();  // varies (0, 0xF5, or 0xFFFFFFFF)
    s.unk_f              = r.read_u8();   // always 0

    // --- Sequences section ---
    // Section type identifier (always 1) followed by u8-len-prefixed "Sequences" marker.
    if (r.remaining() < 4) {
        throw SettingsError("Settings: truncated before section_type_sequences");
    }
    s.section_type_sequences = r.read_u32();

    if (r.remaining() < 1) {
        throw SettingsError("Settings: truncated before Sequences marker");
    }
    {
        std::string marker = r.read_string8();
        if (marker != "Sequences") {
            throw SettingsError("Settings: expected 'Sequences' marker, got '" + marker + "'");
        }
    }

    // Declared sequence count. UNRELIABLE for complex files — the parser reads
    // entries until the "Sequence Groups" section marker is detected.
    if (r.remaining() < 4) {
        throw SettingsError("Settings: truncated at declared_sequence_count");
    }
    uint32_t declared_count = r.read_u32();
    (void)declared_count; // informational only; not used as loop bound

    // Read sequence entries until we detect the "Sequence Groups" section marker.
    // Each entry is: u32 entry_type + u8-prefixed name + u32 entry_id.
    // entry_type 2 = group name, 3 = animation sequence.
    while (!at_sequence_groups_marker(r)) {
        if (r.remaining() < 4 + 1) {
            throw SettingsError("Settings: truncated inside sequence entry " +
                std::to_string(s.sequences.size()));
        }
        SettingsSequence seq;
        seq.entry_type = r.read_u32();
        seq.name       = r.read_string8();
        if (r.remaining() < 4) {
            throw SettingsError("Settings: truncated at sequence entry_id " +
                std::to_string(s.sequences.size()));
        }
        seq.entry_id   = r.read_u32();
        s.sequences.push_back(std::move(seq));

        // Sanity limit to prevent infinite loops on corrupt data.
        if (s.sequences.size() > 100000) {
            throw SettingsError("Settings: unreasonable sequence count > 100000");
        }
    }

    // --- Sequence Groups section ---
    // Section type identifier (always 4) followed by u8-len-prefixed "Sequence Groups".
    if (r.remaining() < 4) {
        throw SettingsError("Settings: truncated before section_type_groups");
    }
    s.section_type_groups = r.read_u32();

    if (r.remaining() < 1) {
        throw SettingsError("Settings: truncated before Sequence Groups marker");
    }
    {
        std::string marker = r.read_string8();
        if (marker != "Sequence Groups") {
            throw SettingsError("Settings: expected 'Sequence Groups' marker, got '" + marker + "'");
        }
    }

    // Group count (always 0 in observed client files).
    if (r.remaining() < 4) {
        throw SettingsError("Settings: truncated at group_count");
    }
    s.group_count = r.read_u32();

    // --- Animation table ---
    // u32 animation_count followed by animation_count entries of 15 bytes each.
    if (r.remaining() < 4) {
        throw SettingsError("Settings: truncated at animation_count");
    }
    uint32_t anim_count = r.read_u32();
    if (anim_count > 100000) {
        throw SettingsError("Settings: unreasonable animation_count " + std::to_string(anim_count));
    }

    s.animations.reserve(anim_count);
    for (uint32_t i = 0; i < anim_count; ++i) {
        if (r.remaining() < 15) {
            throw SettingsError("Settings: truncated inside animation entry " + std::to_string(i));
        }
        SettingsAnimation anim;
        anim.event_code      = r.read_u32();
        anim.num_transitions = r.read_u32();
        anim.unk_field       = r.read_u32();
        anim.unk_byte_1      = r.read_u8();
        anim.unk_byte_2      = r.read_u8();
        anim.unk_byte_3      = r.read_u8();
        s.animations.push_back(anim);
    }

    // --- Footer ---
    // Remaining bytes after the animation table. Structure varies per file.
    if (r.remaining() > 0) {
        auto footer = r.read_bytes(r.remaining());
        s.footer_bytes.assign(footer.begin(), footer.end());
    }

    return s;
}

} // namespace lu::assets
