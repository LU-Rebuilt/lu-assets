#pragma once
// References:
//   - lcdr/lu_formats (github.com/lcdr/lu_formats) fev.ksy — complete FEV binary spec

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// FEV (FMOD Event) file parser.
// 42 .fev files in the client's audio/ directory.
// Format fully reverse-engineered from lu_formats KSY.
//
// Binary layout (all little-endian):
//   Header: magic "FEV1", version 0x00004000, two u32 checksums
//   Manifest: count + array of (type, value) pairs cataloging all objects
//   Project name (u32-prefixed string)
//   Banks: load mode, max streams, checksum, name
//   Event category tree (recursive): name, volume, pitch, max streams, playback behavior, subcategories
//   Event groups (recursive): name, user properties, subgroups, events
//   Events: simple (single layer) or complex (multiple layers, parameters, user properties)
//   Layers: priority, control parameter, sound instances, effect envelopes
//   Sound instances: definition index, timing, loop mode, autopitch, fade types
//   Effect envelopes: name, points with position/value/curve shape
//   Event parameters: name, velocity, min/max, flags, seek speed
//   Sound definition configs: play mode, spawn times, volume/pitch randomization
//   Sound definitions: name, config index, waveforms
//   Waveforms: wavetable (file+bank+length), oscillator (type+freq), dont_play, programmer
//   Reverb definitions: name, levels, decay, reflections, delays, diffusion, density, crossovers
//   Music data: chunk-based tree with comp, sett, themes, cues, sounds, parameters,
//               segments, samples, links, timelines, conditions

struct FevError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr uint32_t FEV1_MAGIC = 0x31564546; // "FEV1"
inline constexpr uint32_t RIFF_MAGIC = 0x46464952; // "RIFF"
inline constexpr uint32_t FEV_VERSION = 0x00004000;

// ---------------------------------------------------------------------------
// Manifest
// ---------------------------------------------------------------------------

enum class FevManifestType : uint32_t {
    PROJECT_VERSION_OR_FLAG = 0x00, // Always 1 in LU; stored at alloc struct +0x10 by FUN_1002dac0
    BANK_COUNT = 0x01,
    EVENT_CATEGORY_COUNT = 0x02,
    EVENT_GROUP_COUNT = 0x03,
    USER_PROPERTY_COUNT = 0x04,
    EVENT_PARAMETER_COUNT = 0x05,
    EFFECT_ENVELOPE_COUNT = 0x06,
    ENVELOPE_POINT_COUNT = 0x07,
    SOUND_INSTANCE_COUNT = 0x08,
    LAYER_COUNT = 0x09,
    SIMPLE_EVENT_COUNT = 0x0A,
    COMPLEX_EVENT_COUNT = 0x0B,
    REVERB_DEFINITION_COUNT = 0x0C,
    WAVEFORM_WAVETABLE_COUNT = 0x0D,
    WAVEFORM_OSCILLATOR_COUNT = 0x0E,
    WAVEFORM_DONT_PLAY_ENTRY_COUNT = 0x0F,
    WAVEFORM_PROGRAMMER_SOUND_COUNT = 0x10,
    SOUND_DEFINITION_COUNT = 0x11,
    RESERVED_0x12 = 0x12, // Always 0; read into manifest array but never used by runtime
    PROJECT_NAME_SIZE = 0x13,
    BANK_NAMES_TOTAL_SIZE = 0x14,
    EVENT_CATEGORY_NAMES_TOTAL_SIZE = 0x15,
    EVENT_GROUP_NAMES_TOTAL_SIZE = 0x16,
    USER_PROPERTY_NAMES_TOTAL_SIZE = 0x17,
    USER_PROPERTY_STRING_VALUES_TOTAL_SIZE = 0x18,
    EVENT_PARAMETER_NAMES_TOTAL_SIZE = 0x19,
    EFFECT_ENVELOPE_NAMES_TOTAL_SIZE = 0x1A,
    EVENT_NAMES_TOTAL_SIZE = 0x1B,
    EVENT_INSTANCE_CATEGORY_NAMES_TOTAL_SIZE = 0x1C,
    REVERB_DEFINITION_NAMES_TOTAL_SIZE = 0x1D,
    WAVETABLE_FILE_NAMES_TOTAL_SIZE = 0x1E,
    WAVETABLE_BANK_NAMES_TOTAL_SIZE = 0x1F,
    SOUND_DEFINITION_NAMES_TOTAL_SIZE = 0x20,
};

struct FevManifestEntry {
    FevManifestType type;
    uint32_t value = 0;
};

// ---------------------------------------------------------------------------
// Banks
// ---------------------------------------------------------------------------

// Bank streaming/loading mode — corresponds to FMOD_MODE flags:
//   FMOD_OPENMEMORY             (0x00000100) = "Decompress into memory"
//   FMOD_CREATECOMPRESSEDSAMPLE (0x00000200) = "Load into memory"
//   FMOD_CREATESTREAM           (0x00000080) = "Stream from disk"
// Verified from bank comment in lu_formats fev.ksy
enum class FevBankLoadMode : uint32_t {
    STREAM_FROM_DISK       = 0x00000080,  // FMOD_CREATESTREAM
    DECOMPRESS_INTO_MEMORY = 0x00000100,  // FMOD_OPENMEMORY
    LOAD_INTO_MEMORY       = 0x00000200,  // FMOD_CREATECOMPRESSEDSAMPLE
};

