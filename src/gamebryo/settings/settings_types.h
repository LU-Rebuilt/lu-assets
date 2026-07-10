#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// Gamebryo compiled-binary settings format (.settings) — an animation-manager
// sequencing format alongside .nif/.kfm actor files (737 files per client version;
// 3754 total across every available client version/leak). Defines animation
// sequences, sequence groups (categories), and inter-sequence transitions.
//
// This is a binary encoding of the same data model as the .kfm text format
// (";Gamebryo KFM File Version x.x.x"). Instead of text lines, .settings uses
// u8-length-prefixed section markers ("Sequences", "Sequence Groups") and
// binary-encoded fields. Written by an unidentified NetDevil build tool — despite
// this format's association with animation sequencing, SequenceEditor.exe (found in
// client trees) turned out to be an unrelated OpenAL audio tool with no Gamebryo
// code, so that attribution in earlier documentation was wrong.
//
// The "Sequences"/"Sequence Groups" section markers are NOT string constants the
// retail engine compares against — those bytes don't appear anywhere in
// legouniverse.exe (confirmed via Ghidra byte-pattern search). The real reader must
// dispatch purely on the numeric section_type tags (1 and 4); the string markers only
// matter to our own reader as a byte-exact round-trip target. Layout confirmed via
// the kfm.xml format specification (niftools/kfmxml) and structural/statistical
// analysis of all 3754 real .settings files across every available client version.
//
// Binary layout:
//   --- Header ---
//   [u8]         version_len
//   [n bytes]    version string (always "2.3.0" in client)
//   [u32]        header_flags       — always 1 (maps to kfm.xml "Unknown Byte")
//   [u32]        model_filename_len — always 0 (i32 length of NIF filename; empty)
//   [u32]        unk_c              — see below: stale tool memory, not semantic data
//   [u8]         unk_d              — see below: correlates with unk_c, same origin
//   [u32]        unk_e              — see below: mostly two dominant "mode" values
//   [u8]         unk_f              — always 0 (3754/3754 real files)
//
//   unk_c/unk_d/unk_e investigation (corpus survey of all 3754 real .settings files
//   across every available client version/leak; static Ghidra RE of legouniverse.exe
//   found no code that compares against the "Sequences"/"Sequence Groups" strings —
//   those bytes don't exist anywhere in the retail binary, meaning the real engine
//   reader dispatches on the numeric section-type tags alone, not string markers.
//   SequenceEditor.exe was checked as the presumed authoring tool per this format's
//   README, but its imports/strings show it's an unrelated OpenAL audio tool with no
//   Gamebryo/NiKFMTool code at all — that attribution appears to be wrong):
//     - unk_c is 0 in 3317/3754 files (88%); the remaining 437 have one of only ~60
//       distinct high-entropy 32-bit values, each shared verbatim across small groups
//       of files that are clearly from the same authoring batch (e.g. amb_frog-gray/
//       red/yellow.settings all share unk_c=0xd1d7de40; mf_kipper_paradox/sentinel
//       .settings share 0xd66bdb20). Cross-file identical values for otherwise
//       unrelated content, with no correlation to sequence/animation count or file
//       size, matches the classic signature of a stale/uninitialized in-memory
//       pointer or heap address serialized directly by the authoring tool's save
//       routine — not a field the game engine reads or a value with in-file meaning.
//     - unk_d is 0 whenever unk_c is 0, and one of {63, 64, 191, 192} whenever
//       unk_c is non-zero (0x3F/0x40/0xBF/0xC0 — plausible as bytes adjacent to the
//       same leaked pointer/struct in memory). Same non-semantic origin as unk_c.
//     - unk_e is 0xFFFFFFFF in 3534/3754 files (94%) and 0xF5 (245) in 136/3754
//       (all large/complex files, seq count 144+); the remaining handful of files
//       have small values (0, 3, 10, 21). 0xFFFFFFFF loosely tracks "this file
//       defines no type-2 sequence-group entries" (3300/3534 such files truly have
//       zero groups) but isn't an exact predicate — 234 counterexamples exist with
//       real groups defined despite unk_e == 0xFFFFFFFF. No exact formula found
//       against sequence count, type-2 group-entry count, or animation count.
//       Likely another tool-session artifact rather than meaningful data.
//   Conclusively resolved: NOT semantically meaningful game data (preserved verbatim
//   for byte-perfect round-trip only). Further resolution would require either live
//   debugging of the actual authoring tool (never identified) or the tool's source.
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
//     [u32]      entry_id         — for type 3: event code / sequence ID
//                                   for type 2: count of animations in this group
//
//   --- Sequence Groups section ---
//   [u32]        section_type_groups — always 4
//   [u8]         len("Sequence Groups") = 15
//   [15 bytes]   "Sequence Groups"
//   [u32]        group_count        — 0 in nearly every observed client file
//     per group entry (group_count times; rare — confirmed from the one known
//     real sample, mf_main_low_hardware/software.settings):
//     [u32]      entry_count        — number of animations belonging to this group
//     [u8]       name_len
//     [n bytes]  name
//     [u32]      unk_trailing       — purpose unknown; 0 in the one known real sample
//
//   --- Animation table ---
//   [u32]        animation_count    — number of type-3 (animation) entries
//     per animation (animation_count times):
//     [u32]      event_code         — sequence ID (same values as type-3 entry_ids,
//                                     but may be in a different order)
//     [u32]      num_transitions    — transition count from this animation
//     [u32]      unk_anim_field     — always 0 (confirmed across all 100085 real
//                                     animation-table entries in the full corpus)
//     [u8]       unk_anim_byte_1    — always 0 (same exhaustive confirmation)
//     [u8]       unk_anim_byte_2    — always 0 (same exhaustive confirmation)
//     [u8]       unk_anim_byte_3    — always 0 (same exhaustive confirmation)
//     (= 15 bytes per entry. num_transitions is also always 0 across the entire
//      corpus, so the "transition data follows inline" case is never exercised by
//      any real file — inter-sequence transitions appear to be unused in practice.)
//
//   --- Footer ---
//   [variable]   remaining data     — varies per file; stored as raw bytes
//
// IMPORTANT: declared_sequence_count is NOT reliable as a parse bound (e.g. one real
// file has 465 entries but declares count=6) — the parser reads entries until it
// detects the "Sequence Groups" section marker instead.

