#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// References:
//   - kfm.xml (github.com/niftools/kfmxml) — KFM format field definitions
//   - lu_formats/files/kfm.ksy — complete KFM 2.2.0.0b layout as shipped by LU
//
// KFM (Keyframe Manager) file: binds an actor's NIF model to its animation
// sequences (.kf files) and defines the transition graph between sequences
// (blend/morph/crossfade/chain), consumed by NiActorManager.
//
// Layout (all LU files are version 2.2.0.0b):
//   ";Gamebryo KFM File Version 2.2.0.0b\n"   — text header line (';' included)
//   [u8]  endian                               — 1 = little-endian (always)
//   [u4_str] model_path                        — path to the actor's .nif (backslashes,
//                                                usually ".\"-prefixed; preserved verbatim)
//   [u4_str] model_root                        — name of the scene-graph root to animate
//   [u32] default_sync_transition_type
//   [u32] default_non_sync_transition_type
//   [f32] default_sync_transition_duration
//   [f32] default_non_sync_transition_duration
//   [u32] num_sequences + sequences
//   [u32] num_sequence_groups + groups
//
// KF files themselves are standard NIF containers (parse with nif_parse).

struct KfmError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Transition types (kfm.ksy enum transition_type).
enum class KfmTransitionType : uint32_t {
    Blend = 0,
    Morph = 1,
    Crossfade = 2,
    Chain = 3,
    DefaultSync = 4,     // use the file-level default sync transition
    DefaultNonSync = 5,  // use the file-level default non-sync transition
};

// Named text-key pair mapping a moment in the outgoing sequence to a moment in
// the incoming one (transition alignment points).
struct KfmBlendPair {
    std::string start_key;
    std::string target_key;
};

// One link of a Chain transition: play sequence_id for duration seconds.
struct KfmChainSequence {
    uint32_t sequence_id = 0;
    float duration = 0.0f;
};

// A transition edge from the owning sequence to dest_id. The extended payload
// (duration/blend_pairs/chain_sequences) is only present in the file when
// stored_type is not one of the two Default* types.
struct KfmTransition {
    uint32_t dest_id = 0;
    uint32_t stored_type = 0; // KfmTransitionType
    float duration = 0.0f;
    std::vector<KfmBlendPair> blend_pairs;
    std::vector<KfmChainSequence> chain_sequences;

    bool has_ext() const {
        return stored_type != static_cast<uint32_t>(KfmTransitionType::DefaultSync) &&
               stored_type != static_cast<uint32_t>(KfmTransitionType::DefaultNonSync);
    }
};

// One animation sequence: NiControllerSequence anim_index inside kf_filename,
// addressed by the actor-unique id (NiActorManager event code).
struct KfmSequence {
    uint32_t id = 0;
    std::string kf_filename; // preserved verbatim (backslashes, ".\"-prefix)
    uint32_t anim_index = 0;
    std::vector<KfmTransition> transitions;
};

// Per-sequence playback parameters inside a sequence group.
struct KfmSequenceInfo {
    uint32_t sequence_id = 0;
    int32_t priority = 0;
    float weight = 0.0f;
    float ease_in_time = 0.0f;
    float ease_out_time = 0.0f;
    uint32_t sync_sequence_id = 0;
};

// Named set of sequences activated together (NiSequenceStreamHelper groups).
struct KfmSequenceGroup {
    uint32_t group_id = 0;
    std::string name;
    std::vector<KfmSequenceInfo> sequence_infos;
};

struct KfmFile {
    // Exact text header line including the leading ';' and trailing '\n',
    // preserved verbatim for byte-identical round-trips through kfm_write.
    std::string header_line;
    uint8_t endian = 1; // 1 = little-endian (the only value LU ships)

    std::string model_path; // exact as stored — backslash separators, ".\"-prefix
    std::string model_root;

    uint32_t default_sync_transition_type = 0;     // KfmTransitionType
    uint32_t default_non_sync_transition_type = 0; // KfmTransitionType
    float default_sync_transition_duration = 0.0f;
    float default_non_sync_transition_duration = 0.0f;

    std::vector<KfmSequence> sequences;
    std::vector<KfmSequenceGroup> sequence_groups;

    // model_path with separators normalized for lookups on non-Windows
    // filesystems (the on-disk field keeps its original backslashes).
    std::string model_path_normalized() const {
        std::string p = model_path;
        for (char& c : p) {
            if (c == '\\') c = '/';
        }
        if (p.rfind("./", 0) == 0) p.erase(0, 2);
        return p;
    }
};
} // namespace lu::assets