struct FevBank {
    FevBankLoadMode load_mode = FevBankLoadMode::DECOMPRESS_INTO_MEMORY;
    int32_t max_streams = 0;
    // FSB-FEV bank matching checksums: two u32 values that are stored verbatim in
    // the FSB4 header at the "reserved" field (offset 24, 8 bytes). FMOD Event reads
    // these when loading a bank to verify the FSB was compiled from the same project
    // as the FEV. Verified from SoundBank_StoreFevChecksum (FUN_10035780) @ 10035780
    // in fmod_event.dll, and confirmed by decrypting LU's FSB files with
    // fsb_extract_bank_checksums(). Use fev_verify_bank_checksum() to validate.
    uint32_t fsb_checksum[2] = {0, 0};
    std::string name;
};

// ---------------------------------------------------------------------------
// Event categories (recursive tree)
// ---------------------------------------------------------------------------

enum class FevMaxPlaybackBehavior : uint32_t {
    STEAL_OLDEST = 0,
    STEAL_NEWEST = 1,
    STEAL_QUIETEST = 2,
    JUST_FAIL = 3,
    JUST_FAIL_IF_QUIETEST = 4,
};

struct FevEventCategory {
    std::string name;
    // Linear volume multiplier (1.0 = unity gain = 0 dB, 0.0 = silence).
    // FDP <volume_db> requires dB: convert with 20*log10(volume).
    // Do NOT confuse with FevEvent::volume which is already stored in dB.
    float volume = 1.0f;
    float pitch = 0.0f;
    int32_t max_streams = -1;
    FevMaxPlaybackBehavior max_playback_behavior = FevMaxPlaybackBehavior::STEAL_OLDEST;
    std::vector<FevEventCategory> subcategories;
};

// ---------------------------------------------------------------------------
// User properties
// ---------------------------------------------------------------------------

enum class FevUserPropertyType : uint32_t {
    INTEGER = 0,
    FLOAT = 1,
    STRING = 2,
};

struct FevUserProperty {
    std::string name;
    FevUserPropertyType type = FevUserPropertyType::INTEGER;
    // Value stored in the variant matching the type
    std::variant<uint32_t, float, std::string> value;
};

// ---------------------------------------------------------------------------
// Effect envelope points
// ---------------------------------------------------------------------------

enum class FevCurveShape : uint32_t {
    FLAT_ENDED = 1,
    LINEAR = 2,
    LOG = 4,
    FLAT_MIDDLE = 8,
};

struct FevEffectEnvelopePoint {
    uint32_t position = 0;
    float value = 0.0f;
    FevCurveShape curve_shape = FevCurveShape::LINEAR;
};

// ---------------------------------------------------------------------------
// Effect envelopes
// ---------------------------------------------------------------------------

// Effect envelope fields verified from FUN_1001f240 (fmod_eventimpl_complex.cpp)
// and FUN_1000f370 (envelope init), fmod_event.dll.
// XML mapping: <envelope> element in .fdp project files.
struct FevEffectEnvelope {
    int32_t control_parameter_index = -1; // Index of event parameter controlling this envelope (-1 = unset)
    std::string name;                     // DSP target name (e.g. "Volume", "Pitch", "3D Pan Level")
    int32_t dsp_effect_index = -1;        // Index of the DSP effect this envelope targets (-1 = none)
    // DSP target type bitfield (version >= 0x260000). In older FEV versions, these bits
    // were derived from the envelope name string at load time:
    //   0x0008 = Volume          0x0010 = Pitch            0x0020 = Pan
    //   0x0040 = Time offset     0x0080 = Surround pan     0x0100 = 3D Speaker spread
    //   0x0200 = Reverb Level    0x0400 = 3D Pan Level     0x0800 = Reverb Balance
    //   0x1000 = Spawn Intensity
    // Bit 14 (0x4000) is masked out on read.
    // Ghidra: FUN_1001f240 @ 1001f5c0 (fmod_event.dll, fmod_eventimpl_complex.cpp).
    uint32_t envelope_flags = 0;
    // Extended envelope flags (version >= 0x390000). Stored at envelope object offset 0x14.
    // Bit 0 (0x01) is masked out on read — set internally when no standard DSP name matches
    // (indicates a user DSP effect envelope).
    // Bit 1 (0x02) is set internally if the memory allocator supports tracking.
    // Remaining bits: envelope behavior flags from the FEV binary.
    // Ghidra: FUN_1001f240 @ 1001f690 (fmod_event.dll).
    uint32_t envelope_flags2 = 0;
    std::vector<FevEffectEnvelopePoint> points;
    uint8_t mapping_data[4] = {};         // Raw mapping bytes stored at envelope's mapping offset
    uint32_t enabled = 0;                 // 0 = disabled, nonzero = enabled (version >= 0x1a0000)
};

// ---------------------------------------------------------------------------
// Sound instances
// ---------------------------------------------------------------------------

enum class FevSoundStartMode : uint32_t {
    IMMEDIATE = 0,
    WAIT_FOR_PREVIOUS = 1,
};

enum class FevSoundLoopMode : uint16_t {
    LOOP_AND_CUTOFF = 0,
    ONESHOT = 1,
    LOOP_AND_PLAY_TO_END = 2,
};

enum class FevAutopitchParameter : uint16_t {
    EVENT_PRIMARY_PARAMETER = 0,
    LAYER_CONTROL_PARAMETER = 2,
};

enum class FevAutopitchEnabled : uint32_t {
    YES = 1,
    NO = 2,
};