struct SettingsError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Per-entry in the "Sequences" section.
// entry_type 2 = sequence group (category name); entry_type 3 = animation sequence.
struct SettingsSequence {
    // Entry type discriminator (confirmed via full-corpus tally: 5422 type-2 and
    // 100085 type-3 entries across all 3754 real .settings files).
    //   2 = sequence group / category name (e.g. "Attack", "Emotes", "Movement").
    //       entry_id holds the number of type-3 animations belonging to this group.
    //   3 = animation sequence (e.g. "mf_a_g_polarm-attack-slam").
    //       entry_id holds the sequence's event code.
    uint32_t entry_type = 0;

    std::string name;

    // For entry_type 3 (animation): event code / sequence ID.
    //   Matches the event_code field in the animation table at the end of the file.
    // For entry_type 2 (group): count of animation entries belonging to this group.
    uint32_t entry_id = 0;
};

// Per-entry in the "Sequence Groups" section (a named category, e.g. "Lookat_01",
// listing how many animations it contains). Genuinely rare: only 1/3754 real client
// .settings files (mf_main_low_hardware/software.settings, identical across every
// client version) has group_count != 0. Layout confirmed against that sample: no
// per-member payload follows the name — the animation table starts immediately after
// the last group entry, resynced via animation_count.
struct SettingsGroupEntry {
    uint32_t entry_count = 0; // number of animations belonging to this group
    std::string name;
    uint32_t unk_trailing = 0; // purpose unknown; 0 in the one known real sample
};

// Per-entry in the animation table (after "Sequence Groups" section). See the
// file-level comment above for the full corpus-survey findings on the unk fields.
struct SettingsAnimation {
    uint32_t event_code = 0;       // sequence ID
    uint32_t num_transitions = 0;  // always 0 in every real file (see file comment)
    uint32_t unk_field = 0;        // always 0 in every real file (see file comment)
    uint8_t  unk_byte_1 = 0;       // always 0 in every real file (see file comment)
    uint8_t  unk_byte_2 = 0;       // always 0 in every real file (see file comment)
    uint8_t  unk_byte_3 = 0;       // always 0 in every real file (see file comment)
};

struct SettingsFile {
    std::string version; // "2.3.0" in nearly every client file; 22/3754 use "2.2.2"
                          // with an identical header layout to 2.3.0

    // --- Header fields (22 bytes between version string and "Sequences" marker) ---
    // See the file-level comment above for the full corpus-survey findings on unk_c/d/e/f.
    uint32_t header_flags = 0;         // always 1; kfm.xml "Unknown Byte" (as u32)
    uint32_t model_filename_len = 0;   // always 0 (empty NIF model filename)
    uint32_t unk_c = 0;               // stale tool memory, not semantic data
    uint8_t  unk_d = 0;               // correlates with unk_c, same origin
    uint32_t unk_e = 0;               // mostly two dominant "mode" values
    uint8_t  unk_f = 0;               // always 0

    // Section type identifier preceding the "Sequences" marker. Always 1.
    uint32_t section_type_sequences = 0;

    // The count field as written in the file. UNRELIABLE as a parse bound (complex
    // files undercount it — see file comment), but preserved verbatim so writes are
    // byte-identical; sequences.size() is the real entry count.
    uint32_t declared_sequence_count = 0;

    // All entries from the "Sequences" section, including both animation
    // sequences (type=3) and sequence group names (type=2).
    // The declared count in the file header is unreliable; this vector
    // contains ALL entries read until the "Sequence Groups" marker.
    std::vector<SettingsSequence> sequences;

    // Section type identifier preceding the "Sequence Groups" marker. Always 4.
    uint32_t section_type_groups = 0;

    // Sequence group count from the "Sequence Groups" section.
    // 0 in nearly every observed client file (see SettingsGroupEntry for the one
    // known exception). group_entries.size() == group_count always.
    uint32_t group_count = 0;

    // Group entries when group_count != 0 (see SettingsGroupEntry).
    std::vector<SettingsGroupEntry> group_entries;

    // Animation table entries. One per type-3 (animation) sequence.
    // Contains event codes and transition data for each animation.
    std::vector<SettingsAnimation> animations;

    // Raw bytes remaining after the animation table — footer data whose
    // structure varies per file. Stored raw for forward compatibility.
    std::vector<uint8_t> footer_bytes;
};
} // namespace lu::assets
