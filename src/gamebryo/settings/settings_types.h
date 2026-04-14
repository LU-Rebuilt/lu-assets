#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// Gamebryo NiKFMTool compiled-binary settings format (.settings).
// 737 files in the client's mesh/ directory, alongside .nif/.kfm actor files.
// Used by NiActorManager to define animation sequences, sequence groups
// (categories), and inter-sequence transitions for each actor.
//
// This is a binary encoding of the same data model as the .kfm text format
// (";Gamebryo KFM File Version x.x.x"). Instead of text lines, .settings uses
// u8-length-prefixed section markers ("Sequences", "Sequence Groups") and
// binary-encoded fields. Written by NetDevil build tools (SequenceEditor.exe).
//
// RE'd from NiKFMTool::ReadBinaryKFM via Ghidra RE, the
// kfm.xml format specification (niftools/kfmxml), and structural analysis
// of 76 unpacked client .settings files.
//
// Binary layout:
//   --- Header ---
//   [u8]         version_len
//   [n bytes]    version string (always "2.3.0" in client)
//   [u32]        header_flags       — always 1 (maps to kfm.xml "Unknown Byte")
//   [u32]        model_filename_len — always 0 (i32 length of NIF filename; empty)
//   [u32]        unk_c              — varies (0 in simple files; non-zero in complex)
//   [u8]         unk_d              — varies (0, 63, 64, or 192)
//   [u32]        unk_e              — varies (0, 0xF5, or 0xFFFFFFFF)
//   [u8]         unk_f              — always 0
//   (unk_c..unk_f: 10 bytes. Purpose not fully determined; may encode
//    kfm.xml Unknown Int 1/2 and Unknown Float 1/2 in a compact layout.)
//
//   --- Sequences section ---
//   [u32]        section_type_sequences — always 1
//   [u8]         len("Sequences") = 9
//   [9 bytes]    "Sequences"
//   [u32]        declared_sequence_count (UNRELIABLE; may undercount — see below)
//     per entry (repeat until "Sequence Groups" section marker detected):
//     [u32]      entry_type       — 2 = sequence group name, 3 = animation sequence
//     [u8]       name_len
//     [n bytes]  name
//     [u32]      entry_id         — for type 3: NiKFMTool event code / sequence ID
//                                   for type 2: count of animations in this group
//
//   --- Sequence Groups section ---
//   [u32]        section_type_groups — always 4
//   [u8]         len("Sequence Groups") = 15
//   [15 bytes]   "Sequence Groups"
//   [u32]        group_count        — always 0 in observed client files
//
//   --- Animation table ---
//   [u32]        animation_count    — number of type-3 (animation) entries
//     per animation (animation_count times):
//     [u32]      event_code         — NiKFMTool sequence ID (same values as type-3
//                                     entry_ids, but may be in a different order)
//     [u32]      num_transitions    — transition count from this animation (often 0)
//     [u32]      unk_anim_field     — purpose unknown (always 0 in observed files)
//     [u8]       unk_anim_byte_1    — purpose unknown (always 0)
//     [u8]       unk_anim_byte_2    — purpose unknown (always 0)
//     [u8]       unk_anim_byte_3    — purpose unknown (always 0)
//     (= 15 bytes per entry; when num_transitions > 0, transition data may follow
//      inline — not yet observed in client files)
//
//   --- Footer ---
//   [variable]   remaining data     — varies per file; stored as raw bytes
//
// IMPORTANT: The declared_sequence_count in the Sequences section header is
// NOT reliable. In complex files (e.g. mf_darkling.settings with 465 entries
// declares count=6), the parser reads entries until it detects the
// "Sequence Groups" section marker (u32(4) + u8(15) + "Sequence Groups").

struct SettingsError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Per-entry in the "Sequences" section.
// entry_type 2 = sequence group (category name); entry_type 3 = animation sequence.
struct SettingsSequence {
    // Entry type discriminator.
    //   2 = sequence group / category name (e.g. "Attack", "Emotes", "Movement").
    //       entry_id holds the number of type-3 animations belonging to this group.
    //   3 = animation sequence (e.g. "mf_a_g_polarm-attack-slam").
    //       entry_id holds the NiKFMTool event code (unique sequence ID).
    // Identified from NiKFMTool::ReadBinaryKFM debug variable "eStoredType"
    // and structural analysis of 76 .settings files.
    uint32_t entry_type = 0;

    std::string name;

    // For entry_type 3 (animation): NiKFMTool event code / sequence ID.
    //   Matches the event_code field in the animation table at the end of the file.
    //   Named "uiSequenceID" in NiKFMTool debug symbols.
    // For entry_type 2 (group): count of animation entries belonging to this group.
    uint32_t entry_id = 0;
};

// Per-entry in the animation table (after "Sequence Groups" section).
struct SettingsAnimation {
    uint32_t event_code = 0;       // NiKFMTool sequence ID
    uint32_t num_transitions = 0;  // transition count (0 = no transitions from this anim)
    uint32_t unk_field = 0;        // purpose unknown (always 0 in observed files)
    uint8_t  unk_byte_1 = 0;       // purpose unknown (always 0 in observed files)
    uint8_t  unk_byte_2 = 0;       // purpose unknown (always 0 in observed files)
    uint8_t  unk_byte_3 = 0;       // purpose unknown (always 0 in observed files)
};

struct SettingsFile {
    std::string version; // "2.3.0" in all client files

    // --- Header fields (22 bytes between version string and "Sequences" marker) ---
    // Identified from NiKFMTool::ReadBinaryKFM via Ghidra RE and kfm.xml spec.
    uint32_t header_flags = 0;         // always 1; kfm.xml "Unknown Byte" (as u32)
    uint32_t model_filename_len = 0;   // always 0 (empty NIF model filename)
    uint32_t unk_c = 0;               // varies; purpose not fully determined
    uint8_t  unk_d = 0;               // varies (0, 63, 64, or 192)
    uint32_t unk_e = 0;               // varies (0, 0xF5, or 0xFFFFFFFF)
    uint8_t  unk_f = 0;               // always 0

    // Section type identifier preceding the "Sequences" marker. Always 1.
    uint32_t section_type_sequences = 0;

    // All entries from the "Sequences" section, including both animation
    // sequences (type=3) and sequence group names (type=2).
    // The declared count in the file header is unreliable; this vector
    // contains ALL entries read until the "Sequence Groups" marker.
    std::vector<SettingsSequence> sequences;

    // Section type identifier preceding the "Sequence Groups" marker. Always 4.
    uint32_t section_type_groups = 0;

    // Sequence group count from the "Sequence Groups" section.
    // Always 0 in observed client files.
    uint32_t group_count = 0;

    // Animation table entries. One per type-3 (animation) sequence.
    // Contains event codes and transition data for each animation.
    std::vector<SettingsAnimation> animations;

    // Raw bytes remaining after the animation table — footer data whose
    // structure varies per file. Stored raw for forward compatibility.
    std::vector<uint8_t> footer_bytes;
};
} // namespace lu::assets