// Sound instance fields verified from FUN_10028eb0 (fmod_soundinstancei.cpp)
// and FUN_10029710 (initializer), fmod_event.dll.
struct FevSoundInstance {
    uint16_t sound_definition_index = 0;
    float start_position = 0.0f;
    float length = 0.0f;
    FevSoundStartMode start_mode = FevSoundStartMode::IMMEDIATE;
    FevSoundLoopMode loop_mode = FevSoundLoopMode::ONESHOT;
    FevAutopitchParameter autopitch_parameter = FevAutopitchParameter::EVENT_PRIMARY_PARAMETER;
    int32_t loop_count = -1;  // -1 = disabled
    FevAutopitchEnabled autopitch_enabled = FevAutopitchEnabled::NO;
    float autopitch_reference = 0.0f;
    float autopitch_at_min = 0.0f;
    float fine_tune = 0.0f;
    float volume = 1.0f;                // Sound instance volume multiplier (default 1.0)
    float volume_randomization = 0.0f;  // Random volume variation
    float pitch = 0.0f;                 // Sound instance pitch adjustment
    uint32_t fade_in_type = 0;
    uint32_t fade_out_type = 0;
};

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

struct FevLayer {
    // These fields only present for complex event layers (not simple event layer)
    uint8_t layer_flags[2] = {};    // Layer behavior flags (e.g. mute, solo)
    int16_t priority = -1;          // -1 = use event priority
    int16_t control_parameter_index = -1; // -1 = unset
    // Always present
    std::vector<FevSoundInstance> sound_instances;
    std::vector<FevEffectEnvelope> effect_envelopes;
};

// ---------------------------------------------------------------------------
// Event parameter flags (bitfield)
// ---------------------------------------------------------------------------

// Parameter behavior flags (u32). Bit layout confirmed via Ghidra RE of
// fmod_designercl.exe FUN_00414752 (parameter flag builder) and
// fmod_event.dll FUN_1001e700 (complex event impl loadFromBuffer).
//
// Byte 0 bit layout:
//   bit 0 (0x01): primary — this is the event's primary parameter
//   bit 1 (0x02): loop — parameter loops (loopmode=0 in FDP XML)
//   bit 2 (0x04): oneshot_and_stop_event — stop event at max (loopmode=1)
//   bit 3 (0x08): oneshot — stop at max (loopmode=2)
//   bit 4 (0x10): auto_distance — runtime-only, set if name is "(distance)"
//   bit 5 (0x20): auto_listener_angle — runtime-only, set if name is "(listener angle)"
//   bits 4+5 (0x30): auto_event_angle — runtime-only, set if name is "(event angle)"
//   bit 6 (0x40): keyoff_on_silence — send key-off when event goes silent
//   bit 7 (0x80): reserved (always 0 in FEV files)
// Bytes 1-3: reserved (always 0 in FEV files)
//
// Bits 1,2,3 (loop modes) are mutually exclusive.
// Bits 4,5 are never set in FEV files — the runtime sets them after loading
// based on the parameter name string match.
struct FevEventParameterFlags {
    bool primary = false;               // bit 0 (0x01)
    bool loop = false;                  // bit 1 (0x02)
    bool oneshot_and_stop_event = false; // bit 2 (0x04)
    bool oneshot = false;               // bit 3 (0x08)
    bool keyoff_on_silence = false;     // bit 6 (0x40)
    uint8_t raw[4] = {};
};

// ---------------------------------------------------------------------------
// Event parameters
// ---------------------------------------------------------------------------

struct FevEventParameter {
    std::string name;
    float velocity = 0.0f;
    float minimum_value = 0.0f;
    float maximum_value = 0.0f;
    FevEventParameterFlags flags;
    float seek_speed = 0.0f;
    // After seek_speed: a u32 value, a u32 count, then count additional u32 values.
    // The ksy documents this as "size: 8, padding, always 0" but the actual format
    // has a variable-length tail when the count is nonzero.
    uint32_t unknown_value = 0;
    std::vector<uint32_t> unknown_extra;  // length given by a u32 count prefix
};

// ---------------------------------------------------------------------------
// 3D flags (bitfield from 4 bytes)
// ---------------------------------------------------------------------------

