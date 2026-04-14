#include <gtest/gtest.h>
#include "fmod/fev/fev_reader.h"

#include <cstring>
#include <vector>

using namespace lu::assets;

namespace {

// Helper to build binary data in little-endian format
class FevBuilder {
public:
    void write_u8(uint8_t v) { buf_.push_back(v); }

    void write_u16(uint16_t v) {
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }

    void write_s16(int16_t v) {
        uint16_t uv;
        std::memcpy(&uv, &v, 2);
        write_u16(uv);
    }

    void write_u32(uint32_t v) {
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    }

    void write_s32(int32_t v) {
        uint32_t uv;
        std::memcpy(&uv, &v, 4);
        write_u32(uv);
    }

    void write_f32(float v) {
        uint32_t uv;
        std::memcpy(&uv, &v, 4);
        write_u32(uv);
    }

    void write_bytes(const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        buf_.insert(buf_.end(), p, p + n);
    }

    void write_zeros(size_t n) {
        buf_.insert(buf_.end(), n, 0);
    }

    // Write a u32-prefixed string (common::u4_str)
    void write_string(const std::string& s) {
        write_u32(static_cast<uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    // Write the FEV1 magic
    void write_magic() {
        buf_.push_back('F');
        buf_.push_back('E');
        buf_.push_back('V');
        buf_.push_back('1');
    }

    // Write the version
    void write_version() {
        write_u32(0x00004000);
    }

    // Write a 4-char chunk tag
    void write_tag(const char* tag) {
        buf_.insert(buf_.end(), tag, tag + 4);
    }

    std::vector<uint8_t>& data() { return buf_; }
    const std::vector<uint8_t>& data() const { return buf_; }
    size_t size() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
};

// Build a minimal but complete FEV binary:
// Header + 1 manifest entry + project name + 1 bank + root category (with 1 subcategory)
// + 1 event group (with 1 simple event) + 0 sound def configs + 0 sound defs
// + 0 reverb defs + no music data
std::vector<uint8_t> build_minimal_fev() {
    FevBuilder b;

    // --- Header ---
    b.write_magic();
    b.write_version();
    b.write_u32(0xAABBCCDD); // sound_def_names_pool_size
    b.write_u32(0x11223344); // waveform_names_pool_size

    // --- Manifest: 1 entry ---
    b.write_u32(1); // manifest_entry_count
    // Entry: BANK_COUNT = 0x01, value = 1
    b.write_u32(0x01);
    b.write_u32(1);

    // --- Project name ---
    b.write_string("TestProject");

    // --- Banks: 1 bank ---
    b.write_u32(1); // bank_count
    // Bank: load_mode(4) + max_streams(4) + fsb_checksum(8) + name
    b.write_u32(0x00000100); // load_mode = DECOMPRESS_INTO_MEMORY (FMOD_OPENMEMORY)
    b.write_s32(32);         // max_streams
    b.write_zeros(8);        // fsb_checksum
    b.write_string("TestBank");

    // --- Root event category ---
    b.write_string("master");  // name
    b.write_f32(1.0f);        // volume
    b.write_f32(0.0f);        // pitch
    b.write_s32(-1);          // max_streams
    b.write_u32(0);           // max_playback_behavior = steal_oldest
    b.write_u32(1);           // subcategory_count = 1

    // Subcategory
    b.write_string("music");   // name
    b.write_f32(0.8f);        // volume
    b.write_f32(0.5f);        // pitch
    b.write_s32(16);          // max_streams
    b.write_u32(1);           // max_playback_behavior = steal_newest
    b.write_u32(0);           // subcategory_count = 0

    // --- Event groups: 1 root group ---
    b.write_u32(1); // root_event_group_count

    // Event group
    b.write_string("TestGroup"); // name
    b.write_u32(0);              // user_property_count
    b.write_u32(0);              // subgroup_count
    b.write_u32(1);              // event_count

    // --- 1 simple event ---
    b.write_u32(16);             // is_simple_event = true (16)
    b.write_string("TestEvent"); // name
    b.write_zeros(16);           // guid

    b.write_f32(0.75f);  // volume
    b.write_f32(1.0f);   // pitch
    b.write_f32(0.1f);   // pitch_randomization
    b.write_f32(0.2f);   // volume_randomization
    b.write_u16(128);    // priority
    b.write_u16(0);      // max_instances
    b.write_u32(4);      // max_playbacks
    b.write_u32(0);      // steal_priority

    // 3D flags (4 bytes): set mode_3d
    // byte0: 0b00010000 = mode_3d at bit 4 (verified from newcontent.fev binary analysis;
    // x_3d events have 0x10 here; bit 3 = 0x08 is mode_2d)
    b.write_u8(0x10);
    b.write_zeros(3);

    b.write_f32(1.0f);   // threed_min_distance
    b.write_f32(100.0f); // threed_max_distance

    // Event flags (4 bytes)
    b.write_zeros(4);

    // Speaker levels (8 floats)
    b.write_f32(1.0f);   // speaker_l
    b.write_f32(1.0f);   // speaker_r
    b.write_f32(0.0f);   // speaker_c
    b.write_f32(0.0f);   // speaker_lfe
    b.write_f32(0.0f);   // speaker_lr
    b.write_f32(0.0f);   // speaker_rr
    b.write_f32(0.0f);   // speaker_ls
    b.write_f32(0.0f);   // speaker_rs

    // 3D cone
    b.write_f32(360.0f); // cone_inside_angle
    b.write_f32(360.0f); // cone_outside_angle
    b.write_f32(1.0f);   // cone_outside_volume

    b.write_u32(1);      // max_playbacks_behavior = steal_oldest (1-based)

    b.write_f32(1.0f);   // doppler_factor
    b.write_f32(0.0f);   // reverb_dry_level
    b.write_f32(0.0f);   // reverb_wet_level
    b.write_f32(0.0f);   // threed_speaker_spread

    b.write_u16(0);      // fade_in_time
    b.write_u16(0);      // fade_in_time_flag
    b.write_u16(0);      // fade_out_time
    b.write_u16(0);      // fade_out_time_flag

    b.write_f32(0.0f);   // spawn_intensity
    b.write_f32(0.0f);   // spawn_intensity_randomization
    b.write_f32(1.0f);   // threed_pan_level
    b.write_u32(0);      // threed_position_randomization

    // Simple event layer (no layer header, just counts + instances/envelopes)
    b.write_u16(0);      // sound_instance_count
    b.write_u16(0);      // effect_envelope_count

    // category_instance_count
    b.write_u32(0);

    // Category name
    b.write_string("master/music");

    // --- Sound definition configs: 0 ---
    b.write_u32(0);

    // --- Sound definitions: 0 ---
    b.write_u32(0);

    // --- Reverb definitions: 0 ---
    b.write_u32(0);

    // --- No music data (end of file) ---

    return b.data();
}

} // anonymous namespace

TEST(FEV, ParseMinimalFile) {
    auto data = build_minimal_fev();
    auto fev = fev_parse(data);

    // Header
    EXPECT_EQ(fev.version, 0x00004000u);
    EXPECT_EQ(fev.sound_def_names_pool_size, 0xAABBCCDDu);
    EXPECT_EQ(fev.waveform_names_pool_size, 0x11223344u);

    // Manifest
    ASSERT_EQ(fev.manifest.size(), 1u);
    EXPECT_EQ(fev.manifest[0].type, FevManifestType::BANK_COUNT);
    EXPECT_EQ(fev.manifest[0].value, 1u);

    // Project name
    EXPECT_EQ(fev.project_name, "TestProject");

    // Banks
    ASSERT_EQ(fev.banks.size(), 1u);
    EXPECT_EQ(fev.banks[0].load_mode, FevBankLoadMode::DECOMPRESS_INTO_MEMORY);
    EXPECT_EQ(fev.banks[0].max_streams, 32);
    EXPECT_EQ(fev.banks[0].name, "TestBank");

    // Root category
    EXPECT_EQ(fev.root_category.name, "master");
    EXPECT_FLOAT_EQ(fev.root_category.volume, 1.0f);
    EXPECT_FLOAT_EQ(fev.root_category.pitch, 0.0f);
    EXPECT_EQ(fev.root_category.max_streams, -1);
    EXPECT_EQ(fev.root_category.max_playback_behavior, FevMaxPlaybackBehavior::STEAL_OLDEST);

    // Subcategory
    ASSERT_EQ(fev.root_category.subcategories.size(), 1u);
    EXPECT_EQ(fev.root_category.subcategories[0].name, "music");
    EXPECT_FLOAT_EQ(fev.root_category.subcategories[0].volume, 0.8f);
    EXPECT_FLOAT_EQ(fev.root_category.subcategories[0].pitch, 0.5f);
    EXPECT_EQ(fev.root_category.subcategories[0].max_streams, 16);
    EXPECT_EQ(fev.root_category.subcategories[0].max_playback_behavior,
              FevMaxPlaybackBehavior::STEAL_NEWEST);
    EXPECT_EQ(fev.root_category.subcategories[0].subcategories.size(), 0u);

    // Event groups
    ASSERT_EQ(fev.event_groups.size(), 1u);
    EXPECT_EQ(fev.event_groups[0].name, "TestGroup");
    EXPECT_EQ(fev.event_groups[0].user_properties.size(), 0u);
    EXPECT_EQ(fev.event_groups[0].subgroups.size(), 0u);

    // Events
    ASSERT_EQ(fev.event_groups[0].events.size(), 1u);
    const auto& ev = fev.event_groups[0].events[0];
    EXPECT_EQ(ev.event_type, FevEventType::SIMPLE);
    EXPECT_EQ(ev.name, "TestEvent");
    EXPECT_FLOAT_EQ(ev.volume, 0.75f);
    EXPECT_FLOAT_EQ(ev.pitch, 1.0f);
    EXPECT_FLOAT_EQ(ev.pitch_randomization, 0.1f);
    EXPECT_FLOAT_EQ(ev.volume_randomization, 0.2f);
    EXPECT_EQ(ev.priority, 128u);
    EXPECT_EQ(ev.max_playbacks, 4u);
    EXPECT_TRUE(ev.threed_flags.mode_3d);
    EXPECT_FALSE(ev.threed_flags.mode_2d);
    EXPECT_FLOAT_EQ(ev.threed_min_distance, 1.0f);
    EXPECT_FLOAT_EQ(ev.threed_max_distance, 100.0f);
    EXPECT_FLOAT_EQ(ev.speaker_l, 1.0f);
    EXPECT_FLOAT_EQ(ev.speaker_r, 1.0f);
    EXPECT_EQ(ev.category, "master/music");

    // Simple event has exactly 1 layer
    ASSERT_EQ(ev.layers.size(), 1u);
    EXPECT_EQ(ev.layers[0].sound_instances.size(), 0u);
    EXPECT_EQ(ev.layers[0].effect_envelopes.size(), 0u);

    // Simple event has no parameters or user properties
    EXPECT_EQ(ev.parameters.size(), 0u);
    EXPECT_EQ(ev.user_properties.size(), 0u);

    // Sound definition configs / definitions / reverb defs
    EXPECT_EQ(fev.sound_definition_configs.size(), 0u);
    EXPECT_EQ(fev.sound_definitions.size(), 0u);
    EXPECT_EQ(fev.reverb_definitions.size(), 0u);

    // No music data
    EXPECT_EQ(fev.music_data.items.size(), 0u);
}

TEST(FEV, InvalidMagicThrows) {
    FevBuilder b;
    b.write_tag("BAD!");       // Wrong magic
    b.write_version();
    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(0);            // 0 manifest entries
    b.write_string("test");
    b.write_u32(0);            // 0 banks
    // Minimal root category
    b.write_string("");
    b.write_f32(0);
    b.write_f32(0);
    b.write_s32(0);
    b.write_u32(0);
    b.write_u32(0);
    // 0 event groups
    b.write_u32(0);
    // 0 configs, 0 defs, 0 reverbs
    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(0);

    EXPECT_THROW(fev_parse(b.data()), FevError);
}

TEST(FEV, EmptyDataThrows) {
    std::vector<uint8_t> empty;
    EXPECT_THROW(fev_parse(empty), FevError);
}

TEST(FEV, TooSmallDataThrows) {
    std::vector<uint8_t> small = {'F', 'E', 'V', '1', 0, 0};
    EXPECT_THROW(fev_parse(small), FevError);
}

TEST(FEV, ComplexEventWithSoundInstance) {
    // Build a minimal FEV with a complex event that has 1 layer with 1 sound instance
    FevBuilder b;

    // --- Header ---
    b.write_magic();
    b.write_version();
    b.write_u32(0);
    b.write_u32(0);

    // --- Manifest: 0 entries ---
    b.write_u32(0);

    // --- Project name ---
    b.write_string("Proj");

    // --- Banks: 0 ---
    b.write_u32(0);

    // --- Root category (minimal) ---
    b.write_string("root");
    b.write_f32(1.0f);
    b.write_f32(0.0f);
    b.write_s32(-1);
    b.write_u32(0);
    b.write_u32(0); // 0 subcategories

    // --- 1 event group ---
    b.write_u32(1);
    b.write_string("grp");
    b.write_u32(0); // 0 user properties
    b.write_u32(0); // 0 subgroups
    b.write_u32(1); // 1 event

    // --- Complex event (is_simple_event = 8 = false) ---
    b.write_u32(8); // complex
    b.write_string("ComplexEvent");
    b.write_zeros(16); // guid

    b.write_f32(0.5f);  // volume
    b.write_f32(0.0f);  // pitch
    b.write_f32(0.0f);  // pitch_randomization
    b.write_f32(0.0f);  // volume_randomization
    b.write_u16(64);    // priority
    b.write_u16(0);     // max_instances
    b.write_u32(1);     // max_playbacks
    b.write_u32(0);     // steal_priority
    b.write_zeros(4);   // 3d flags
    b.write_f32(1.0f);  // min distance
    b.write_f32(50.0f); // max distance
    b.write_zeros(4);   // event flags

    // 8 speaker levels
    for (int i = 0; i < 8; ++i) b.write_f32(0.0f);

    // Cone
    b.write_f32(360.0f);
    b.write_f32(360.0f);
    b.write_f32(1.0f);

    b.write_u32(1);     // max_playbacks_behavior
    b.write_f32(0.0f);  // doppler
    b.write_f32(0.0f);  // reverb dry
    b.write_f32(0.0f);  // reverb wet
    b.write_f32(0.0f);  // speaker spread

    b.write_u16(100);   // fade_in_time
    b.write_u16(0);     // fade_in_time_flag
    b.write_u16(200);   // fade_out_time
    b.write_u16(0);     // fade_out_time_flag

    b.write_f32(0.0f);  // spawn_intensity
    b.write_f32(0.0f);  // spawn_intensity_randomization
    b.write_f32(0.0f);  // pan_level
    b.write_u32(0);     // position_randomization

    // Complex event: layer_count = 1
    b.write_u32(1);

    // Layer (complex): layer_flags(2) + priority(s16) + control_parameter_index(s16)
    b.write_zeros(2);   // layer_flags
    b.write_s16(5);     // priority
    b.write_s16(-1);    // control_parameter_index (unset)

    // sound_instance_count = 1, effect_envelope_count = 0
    b.write_u16(1);
    b.write_u16(0);

    // Sound instance
    b.write_u16(0);      // sound_definition_index
    b.write_f32(0.0f);   // start_position
    b.write_f32(1000.0f); // length
    b.write_u32(0);      // start_mode = immediate
    b.write_u16(1);      // loop_mode = oneshot
    b.write_u16(0);      // autopitch_parameter = event_primary
    b.write_s32(-1);     // loop_count = disabled
    b.write_u32(2);      // autopitch_enabled = no
    b.write_f32(0.0f);   // autopitch_reference
    b.write_f32(0.0f);   // autopitch_at_min
    b.write_f32(1.0f);   // fine_tune
    b.write_f32(1.0f);   // volume
    b.write_f32(0.0f);   // volume_randomization
    b.write_f32(0.0f);   // pitch
    b.write_u32(0);      // fade_in_type
    b.write_u32(0);      // fade_out_type

    // Complex event: parameter_count = 0
    b.write_u32(0);

    // Complex event: user_property_count = 0
    b.write_u32(0);

    // category_instance_count
    b.write_u32(0);

    // Category
    b.write_string("root");

    // --- Sound definition configs: 0 ---
    b.write_u32(0);

    // --- Sound definitions: 0 ---
    b.write_u32(0);

    // --- Reverb definitions: 0 ---
    b.write_u32(0);

    auto fev = fev_parse(b.data());

    ASSERT_EQ(fev.event_groups.size(), 1u);
    ASSERT_EQ(fev.event_groups[0].events.size(), 1u);

    const auto& ev = fev.event_groups[0].events[0];
    EXPECT_EQ(ev.event_type, FevEventType::COMPLEX);
    EXPECT_EQ(ev.name, "ComplexEvent");
    EXPECT_FLOAT_EQ(ev.volume, 0.5f);
    EXPECT_EQ(ev.priority, 64u);
    EXPECT_EQ(ev.fade_in_time, 100u);
    EXPECT_EQ(ev.fade_out_time, 200u);

    // 1 layer
    ASSERT_EQ(ev.layers.size(), 1u);
    EXPECT_EQ(ev.layers[0].priority, 5);
    EXPECT_EQ(ev.layers[0].control_parameter_index, -1);

    // 1 sound instance in the layer
    ASSERT_EQ(ev.layers[0].sound_instances.size(), 1u);
    const auto& si = ev.layers[0].sound_instances[0];
    EXPECT_EQ(si.sound_definition_index, 0u);
    EXPECT_FLOAT_EQ(si.length, 1000.0f);
    EXPECT_EQ(si.start_mode, FevSoundStartMode::IMMEDIATE);
    EXPECT_EQ(si.loop_mode, FevSoundLoopMode::ONESHOT);
    EXPECT_EQ(si.loop_count, -1);
    EXPECT_EQ(si.autopitch_enabled, FevAutopitchEnabled::NO);
    EXPECT_FLOAT_EQ(si.fine_tune, 1.0f);

    EXPECT_EQ(ev.category, "root");
}

TEST(FEV, SoundDefinitionWithWavetable) {
    // Build a minimal FEV with 1 sound definition config and 1 sound definition
    FevBuilder b;

    b.write_magic();
    b.write_version();
    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(0); // 0 manifest

    b.write_string("Proj");
    b.write_u32(0); // 0 banks

    // Root category
    b.write_string("");
    b.write_f32(1.0f);
    b.write_f32(0.0f);
    b.write_s32(-1);
    b.write_u32(0);
    b.write_u32(0);

    b.write_u32(0); // 0 event groups

    // 1 sound definition config
    b.write_u32(1);
    b.write_u32(1);    // play_mode = random
    b.write_u32(0);    // spawn_time_min
    b.write_u32(1000); // spawn_time_max
    b.write_u32(8);    // maximum_spawned_sounds
    b.write_f32(0.9f); // volume
    b.write_u32(1);    // volume_rand_method
    b.write_f32(0.0f); // volume_random_min
    b.write_f32(0.0f); // volume_random_max
    b.write_f32(0.1f); // volume_randomization
    b.write_f32(0.5f); // pitch
    b.write_u32(1);    // pitch_rand_method
    b.write_f32(0.0f); // pitch_random_min
    b.write_f32(0.0f); // pitch_random_max
    b.write_f32(0.2f); // pitch_randomization
    b.write_u32(0);    // pitch_randomization_behavior = randomize_every_spawn
    b.write_f32(0.0f); // threed_position_randomization
    b.write_u16(0);    // trigger_delay_min
    b.write_u16(100);  // trigger_delay_max
    b.write_u16(1);    // spawn_count

    // 1 sound definition with 1 wavetable waveform
    b.write_u32(1);
    b.write_string("/sounds/test_sound");
    b.write_u32(0);  // config_index

    b.write_u32(1);  // waveform_count
    b.write_u32(0);  // waveform type = wavetable
    b.write_u32(100); // weight

    // Wavetable params
    b.write_string("sound.wav");
    b.write_string("TestBank");
    b.write_u32(0);    // percentage_locked
    b.write_u32(5000); // length_ms

    // 0 reverb definitions
    b.write_u32(0);

    auto fev = fev_parse(b.data());

    // Sound definition configs
    ASSERT_EQ(fev.sound_definition_configs.size(), 1u);
    EXPECT_EQ(fev.sound_definition_configs[0].play_mode, FevPlayMode::RANDOM);
    EXPECT_EQ(fev.sound_definition_configs[0].spawn_time_max, 1000u);
    EXPECT_EQ(fev.sound_definition_configs[0].maximum_spawned_sounds, 8u);
    EXPECT_FLOAT_EQ(fev.sound_definition_configs[0].volume, 0.9f);
    EXPECT_FLOAT_EQ(fev.sound_definition_configs[0].volume_randomization, 0.1f);
    EXPECT_FLOAT_EQ(fev.sound_definition_configs[0].pitch, 0.5f);
    EXPECT_FLOAT_EQ(fev.sound_definition_configs[0].pitch_randomization, 0.2f);
    EXPECT_EQ(fev.sound_definition_configs[0].trigger_delay_max, 100u);
    EXPECT_EQ(fev.sound_definition_configs[0].spawn_count, 1u);

    // Sound definitions
    ASSERT_EQ(fev.sound_definitions.size(), 1u);
    EXPECT_EQ(fev.sound_definitions[0].name, "/sounds/test_sound");
    EXPECT_EQ(fev.sound_definitions[0].config_index, 0u);

    ASSERT_EQ(fev.sound_definitions[0].waveforms.size(), 1u);
    EXPECT_EQ(fev.sound_definitions[0].waveforms[0].type, FevWaveformType::WAVETABLE);
    EXPECT_EQ(fev.sound_definitions[0].waveforms[0].weight, 100u);

    auto& params = std::get<FevWavetableParams>(fev.sound_definitions[0].waveforms[0].params);
    EXPECT_EQ(params.filename, "sound.wav");
    EXPECT_EQ(params.bank_name, "TestBank");
    EXPECT_EQ(params.length_ms, 5000u);
}

TEST(FEV, UserPropertyTypes) {
    // Build a minimal FEV with an event group that has user properties of all 3 types
    FevBuilder b;

    b.write_magic();
    b.write_version();
    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(0); // 0 manifest

    b.write_string("Proj");
    b.write_u32(0); // 0 banks

    // Root category
    b.write_string("");
    b.write_f32(1.0f);
    b.write_f32(0.0f);
    b.write_s32(-1);
    b.write_u32(0);
    b.write_u32(0);

    // 1 event group with 3 user properties
    b.write_u32(1);
    b.write_string("grp");
    b.write_u32(3); // user_property_count

    // Integer property
    b.write_string("int_prop");
    b.write_u32(0); // type = integer
    b.write_u32(42);

    // Float property
    b.write_string("float_prop");
    b.write_u32(1); // type = float
    b.write_f32(3.14f);

    // String property
    b.write_string("str_prop");
    b.write_u32(2); // type = string
    b.write_string("hello");

    b.write_u32(0); // 0 subgroups
    b.write_u32(0); // 0 events

    b.write_u32(0); // 0 configs
    b.write_u32(0); // 0 defs
    b.write_u32(0); // 0 reverbs

    auto fev = fev_parse(b.data());

    ASSERT_EQ(fev.event_groups.size(), 1u);
    ASSERT_EQ(fev.event_groups[0].user_properties.size(), 3u);

    const auto& ip = fev.event_groups[0].user_properties[0];
    EXPECT_EQ(ip.name, "int_prop");
    EXPECT_EQ(ip.type, FevUserPropertyType::INTEGER);
    EXPECT_EQ(std::get<uint32_t>(ip.value), 42u);

    const auto& fp = fev.event_groups[0].user_properties[1];
    EXPECT_EQ(fp.name, "float_prop");
    EXPECT_EQ(fp.type, FevUserPropertyType::FLOAT);
    EXPECT_FLOAT_EQ(std::get<float>(fp.value), 3.14f);

    const auto& sp = fev.event_groups[0].user_properties[2];
    EXPECT_EQ(sp.name, "str_prop");
    EXPECT_EQ(sp.type, FevUserPropertyType::STRING);
    EXPECT_EQ(std::get<std::string>(sp.value), "hello");
}

// Helper to write a minimal FEV prefix (header, manifest, project, banks, category, groups)
// with zero event groups, so tests can focus on sound defs / reverb / music data
void write_minimal_fev_prefix(FevBuilder& b) {
    b.write_magic();
    b.write_version();
    b.write_u32(0);              // sound_def_names_pool_size
    b.write_u32(0);              // waveform_names_pool_size
    b.write_u32(0);              // 0 manifest entries
    b.write_string("Test");      // project_name
    b.write_u32(0);              // 0 banks
    // Root category (minimal)
    b.write_string("");
    b.write_f32(1.0f);
    b.write_f32(0.0f);
    b.write_s32(-1);
    b.write_u32(0);
    b.write_u32(0);              // 0 subcategories
    b.write_u32(0);              // 0 event groups
}

TEST(FEV, EffectEnvelopeFields) {
    // Build a FEV with a complex event that has an effect envelope
    FevBuilder b;
    b.write_magic();
    b.write_version();
    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(0);              // 0 manifest
    b.write_string("Proj");
    b.write_u32(0);              // 0 banks
    // Root category
    b.write_string("root");
    b.write_f32(1.0f);
    b.write_f32(0.0f);
    b.write_s32(-1);
    b.write_u32(0);
    b.write_u32(0);
    // 1 event group with 1 complex event
    b.write_u32(1);
    b.write_string("grp");
    b.write_u32(0);              // 0 user props
    b.write_u32(0);              // 0 subgroups
    b.write_u32(1);              // 1 event

    // Complex event (type=8)
    b.write_u32(8);
    b.write_string("EnvEvent");
    b.write_zeros(16);           // guid
    b.write_f32(1.0f);           // volume
    b.write_f32(0.0f);           // pitch
    b.write_f32(0.0f);           // pitch_randomization
    b.write_f32(0.0f);           // volume_randomization
    b.write_u16(0);              // priority
    b.write_u16(0);              // max_instances
    b.write_u32(0);              // max_playbacks
    b.write_u32(0);              // steal_priority
    b.write_zeros(4);            // 3d flags
    b.write_f32(1.0f);           // min_distance
    b.write_f32(100.0f);         // max_distance
    b.write_zeros(4);            // event flags
    for (int i = 0; i < 8; ++i) b.write_f32(0.0f);  // speakers
    b.write_f32(360.0f);         // cone_inside
    b.write_f32(360.0f);         // cone_outside
    b.write_f32(1.0f);           // cone_volume
    b.write_u32(1);              // max_playbacks_behavior
    b.write_f32(0.0f);           // doppler
    b.write_f32(0.0f);           // reverb dry
    b.write_f32(0.0f);           // reverb wet
    b.write_f32(0.0f);           // speaker spread
    b.write_u16(0);              // fade in
    b.write_u16(0);
    b.write_u16(0);              // fade out
    b.write_u16(0);
    b.write_f32(0.0f);           // spawn intensity
    b.write_f32(0.0f);
    b.write_f32(0.0f);           // pan level
    b.write_u32(0);              // position rand

    // 1 layer
    b.write_u32(1);
    b.write_zeros(2);            // layer_flags
    b.write_s16(-1);             // priority
    b.write_s16(-1);             // control_parameter_index
    b.write_u16(0);              // 0 sound instances
    b.write_u16(1);              // 1 effect envelope

    // Effect envelope
    b.write_s32(2);              // control_parameter_index
    b.write_string("Volume");    // name
    b.write_s32(3);              // dsp_effect_index
    b.write_u32(0x04);           // envelope_flags
    b.write_u32(0x01);           // envelope_flags2

    // 2 envelope points
    b.write_u32(2);
    b.write_u32(0);              // point 0: position
    b.write_f32(0.0f);           // value
    b.write_u32(2);              // curve_shape = linear
    b.write_u32(1000);           // point 1: position
    b.write_f32(1.0f);           // value
    b.write_u32(4);              // curve_shape = log

    // mapping_data (4 bytes) + enabled (u32)
    uint8_t mdata[] = {0x01, 0x02, 0x03, 0x04};
    b.write_bytes(mdata, 4);
    b.write_u32(1);              // enabled

    // 0 parameters, 0 user properties
    b.write_u32(0);
    b.write_u32(0);
    // category_instance_count + category
    b.write_u32(0);
    b.write_string("root");

    // 0 sound def configs, defs, reverbs
    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(0);

    auto fev = fev_parse(b.data());
    ASSERT_EQ(fev.event_groups.size(), 1u);
    const auto& ev = fev.event_groups[0].events[0];
    ASSERT_EQ(ev.layers.size(), 1u);
    ASSERT_EQ(ev.layers[0].effect_envelopes.size(), 1u);

    const auto& env = ev.layers[0].effect_envelopes[0];
    EXPECT_EQ(env.control_parameter_index, 2);
    EXPECT_EQ(env.name, "Volume");
    EXPECT_EQ(env.dsp_effect_index, 3);
    EXPECT_EQ(env.envelope_flags, 0x04u);
    EXPECT_EQ(env.envelope_flags2, 0x01u);
    ASSERT_EQ(env.points.size(), 2u);
    EXPECT_EQ(env.points[0].position, 0u);
    EXPECT_FLOAT_EQ(env.points[0].value, 0.0f);
    EXPECT_EQ(env.points[0].curve_shape, FevCurveShape::LINEAR);
    EXPECT_EQ(env.points[1].position, 1000u);
    EXPECT_FLOAT_EQ(env.points[1].value, 1.0f);
    EXPECT_EQ(env.points[1].curve_shape, FevCurveShape::LOG);
    EXPECT_EQ(env.mapping_data[0], 0x01);
    EXPECT_EQ(env.mapping_data[3], 0x04);
    EXPECT_EQ(env.enabled, 1u);
}

TEST(FEV, SoundInstanceVolumeAndPitch) {
    // Verify the newly identified sound instance volume/pitch fields
    FevBuilder b;
    b.write_magic();
    b.write_version();
    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(0);              // 0 manifest
    b.write_string("Proj");
    b.write_u32(0);              // 0 banks
    // Root category
    b.write_string("root");
    b.write_f32(1.0f);
    b.write_f32(0.0f);
    b.write_s32(-1);
    b.write_u32(0);
    b.write_u32(0);
    // 1 event group with 1 simple event
    b.write_u32(1);
    b.write_string("grp");
    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(1);

    // Simple event (type=16)
    b.write_u32(16);
    b.write_string("SndEvt");
    b.write_zeros(16);
    b.write_f32(1.0f);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_u16(0);
    b.write_u16(0);
    b.write_u32(0);
    b.write_u32(0);
    b.write_zeros(4);
    b.write_f32(1.0f);
    b.write_f32(100.0f);
    b.write_zeros(4);
    for (int i = 0; i < 8; ++i) b.write_f32(0.0f);
    b.write_f32(360.0f);
    b.write_f32(360.0f);
    b.write_f32(1.0f);
    b.write_u32(1);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_u16(0);
    b.write_u16(0);
    b.write_u16(0);
    b.write_u16(0);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_u32(0);

    // Simple event layer: 1 sound instance, 0 envelopes
    b.write_u16(1);
    b.write_u16(0);

    // Sound instance with non-default volume/pitch
    b.write_u16(0);              // sound_definition_index
    b.write_f32(0.0f);           // start_position
    b.write_f32(500.0f);         // length
    b.write_u32(0);              // start_mode
    b.write_u16(1);              // loop_mode = oneshot
    b.write_u16(0);              // autopitch_parameter
    b.write_s32(-1);             // loop_count
    b.write_u32(2);              // autopitch_enabled = no
    b.write_f32(0.0f);           // autopitch_reference
    b.write_f32(0.0f);           // autopitch_at_min
    b.write_f32(0.0f);           // fine_tune
    b.write_f32(0.75f);          // volume
    b.write_f32(0.1f);           // volume_randomization
    b.write_f32(-0.5f);          // pitch
    b.write_u32(1);              // fade_in_type
    b.write_u32(2);              // fade_out_type

    b.write_u32(0);              // category_instance_count
    b.write_string("root");

    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(0);

    auto fev = fev_parse(b.data());
    const auto& si = fev.event_groups[0].events[0].layers[0].sound_instances[0];
    EXPECT_FLOAT_EQ(si.volume, 0.75f);
    EXPECT_FLOAT_EQ(si.volume_randomization, 0.1f);
    EXPECT_FLOAT_EQ(si.pitch, -0.5f);
    EXPECT_EQ(si.fade_in_type, 1u);
    EXPECT_EQ(si.fade_out_type, 2u);
}

TEST(FEV, SoundDefinitionConfigRandFields) {
    // Verify the newly identified volume/pitch randomization method and range fields
    FevBuilder b;
    write_minimal_fev_prefix(b);

    // 1 sound definition config
    b.write_u32(1);
    b.write_u32(0);              // play_mode = sequential
    b.write_u32(0);              // spawn_time_min
    b.write_u32(0);              // spawn_time_max
    b.write_u32(1);              // maximum_spawned_sounds
    b.write_f32(0.8f);           // volume
    b.write_u32(2);              // volume_rand_method
    b.write_f32(-3.0f);          // volume_random_min
    b.write_f32(3.0f);           // volume_random_max
    b.write_f32(0.5f);           // volume_randomization
    b.write_f32(1.0f);           // pitch
    b.write_u32(1);              // pitch_rand_method
    b.write_f32(-1.0f);          // pitch_random_min
    b.write_f32(1.0f);           // pitch_random_max
    b.write_f32(0.3f);           // pitch_randomization
    b.write_u32(2);              // pitch_randomization_behavior
    b.write_f32(5.0f);           // threed_position_randomization
    b.write_u16(10);             // trigger_delay_min
    b.write_u16(50);             // trigger_delay_max
    b.write_u16(3);              // spawn_count

    b.write_u32(0);              // 0 sound defs
    b.write_u32(0);              // 0 reverbs

    auto fev = fev_parse(b.data());
    ASSERT_EQ(fev.sound_definition_configs.size(), 1u);
    const auto& cfg = fev.sound_definition_configs[0];
    EXPECT_FLOAT_EQ(cfg.volume, 0.8f);
    EXPECT_EQ(cfg.volume_rand_method, 2u);
    EXPECT_FLOAT_EQ(cfg.volume_random_min, -3.0f);
    EXPECT_FLOAT_EQ(cfg.volume_random_max, 3.0f);
    EXPECT_FLOAT_EQ(cfg.volume_randomization, 0.5f);
    EXPECT_FLOAT_EQ(cfg.pitch, 1.0f);
    EXPECT_EQ(cfg.pitch_rand_method, 1u);
    EXPECT_FLOAT_EQ(cfg.pitch_random_min, -1.0f);
    EXPECT_FLOAT_EQ(cfg.pitch_random_max, 1.0f);
    EXPECT_FLOAT_EQ(cfg.pitch_randomization, 0.3f);
    EXPECT_EQ(cfg.pitch_randomization_behavior,
              FevPitchRandomizationBehavior::RANDOMIZE_WHEN_EVENT_STARTS);
    EXPECT_FLOAT_EQ(cfg.threed_position_randomization, 5.0f);
    EXPECT_EQ(cfg.trigger_delay_min, 10u);
    EXPECT_EQ(cfg.trigger_delay_max, 50u);
    EXPECT_EQ(cfg.spawn_count, 3u);
}

TEST(FEV, OscillatorWaveform) {
    FevBuilder b;
    write_minimal_fev_prefix(b);

    b.write_u32(0);              // 0 sound def configs

    // 1 sound definition with 1 oscillator waveform
    b.write_u32(1);
    b.write_string("/test/osc");
    b.write_u32(0);              // config_index
    b.write_u32(1);              // waveform_count
    b.write_u32(1);              // type = oscillator
    b.write_u32(50);             // weight
    b.write_u32(4);              // oscillator type = triangle
    b.write_f32(440.0f);         // frequency

    b.write_u32(0);              // 0 reverbs

    auto fev = fev_parse(b.data());
    ASSERT_EQ(fev.sound_definitions.size(), 1u);
    ASSERT_EQ(fev.sound_definitions[0].waveforms.size(), 1u);
    const auto& wf = fev.sound_definitions[0].waveforms[0];
    EXPECT_EQ(wf.type, FevWaveformType::OSCILLATOR);
    EXPECT_EQ(wf.weight, 50u);
    auto& osc = std::get<FevOscillatorParams>(wf.params);
    EXPECT_EQ(osc.type, FevOscillatorType::TRIANGLE);
    EXPECT_FLOAT_EQ(osc.frequency, 440.0f);
}

TEST(FEV, ReverbDefinitionFields) {
    FevBuilder b;
    write_minimal_fev_prefix(b);

    b.write_u32(0);              // 0 sound def configs
    b.write_u32(0);              // 0 sound defs

    // 1 reverb definition
    b.write_u32(1);
    b.write_string("TestReverb");
    b.write_s32(-1000);          // room
    b.write_s32(-500);           // room_hf
    b.write_f32(50.0f);          // room_rolloff_factor
    b.write_f32(0.83f);          // decay_time
    b.write_f32(1.0f);           // decay_hf_ratio
    b.write_s32(-2602);          // reflections
    b.write_f32(0.007f);         // reflections_delay
    b.write_s32(200);            // reverb (late)
    b.write_f32(0.011f);         // reverb_delay
    b.write_f32(100.0f);         // diffusion
    b.write_f32(100.0f);         // density
    b.write_f32(5000.0f);        // hf_reference
    b.write_f32(0.0f);           // room_lf
    b.write_f32(250.0f);         // lf_reference
    b.write_s32(0);              // instance
    b.write_s32(-1);             // environment
    b.write_f32(7.5f);           // env_size
    b.write_f32(1.0f);           // env_diffusion
    b.write_s32(0);              // room_lf_b
    // 48-byte extended block: reflections_pan[3], reverb_pan[3],
    // echo_time, echo_depth, modulation_time, modulation_depth,
    // air_absorption_hf, lf_reference_ext
    b.write_f32(1.0f); b.write_f32(0.0f); b.write_f32(0.0f); // reflections_pan
    b.write_f32(0.0f); b.write_f32(1.0f); b.write_f32(0.0f); // reverb_pan
    b.write_f32(0.25f);          // echo_time
    b.write_f32(0.5f);           // echo_depth
    b.write_f32(0.1f);           // modulation_time
    b.write_f32(0.2f);           // modulation_depth
    b.write_f32(-3.0f);          // air_absorption_hf
    b.write_f32(0.0f);           // lf_reference_ext
    b.write_f32(250.0f);         // lf_reference_b
    b.write_u32(0x33F);          // flags

    auto fev = fev_parse(b.data());
    ASSERT_EQ(fev.reverb_definitions.size(), 1u);
    const auto& rv = fev.reverb_definitions[0];
    EXPECT_EQ(rv.name, "TestReverb");
    EXPECT_EQ(rv.room, -1000);
    EXPECT_EQ(rv.room_hf, -500);
    EXPECT_FLOAT_EQ(rv.room_rolloff_factor, 50.0f);
    EXPECT_FLOAT_EQ(rv.decay_time, 0.83f);
    EXPECT_FLOAT_EQ(rv.decay_hf_ratio, 1.0f);
    EXPECT_EQ(rv.reflections, -2602);
    EXPECT_FLOAT_EQ(rv.reflections_delay, 0.007f);
    EXPECT_EQ(rv.reverb, 200);
    EXPECT_FLOAT_EQ(rv.reverb_delay, 0.011f);
    EXPECT_FLOAT_EQ(rv.diffusion, 100.0f);
    EXPECT_FLOAT_EQ(rv.density, 100.0f);
    EXPECT_FLOAT_EQ(rv.hf_reference, 5000.0f);
    EXPECT_FLOAT_EQ(rv.room_lf, 0.0f);
    EXPECT_FLOAT_EQ(rv.lf_reference, 250.0f);
    EXPECT_EQ(rv.instance, 0);
    EXPECT_EQ(rv.environment, -1);
    EXPECT_FLOAT_EQ(rv.env_size, 7.5f);
    EXPECT_FLOAT_EQ(rv.env_diffusion, 1.0f);
    EXPECT_EQ(rv.room_lf_b, 0);
    EXPECT_FLOAT_EQ(rv.reflections_pan[0], 1.0f);
    EXPECT_FLOAT_EQ(rv.reflections_pan[1], 0.0f);
    EXPECT_FLOAT_EQ(rv.reverb_pan[1], 1.0f);
    EXPECT_FLOAT_EQ(rv.echo_time, 0.25f);
    EXPECT_FLOAT_EQ(rv.echo_depth, 0.5f);
    EXPECT_FLOAT_EQ(rv.modulation_time, 0.1f);
    EXPECT_FLOAT_EQ(rv.modulation_depth, 0.2f);
    EXPECT_FLOAT_EQ(rv.air_absorption_hf, -3.0f);
    EXPECT_FLOAT_EQ(rv.lf_reference_b, 250.0f);
    EXPECT_EQ(rv.flags, 0x33Fu);
}

TEST(FEV, EventParameterWithExtraData) {
    // Test complex event with a parameter that has a variable-length tail
    FevBuilder b;
    b.write_magic();
    b.write_version();
    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(0);
    b.write_string("Proj");
    b.write_u32(0);
    b.write_string("root");
    b.write_f32(1.0f);
    b.write_f32(0.0f);
    b.write_s32(-1);
    b.write_u32(0);
    b.write_u32(0);
    // 1 event group with 1 complex event
    b.write_u32(1);
    b.write_string("grp");
    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(1);

    // Complex event
    b.write_u32(8);
    b.write_string("ParamEvt");
    b.write_zeros(16);
    b.write_f32(1.0f);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_u16(0);
    b.write_u16(0);
    b.write_u32(0);
    b.write_u32(0);
    b.write_zeros(4);
    b.write_f32(1.0f);
    b.write_f32(100.0f);
    b.write_zeros(4);
    for (int i = 0; i < 8; ++i) b.write_f32(0.0f);
    b.write_f32(360.0f);
    b.write_f32(360.0f);
    b.write_f32(1.0f);
    b.write_u32(1);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_u16(0);
    b.write_u16(0);
    b.write_u16(0);
    b.write_u16(0);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_f32(0.0f);
    b.write_u32(0);

    // 1 layer with no sounds/envelopes
    b.write_u32(1);
    b.write_zeros(2);
    b.write_s16(-1);
    b.write_s16(-1);
    b.write_u16(0);
    b.write_u16(0);

    // 1 parameter
    b.write_u32(1);
    b.write_string("distance");
    b.write_f32(1.0f);           // velocity
    b.write_f32(0.0f);           // minimum_value
    b.write_f32(100.0f);         // maximum_value
    // Flags: primary bit set (byte0 = 0x01)
    b.write_u8(0x01);
    b.write_zeros(3);
    b.write_f32(10.0f);          // seek_speed
    b.write_u32(0);              // unknown_value
    b.write_u32(2);              // extra count
    b.write_u32(42);             // extra[0]
    b.write_u32(99);             // extra[1]

    // 0 user properties
    b.write_u32(0);
    b.write_u32(0);              // category_instance_count
    b.write_string("root");

    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(0);

    auto fev = fev_parse(b.data());
    const auto& ev = fev.event_groups[0].events[0];
    ASSERT_EQ(ev.parameters.size(), 1u);
    const auto& param = ev.parameters[0];
    EXPECT_EQ(param.name, "distance");
    EXPECT_FLOAT_EQ(param.velocity, 1.0f);
    EXPECT_FLOAT_EQ(param.minimum_value, 0.0f);
    EXPECT_FLOAT_EQ(param.maximum_value, 100.0f);
    EXPECT_TRUE(param.flags.primary);
    EXPECT_FLOAT_EQ(param.seek_speed, 10.0f);
    EXPECT_EQ(param.unknown_value, 0u);
    ASSERT_EQ(param.unknown_extra.size(), 2u);
    EXPECT_EQ(param.unknown_extra[0], 42u);
    EXPECT_EQ(param.unknown_extra[1], 99u);
}

TEST(FEV, BankLoadModes) {
    // Test different bank load modes
    FevBuilder b;
    b.write_magic();
    b.write_version();
    b.write_u32(0);
    b.write_u32(0);
    b.write_u32(0);              // 0 manifest
    b.write_string("Proj");

    // 3 banks with different load modes
    b.write_u32(3);
    // Bank 1: stream from disk
    b.write_u32(0x00000080);     // FMOD_CREATESTREAM
    b.write_s32(-1);
    b.write_zeros(8);
    b.write_string("Streaming");
    // Bank 2: decompress into memory
    b.write_u32(0x00000100);     // FMOD_OPENMEMORY
    b.write_s32(16);
    b.write_zeros(8);
    b.write_string("Decompressed");
    // Bank 3: load into memory
    b.write_u32(0x00000200);     // FMOD_CREATECOMPRESSEDSAMPLE
    b.write_s32(32);
    b.write_zeros(8);
    b.write_string("Compressed");

    // Root category
    b.write_string("");
    b.write_f32(1.0f);
    b.write_f32(0.0f);
    b.write_s32(-1);
    b.write_u32(0);
    b.write_u32(0);

    b.write_u32(0);              // 0 groups
    b.write_u32(0);              // 0 configs
    b.write_u32(0);              // 0 defs
    b.write_u32(0);              // 0 reverbs

    auto fev = fev_parse(b.data());
    ASSERT_EQ(fev.banks.size(), 3u);
    EXPECT_EQ(fev.banks[0].load_mode, FevBankLoadMode::STREAM_FROM_DISK);
    EXPECT_EQ(fev.banks[0].name, "Streaming");
    EXPECT_EQ(fev.banks[1].load_mode, FevBankLoadMode::DECOMPRESS_INTO_MEMORY);
    EXPECT_EQ(fev.banks[1].name, "Decompressed");
    EXPECT_EQ(fev.banks[2].load_mode, FevBankLoadMode::LOAD_INTO_MEMORY);
    EXPECT_EQ(fev.banks[2].name, "Compressed");
}

TEST(FEV, MusicDataSettChunk) {
    // Test music data parsing with a sett (settings) chunk
    FevBuilder b;
    write_minimal_fev_prefix(b);

    b.write_u32(0);              // 0 sound def configs
    b.write_u32(0);              // 0 sound defs
    b.write_u32(0);              // 0 reverbs

    // Music data: 1 item containing a "comp" container with a "sett" chunk
    // Item: total_length includes the 4-byte length field itself
    // Inner: "comp" tag + nested item with "sett" tag + 2 floats
    // sett payload = 4 (tag) + 8 (2 floats) = 12 bytes
    // sett item = 4 (length) + 12 = 16 bytes, total_length = 20
    // comp wrapper = 4 (tag "comp") + sett item (16 bytes) = 20
    // outer item total = 4 (length) + 20 = 24

    uint32_t sett_item_len = 4 + 4 + 4 + 4;  // length(4) + tag(4) + vol(4) + rev(4) = 16
    uint32_t outer_len = 4 + 4 + sett_item_len; // length(4) + "comp"(4) + sett_item

    b.write_u32(outer_len);      // outer item total_length
    b.write_tag("comp");         // container tag
    // Nested sett item
    b.write_u32(sett_item_len);  // sett item total_length
    b.write_tag("sett");
    b.write_f32(0.8f);           // volume
    b.write_f32(0.3f);           // reverb

    auto fev = fev_parse(b.data());
    ASSERT_EQ(fev.music_data.items.size(), 1u);
    ASSERT_EQ(fev.music_data.items[0].chunks.size(), 1u);
    EXPECT_EQ(fev.music_data.items[0].chunks[0].type, "comp");

    // The comp chunk's body should contain nested items
    auto& nested = std::get<std::vector<FevMusicDataItem>>(fev.music_data.items[0].chunks[0].body);
    ASSERT_EQ(nested.size(), 1u);
    ASSERT_EQ(nested[0].chunks.size(), 1u);
    EXPECT_EQ(nested[0].chunks[0].type, "sett");
    auto& sett = std::get<FevMdSett>(nested[0].chunks[0].body);
    EXPECT_FLOAT_EQ(sett.volume, 0.8f);
    EXPECT_FLOAT_EQ(sett.reverb, 0.3f);
}