// 3D flags bitfield — FMOD_MODE-derived u32 controlling 3D spatialization.
// Stored at runtime EventI offset +0x40. Bit layout verified from
// newcontent.fev binary analysis and Ghidra RE of fmod_event.dll
// EventI::getPropertyByIndex @ 10017550 / setPropertyByIndex @ 10017a40.
//
// Byte 0 (u32 bits 0-7):
//   bit 7 (0x80): FMOD_CREATESTREAM — stream from disk (typically 0 for events)
//   bit 6 (0x40): FMOD_SOFTWARE — software mixing (typically 0)
//   bit 5 (0x20): FMOD_HARDWARE — hardware mixing (typically 0)
//   bit 4 (0x10): mode_3d — FMOD_3D, x_3d events have 0x10 here
//   bit 3 (0x08): mode_2d — FMOD_2D, x_2d events have 0x08 here
//   bit 2 (0x04): FMOD_LOOP_BIDI
//   bit 1 (0x02): FMOD_LOOP_NORMAL
//   bit 0 (0x01): FMOD_LOOP_OFF
// Byte 1 (u32 bits 8-15): creation/open flags (typically 0 for events)
// Byte 2 (u32 bits 16-23):
//   bit 21 (0x200000): rolloff_linear — FMOD_3D_LINEARSQUAREROLLOFF
//   bit 20 (0x100000): rolloff_logarithmic — FMOD_3D_INVERSEROLLOFF (default)
//   bit 19 (0x080000): position_world_relative — FMOD_3D_WORLDRELATIVE
//   bit 18 (0x040000): position_head_relative — FMOD_3D_HEADRELATIVE
//   bit 17 (0x020000): unique — FMOD_UNIQUE
//   bit 16 (0x010000): ignore_geometry (FEV-specific packing; FMOD_MODE has this
//                       at bit 30 as FMOD_3D_IGNOREGEOMETRY, but binary analysis
//                       of FEV files shows it here)
// Byte 3 (u32 bits 24-31):
//   bit 30 (0x40000000): FMOD_3D_IGNOREGEOMETRY in FMOD_MODE (canonical position)
//   bit 26 (0x04000000): FMOD_3D_CUSTOMROLLOFF
struct FevEvent3DFlags {
    bool mode_2d = false;
    bool mode_3d = false;
    bool rolloff_linear = false;
    bool rolloff_logarithmic = false;
    bool position_world_relative = false;
    bool position_head_relative = false;
    bool unique = false;
    bool ignore_geometry = false;
    uint8_t raw[4] = {};
};

// ---------------------------------------------------------------------------
// Event flags (bitfield from 4 bytes)
// ---------------------------------------------------------------------------

// Event flags bitfield — stored at runtime EventI offset +0x60.
// Confirmed via Ghidra RE of fmod_event.dll EventI::getPropertyByIndex
// @ 10017550 / setPropertyByIndex @ 10017a40, and fmod_designercl.exe
// FUN_00426509 (oneshot flag writer).
//
// Byte 0 (u32 bits 0-7): reserved (always 0 in LU FEV files)
// Byte 1 (u32 bits 8-15): rolloff type flags (mutually exclusive):
//   bit 8  (0x0100): inverse rolloff (FMOD_EVENTPROPERTY_3D_ROLLOFF value 0)
//   bit 9  (0x0200): linear squared rolloff (value 1)
//   bit 10 (0x0400): linear rolloff (value 2)
//   bit 11 (0x0800): logarithmic rolloff (value 3)
//   bits 12-15: reserved
// Byte 2 (u32 bits 16-23):
//   bit 23 (0x800000): custom rolloff curve (FMOD_EVENTPROPERTY_3D_ROLLOFF value 4)
//   bit 19 (0x080000): oneshot/continuous flag.
//     Designer FUN_00426509: <oneshot>Yes</oneshot> CLEARS this bit;
//     <oneshot>No</oneshot> SETS it. So bit=1 may mean "continuous/not oneshot".
//   bits 22-20, 18-16: reserved
// Byte 3 (u32 bits 24-31): reserved
struct FevEventFlags {
    uint8_t rolloff_type = 0;  // Byte 1: rolloff type bits (0x100-0x800, 0x800000)
    bool oneshot = false;      // Bit 19 (0x80000): oneshot flag
    uint8_t raw[4] = {};       // Full raw 4-byte value for bit-level access
};

// ---------------------------------------------------------------------------
// Events (max playback behavior uses 1-based enum unlike category's 0-based)
// ---------------------------------------------------------------------------

enum class FevEventMaxPlaybackBehavior : uint32_t {
    STEAL_OLDEST = 1,
    STEAL_NEWEST = 2,
    STEAL_QUIETEST = 3,
    JUST_FAIL = 4,
    JUST_FAIL_IF_QUIETEST = 5,
};

enum class FevEventType : uint32_t {
    COMPLEX = 8,
    SIMPLE = 16,
};

struct FevEvent {
    FevEventType event_type = FevEventType::SIMPLE;
    std::string name;
    uint8_t guid[16] = {};
    // Linear volume multiplier (1.0 = unity gain = 0 dB).
    // Same unit as FevEventCategory::volume and FevSoundDefinitionConfig::volume.
    // FDP <volume_db> requires dB: convert with 20*log10(volume).
    float volume = 0.0f;
    float pitch = 0.0f;
    float pitch_randomization = 0.0f;
    float volume_randomization = 0.0f;
    uint16_t priority = 0;
    uint16_t max_instances = 0;   // Maximum simultaneous instances (0 = unlimited)
    uint32_t max_playbacks = 0;
    uint32_t steal_priority = 0;
    FevEvent3DFlags threed_flags;
    float threed_min_distance = 0.0f;
    float threed_max_distance = 0.0f;
    FevEventFlags event_flags;
    float speaker_l = 0.0f;
    float speaker_r = 0.0f;
    float speaker_c = 0.0f;
    float speaker_lfe = 0.0f;
    float speaker_lr = 0.0f;
    float speaker_rr = 0.0f;
    float speaker_ls = 0.0f;
    float speaker_rs = 0.0f;
    float threed_cone_inside_angle = 0.0f;
    float threed_cone_outside_angle = 0.0f;
    float threed_cone_outside_volume = 0.0f;
    FevEventMaxPlaybackBehavior max_playbacks_behavior = FevEventMaxPlaybackBehavior::STEAL_OLDEST;
    float threed_doppler_factor = 0.0f;
    float reverb_dry_level = 0.0f;
    float reverb_wet_level = 0.0f;
    float threed_speaker_spread = 0.0f;
    uint16_t fade_in_time = 0;
    uint16_t fade_in_time_flag = 0;
    uint16_t fade_out_time = 0;
    uint16_t fade_out_time_flag = 0;
    float spawn_intensity = 0.0f;
    float spawn_intensity_randomization = 0.0f;
    float threed_pan_level = 0.0f;
    uint32_t threed_position_randomization = 0;
    // Complex event: multiple layers, parameters, user properties
    // Simple event: single layer (no layer header fields)
    std::vector<FevLayer> layers;
    std::vector<FevEventParameter> parameters;   // Complex only
    std::vector<FevUserProperty> user_properties; // Complex only
    // Number of event category instance strings that follow in the binary.
    // 0 = use the default/root category; 1 = one category name string follows.
    // Read by fmod_event.dll FUN_10011660 @ 10012300 (fmod_eventgroupi.cpp):
    // if 0, assigns DAT_10046f48[0x2027] (default category); otherwise reads
    // that many u32-prefixed name strings for category lookup.
    // Ghidra: EventGroupI_loadFromBuffer @ 10011660, category read @ ~10012300.
    uint32_t category_instance_count = 0;
    std::string category;
};

// ---------------------------------------------------------------------------
// Event groups (recursive tree)
// ---------------------------------------------------------------------------

struct FevEventGroup {
    std::string name;
    std::vector<FevUserProperty> user_properties;
    std::vector<FevEventGroup> subgroups;
    std::vector<FevEvent> events;
};

// ---------------------------------------------------------------------------
// Sound definition configs
// ---------------------------------------------------------------------------

enum class FevPlayMode : uint32_t {
    SEQUENTIAL = 0,
    RANDOM = 1,
    RANDOM_NO_REPEAT = 2,
    SEQUENTIAL_EVENT_RESTART = 3,
    SHUFFLE = 4,
    PROGRAMMER_SELECTED = 5,
    SHUFFLE_GLOBAL = 6,
    SEQUENTIAL_GLOBAL = 7,
};

enum class FevPitchRandomizationBehavior : uint32_t {
    RANDOMIZE_EVERY_SPAWN = 0,
    RANDOMIZE_WHEN_TRIGGERED_BY_PARAMETER = 1,
    RANDOMIZE_WHEN_EVENT_STARTS = 2,
};

// Sound definition config fields verified from FUN_1002b3f0 (sound def config loader)
// and FUN_10038400 (initializer), fmod_event.dll.
// XML mapping: <sounddef> element in .fdp project files.
struct FevSoundDefinitionConfig {
    FevPlayMode play_mode = FevPlayMode::SEQUENTIAL;
    uint32_t spawn_time_min = 0;
    uint32_t spawn_time_max = 0;
    uint32_t maximum_spawned_sounds = 0;
    // All volume fields here are LINEAR multipliers (1.0 = unity gain = 0 dB).
    // FDP <volume_db> requires dB: convert with 20*log10(value).
    // Unlike FevEvent::volume which is already in dB.
    float volume = 0.0f;
    uint32_t volume_rand_method = 0;    // <volume_randmethod> — randomization distribution method
    float volume_random_min = 0.0f;     // <volume_random_min> — lower bound (linear)
    float volume_random_max = 0.0f;     // <volume_random_max> — upper bound (linear)
    float volume_randomization = 0.0f;  // Randomization strength (linear)
    float pitch = 0.0f;
    uint32_t pitch_rand_method = 0;     // <pitch_randmethod> — randomization distribution method
    float pitch_random_min = 0.0f;      // <pitch_random_min> — lower bound for pitch randomization
    float pitch_random_max = 0.0f;      // <pitch_random_max> — upper bound for pitch randomization
    float pitch_randomization = 0.0f;
    FevPitchRandomizationBehavior pitch_randomization_behavior =
        FevPitchRandomizationBehavior::RANDOMIZE_EVERY_SPAWN;
    float threed_position_randomization = 0.0f;
    uint16_t trigger_delay_min = 0;
    uint16_t trigger_delay_max = 0;
    uint16_t spawn_count = 0;
};

// ---------------------------------------------------------------------------
// Waveforms
// ---------------------------------------------------------------------------

enum class FevWaveformType : uint32_t {
    WAVETABLE = 0,
    OSCILLATOR = 1,
    DONT_PLAY = 2,
    PROGRAMMER = 3,
};

enum class FevOscillatorType : uint32_t {
    SINE = 0,
    SQUARE = 1,
    SAW_UP = 2,
    SAW_DOWN = 3,
    TRIANGLE = 4,
    NOISE = 5,
};

struct FevWavetableParams {
    std::string filename;
    std::string bank_name;
    // Percentage weight locked flag — from <percentagelocked> in .fdp XML.
    // When set, the waveform's selection weight is fixed and not auto-balanced.
    uint32_t percentage_locked = 0;
    uint32_t length_ms = 0;  // In milliseconds
};

struct FevOscillatorParams {
    FevOscillatorType type = FevOscillatorType::SINE;
    float frequency = 0.0f;
};

// dont_play and programmer have no parameters

struct FevWaveform {
    FevWaveformType type = FevWaveformType::WAVETABLE;
    uint32_t weight = 0;
    // Type-specific parameters
    std::variant<FevWavetableParams, FevOscillatorParams, std::monostate> params;
};

// ---------------------------------------------------------------------------
// Sound definitions
// ---------------------------------------------------------------------------

struct FevSoundDefinition {
    std::string name;
    uint32_t config_index = 0;
    std::vector<FevWaveform> waveforms;
};

// ---------------------------------------------------------------------------
// Reverb definitions
// ---------------------------------------------------------------------------

// Reverb definition — maps to FMOD_REVERB_PROPERTIES.
// Verified from EventProjectI_loadFromBuffer @ 1002bdf0 and FUN_10027880
// (reverb constructor), fmod_event.dll. The serialization order differs from
// the FMOD_REVERB_PROPERTIES struct layout; object slot indices (ppuVar6[N])
// given in comments for cross-reference with Ghidra RE.
struct FevReverbDefinition {
    std::string name;
    int32_t room = 0;                     // ppuVar6[10] — Room (master level), 0 to -10000
    int32_t room_hf = 0;                  // ppuVar6[11] — RoomHF, 0 to -10000
    float room_rolloff_factor = 0.0f;     // ppuVar6[33] — RoomRolloffFactor, default 0.0
    float decay_time = 1.0f;              // ppuVar6[13] — DecayTime (seconds), default 1.0
    float decay_hf_ratio = 1.0f;          // ppuVar6[14] — DecayHFRatio, default 1.0
    int32_t reflections = 0;              // ppuVar6[16] — Reflections, 1000 to -10000
    float reflections_delay = 0.0f;       // ppuVar6[17] — ReflectionsDelay (seconds)
    int32_t reverb = 0;                   // ppuVar6[21] — Late reverb level, 2000 to -10000
    float reverb_delay = 0.0f;            // ppuVar6[22] — ReverbDelay (seconds)
    float diffusion = 0.0f;               // ppuVar6[34] — default 0; may be Diffusion or internal EAX4 field
    float density = 0.0f;                 // ppuVar6[35] — default 0; may be Density or internal EAX4 field
    float hf_reference = 0.0f;            // ppuVar6[31] — HFReference (Hz), default 5000.0
    float room_lf = 0.0f;                 // ppuVar6[12] — RoomLF (version-conditional older path)
    float lf_reference = 0.0f;            // ppuVar6[15] — DecayLFRatio (older path) or LFReference (newer)
    // 16-byte EAX4 instance block (ppuVar6[6-9] in older-version path)
    int32_t instance = 0;                 // ppuVar6[6]  — EAX4 reverb instance index (0-3)
    int32_t environment = -1;             // ppuVar6[7]  — preset environment index (-1 = none)
    float env_size = 7.5f;                // ppuVar6[8]  — environment size (meters)
    float env_diffusion = 1.0f;           // ppuVar6[9]  — environment diffusion (0.0-1.0)
    int32_t room_lf_b = 0;               // second RoomLF occurrence in serialization
    // 48-byte extended properties block (12 × f32).
    // Fields confirmed from Ghidra EventProjectI_loadFromBuffer @ 1002bdf0.
    float reflections_pan[3] = {};        // ppuVar6[18-20] — FMOD_REVERB_PROPERTIES.ReflectionsPan
    float reverb_pan[3] = {};             // ppuVar6[23-25] — FMOD_REVERB_PROPERTIES.ReverbPan
    float echo_time = 0.25f;              // ppuVar6[26]    — EchoTime (seconds)
    float echo_depth = 0.0f;              // ppuVar6[27]    — EchoDepth
    float modulation_time = 0.25f;        // ppuVar6[28]    — ModulationTime (seconds)
    float modulation_depth = 0.0f;        // ppuVar6[29]    — ModulationDepth
    float air_absorption_hf = -5.0f;      // ppuVar6[30]    — AirAbsorptionHF (dB/m)
    // FMOD_REVERB_PROPERTIES.LFReference — the low-frequency crossover (Hz).
    // Serialized redundantly: once in the version-conditional block (lf_reference above),
    // once as the 12th float in this extended block, and once more as lf_reference_b below.
    // The runtime reader (fmod_event.dll @ 1002d5f0) overwrites ppuVar6[0x20] each time.
    // Ghidra: EventProjectI_loadFromBuffer @ 1002bdf0, extended block read at ~1002d5f0.
    float lf_reference_ext = 0.0f;        // ppuVar6[32]    — LFReference (Hz), redundant 2nd write
    float lf_reference_b = 0.0f;          // ppuVar6[32]    — LFReference (Hz), redundant 3rd write
    uint32_t flags = 0;                   // ppuVar6[36] — FMOD_REVERB_FLAGS bitmask (default 0x33F)
};

// ---------------------------------------------------------------------------
// Music data chunks
// ---------------------------------------------------------------------------

// Settings chunk (sett): global volume and reverb.
// Exactly 2 floats in all FEV versions (no additional fields in newer versions).
// Ghidra: fmod_event.dll FUN_1000c3f0 (fmod_musicparsing.cpp @ 0xac8) — reads 2 u32
// (volume, reverb) then creates MusicSystemI (0x148 bytes).
struct FevMdSett {
    float volume = 0.0f;
    float reverb = 0.0f;
};

// Theme data chunk (thmd)
enum class FevThemePlaybackMethod : uint8_t {
    SEQUENCED = 0,
    CONCURRENT = 1,
};

enum class FevThemeDefaultTransition : uint8_t {
    NEVER = 0,
    QUEUED = 1,
    CROSSFADE = 2,
};

enum class FevThemeQuantization : uint8_t {
    FREE = 0,
    ON_BAR = 1,
    ON_BEAT = 2,
};

struct FevMdThmd {
    uint32_t theme_id = 0;
    FevThemePlaybackMethod playback_method = FevThemePlaybackMethod::SEQUENCED;
    FevThemeDefaultTransition default_transition = FevThemeDefaultTransition::NEVER;
    FevThemeQuantization quantization = FevThemeQuantization::FREE;
    uint32_t transition_timeout = 0;
    uint32_t crossfade_duration = 0;
    std::vector<uint32_t> end_sequence_ids;
    std::vector<uint32_t> start_sequence_ids;
};

// Cue entry list (entl)
struct FevMdEntl {
    uint16_t names_length = 0;
    std::vector<uint32_t> ids;
    std::vector<std::string> cue_names;
};

// Sound/scene data chunk (scnd)
// Each scene has an ID for hash-table lookup and a list of cue instances.
// Ghidra: fmod_musicplayer.exe FUN_004c0d70 (fmod_compositionentities.cpp @ 0x515).
struct FevCueInstance {
    uint32_t cue_id = 0;
    // Condition or ordering ID for this cue instance within the scene.
    // Read alongside cue_id as paired u32 values in a bulk read.
    // Appears to control cue evaluation order or condition binding.
    // Observed as small sequential integers in LU FEV files.
    // Ghidra: fmod_musicplayer.exe FUN_004c0d70 @ 004c0ede (bulk read),
    // FUN_004be200 (storage at scene data offset 0x14).
    uint32_t condition_id = 0;
};

struct FevMdScnd {
    // Scene ID — u32 used as the hash key in FMOD::BucketHash for scene lookup.
    // Ghidra: fmod_musicplayer.exe @ 004c0f02, passed to FUN_004be200 as key.
    uint32_t scene_id = 0;
    std::vector<FevCueInstance> cue_instances;
};

// Segment data chunk (sgmd)
// Ghidra: fmod_musicplayer.exe FUN_004c2010 (fmod_compositionentities.cpp @ 0x77d),
//         fmod_designercl.exe FUN_00516f6f (sgmd writer).
struct FevMdSgmd {
    uint32_t segment_id = 0;
    // Segment length or beat count — u32 stored at CoreSegment offset 0x0c.
    // Written by designer vtable[0x20](), read before timeline_id.
    // Ghidra: fmod_musicplayer.exe @ 004c218c (read), fmod_designercl.exe @ 00516f99 (write).
    uint32_t segment_length = 0;
    uint32_t timeline_id = 0;
    uint8_t time_signature_beats = 0;
    uint8_t time_signature_beat_value = 0;
    float beats_per_minute = 0.0f;
    // Second float field — stored at CoreSegment offset 0x1c, written by designer vtable[0x18]().
    // Likely segment tempo modifier or length in seconds.
    // Ghidra: fmod_musicplayer.exe @ 004c21c0 (read), fmod_designercl.exe @ 005016b0 (write).
    float segment_tempo = 0.0f;
    uint8_t sync_beats[4] = {};  // 16 x 2-bit sync beat values packed into 4 bytes
    // Contains nested music_data after sync beats
};

// Sample header (smph)
enum class FevSamplePlaybackMode : uint8_t {
    SEQUENTIAL = 0,
    RANDOM = 1,
    RANDOM_WITHOUT_REPEAT = 2,
    SHUFFLED = 3,
};

struct FevMdSmph {
    FevSamplePlaybackMode playback_mode = FevSamplePlaybackMode::SEQUENTIAL;
    uint32_t count = 0;
};

// String list chunk (str)
struct FevMdStr {
    std::vector<uint32_t> name_end_offsets;
    // Total byte size of the string data that follows the offset table.
    // Equals the sum of all null-terminated string lengths (including null
    // terminators). Used for buffer pre-allocation by the music system
    // "smpf"/"str " chunk reader. Observed as the byte offset of the last
    // name_end_offset entry, or 0 when count is 0.
    uint32_t total_string_data_size = 0;
    std::vector<std::string> names;
    // If count==0, a single 0x00 end marker is read
};

// Sample chunk (smp)
struct FevMdSmp {
    std::string bank_name;
    uint32_t index = 0;
};

// Link data (lnkd)
struct FevMdLnkdTransitionBehavior {
    bool at_segment_end = false;
    bool on_bar = false;
    bool on_beat = false;
    uint8_t raw[4] = {};
};

struct FevMdLnkd {
    uint32_t segment_1_id = 0;
    uint32_t segment_2_id = 0;
    FevMdLnkdTransitionBehavior transition_behavior;
};

// Link from-segment data (lfsd)
// Ghidra: fmod_musicplayer.exe FUN_004bff40 (fmod_compositionentities.cpp @ 0xaaa).
struct FevMdLfsd {
    // From-segment ID — u32 hash key identifying which segment these links originate from.
    // Ghidra: fmod_musicplayer.exe @ 004bff4a (read), used as BucketHash key.
    uint32_t from_segment_id = 0;
    std::vector<uint32_t> link_ids; // count u32 link IDs referencing lnkd entries
};

// Timeline data (tlnd)
// Ghidra: fmod_musicplayer.exe FUN_004c1160 (fmod_compositionentities.cpp @ 0x678).
// The timeline_id is a u32 used as a BucketHash key for timeline lookup.
struct FevMdTlnd {
    uint32_t timeline_id = 0;
};

// Condition match set (cms)
enum class FevConditionType_Cms : uint8_t {
    ON_THEME = 0,
    ON_CUE = 1,
};

struct FevMdCms {
    FevConditionType_Cms condition_type = FevConditionType_Cms::ON_THEME;
    uint32_t theme_id = 0;
    uint32_t cue_id = 0;
};

// Condition parameter (cprm)
enum class FevConditionType_Cprm : uint16_t {
    EQUAL_TO = 0,
    GREATER_THAN = 1,
    GREATER_THAN_INCLUDING = 2,
    LESS_THAN = 3,
    LESS_THAN_INCLUDING = 4,
    BETWEEN = 5,
    BETWEEN_INCLUDING = 6,
};

struct FevMdCprm {
    FevConditionType_Cprm condition_type = FevConditionType_Cprm::EQUAL_TO;
    uint32_t param_id = 0;
    uint32_t value_1 = 0;
    uint32_t value_2 = 0;  // Padding if comparison needs only one operand
};

// A single music data chunk (type tag + parsed body)
struct FevMusicDataChunk;

struct FevMusicDataItem {
    uint32_t total_length = 0; // includes the 4-byte length field itself
    std::vector<FevMusicDataChunk> chunks;
};

struct FevMusicDataChunk {
    std::string type; // 4-char tag: "comp", "sett", "thms", etc.

    // The body, depending on type. For container types ("comp","thms","cues","scns",
    // "prms","sgms","smps","smpf","lnks","tlns") body is nested music_data.
    // For leaf types, one of the specific structs.
    // For simple u16 headers (thmh, scnh, prmh, sgmh, lnkh, lfsh, tlnh) we store a u16.
    // For prmd we store a u32.
    // For smpm we store a u32.

    // We use a variant for all known chunk types.
    // Nested music data stored as vector of items.
    std::variant<
        std::vector<FevMusicDataItem>,  // Container types (comp, thms, cues, scns, prms, sgms, smps, smpf, lnks, tlns)
        FevMdSett,                      // sett
        uint16_t,                       // thmh, scnh, prmh, sgmh, lnkh, lfsh, tlnh
        FevMdThmd,                      // thmd
        FevMdEntl,                      // entl
        FevMdScnd,                      // scnd
        uint32_t,                       // prmd, smpm
        FevMdSgmd,                      // sgmd (note: also contains nested music data in sgmd_nested_items)
        FevMdSmph,                      // smph
        FevMdStr,                       // str
        FevMdSmp,                       // smp
        FevMdLnkd,                      // lnkd
        FevMdLfsd,                      // lfsd
        FevMdTlnd,                      // tlnd
        std::monostate,                 // cond (empty)
        FevMdCms,                       // cms
        FevMdCprm                       // cprm
    > body;

    // sgmd contains nested music data after its fixed fields; stored here
    std::vector<FevMusicDataItem> sgmd_nested_items;
};

// Top-level music data: a sequence of items parsed until end-of-stream
struct FevMusicData {
    std::vector<FevMusicDataItem> items;
};

// ---------------------------------------------------------------------------
// Top-level FEV file
// ---------------------------------------------------------------------------

struct FevFile {
    uint32_t version = 0;

    // Two u32 checksums present in all FEV files (version >= 0x2e0000 for ck1,
    // >= 0x320000 for ck2). The ksy documents them as "Tied to sound definitions
    // and waveforms." They are read by fmod_event.dll EventProjectI_loadFromBuffer
    // @ 1002bdf0 and the first is stored into bank objects via SoundBank_StoreFevChecksum
    // (formerly FUN_10035780) @ 10035780.
    //
    // The computation algorithm used by FMOD Designer is not yet reversed.
    // FEV header bytes 8–15: pre-computed name-pool allocation hints.
    // Despite their prior naming as "checksums", these are NOT hashes.
    //
    // RE of fmod_event.dll EventProjectI_loadFromBuffer (Ghidra):
    //   sound_def_names_pool_size (bytes 8–11):
    //     Read at 0x1002c196 (cond: version >= 0x2E0000; LU = 0x400000 → read).
    //     Passed to SoundBank_StoreFevChecksum @ 0x10035780 as param_1.
    //     That function stores it at SoundBank+4, then calls FUN_10004270(allocator,
    //     param_1_as_size, ...) — i.e. uses the value solely as a malloc size for
    //     the sound definition name string pool.  No comparison/validation occurs.
    //
    //   waveform_names_pool_size (bytes 12–15):
    //     Seeked past entirely at 0x1002c1c6 (cond: version >= 0x320000).
    //     Completely unused at runtime for LU's FEV version.
    //
    // Values scale with content: e.g. ambience_racing.fev sd=40 wv=48,
    // ambience_spaceship.fev sd=3264 wv=4040.  They represent total byte lengths
    // of the respective name string pools, baked in at FEV write time by FMOD Designer.
    //
    // NOTE: Per-bank checksums (FevBank::fsb_checksum) ARE fully understood.
    // See fsb_extract_bank_checksums() in fsb.h — verified 102/102 banks.
    uint32_t sound_def_names_pool_size = 0;  // Allocation hint for sounddef name strings
    uint32_t waveform_names_pool_size  = 0;  // Allocation hint for waveform name strings (unused at runtime)

    std::vector<FevManifestEntry> manifest;
    std::string project_name;
    std::vector<FevBank> banks;
    FevEventCategory root_category;
    std::vector<FevEventGroup> event_groups;
    std::vector<FevSoundDefinitionConfig> sound_definition_configs;
    std::vector<FevSoundDefinition> sound_definitions;
    std::vector<FevReverbDefinition> reverb_definitions;
    FevMusicData music_data;
};
} // namespace lu::assets
