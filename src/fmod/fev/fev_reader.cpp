#include "fmod/fev/fev_reader.h"
#include "fmod/fsb/fsb_reader.h"
#include "common/binary_reader/binary_reader.h"

#include <cstring>

namespace lu::assets {

namespace {

// ---------------------------------------------------------------------------
// RIFF FEV parsing context
// ---------------------------------------------------------------------------
// The RIFF-based FEV format (version 0x00450000, FMOD Designer 4.45) wraps the
// same binary structures as FEV1 inside RIFF chunks, with these differences:
//   - Event group, event, and parameter names use u32 indices into a STRR table
//   - Effect envelopes omit the inline name string and store position-only points
//   - Banks have per-language checksum arrays: lang_count × (ck0, ck1, unk)
//   - Category names and sounddef filenames remain as inline u32-prefixed strings

struct FevParseContext {
    const std::vector<std::string>* strr = nullptr; // non-null for RIFF FEV
    uint32_t lang_count = 1;

    bool is_riff() const { return strr != nullptr; }

    // Read a name field: STRR index lookup for RIFF, inline string for FEV1.
    std::string read_indexed_name(BinaryReader& r) const;
};

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

// Read a u32-length-prefixed string (common::u4_str in ksy).
// FEV strings include the null terminator in their length, so strip it.
std::string read_fev_string(BinaryReader& r) {
    uint32_t len = r.read_u32();
    if (len == 0) return {};
    auto bytes = r.read_bytes(len);
    // Strip trailing null(s) — FEV length includes the null terminator
    size_t str_len = len;
    while (str_len > 0 && bytes[str_len - 1] == 0) --str_len;
    return std::string(reinterpret_cast<const char*>(bytes.data()), str_len);
}

// Read a null-terminated ASCII string from the current position
std::string read_null_terminated_string(BinaryReader& r) {
    std::string result;
    while (!r.eof()) {
        uint8_t ch = r.read_u8();
        if (ch == 0) break;
        result += static_cast<char>(ch);
    }
    return result;
}

std::string FevParseContext::read_indexed_name(BinaryReader& r) const {
    if (strr) {
        uint32_t idx = r.read_u32();
        return idx < strr->size() ? (*strr)[idx] : "";
    }
    return read_fev_string(r);
}

// ---------------------------------------------------------------------------
// User properties
// ---------------------------------------------------------------------------

FevUserProperty read_user_property(BinaryReader& r) {
    FevUserProperty prop;
    prop.name = read_fev_string(r);
    prop.type = static_cast<FevUserPropertyType>(r.read_u32());
    switch (prop.type) {
        case FevUserPropertyType::INTEGER:
            prop.value = r.read_u32();
            break;
        case FevUserPropertyType::FLOAT: {
            float fval = r.read_f32();
            prop.value = fval;
            break;
        }
        case FevUserPropertyType::STRING:
            prop.value = read_fev_string(r);
            break;
        default:
            // Treat unknown types as integer
            prop.value = r.read_u32();
            break;
    }
    return prop;
}

// ---------------------------------------------------------------------------
// Event categories (recursive)
// ---------------------------------------------------------------------------

FevEventCategory read_event_category(BinaryReader& r) {
    FevEventCategory cat;
    cat.name = read_fev_string(r);
    cat.volume = r.read_f32();
    cat.pitch = r.read_f32();
    cat.max_streams = r.read_s32();
    cat.max_playback_behavior = static_cast<FevMaxPlaybackBehavior>(r.read_u32());

    uint32_t num_sub = r.read_u32();
    cat.subcategories.reserve(num_sub);
    for (uint32_t i = 0; i < num_sub; ++i) {
        cat.subcategories.push_back(read_event_category(r));
    }

    return cat;
}

// ---------------------------------------------------------------------------
// Effect envelope points
// ---------------------------------------------------------------------------

FevEffectEnvelopePoint read_effect_envelope_point(BinaryReader& r) {
    FevEffectEnvelopePoint pt;
    pt.position = r.read_u32();
    pt.value = r.read_f32();
    pt.curve_shape = static_cast<FevCurveShape>(r.read_u32());
    return pt;
}

// ---------------------------------------------------------------------------
// Effect envelopes
// ---------------------------------------------------------------------------

FevEffectEnvelope read_effect_envelope(BinaryReader& r, const FevParseContext& ctx) {
    FevEffectEnvelope env;
    env.control_parameter_index = r.read_s32();

    if (ctx.is_riff()) {
        // RIFF FEV: no inline name string, position-only points, no mapping_data.
        // Format: ctrl_param(s32), dsp_idx(s32), flags(u32), point_count(u32),
        //         positions[count](u32 each), enabled(u32).
        // Note: FEV1 has flags2 and mapping_data fields; RIFF FEV omits both.
        env.dsp_effect_index = r.read_s32();
        env.envelope_flags = r.read_u32();

        uint32_t point_count = r.read_u32();
        env.points.reserve(point_count);
        for (uint32_t i = 0; i < point_count; ++i) {
            FevEffectEnvelopePoint pt;
            pt.position = r.read_u32(); // position only — no value/curve data
            pt.value = 0.0f;
            pt.curve_shape = FevCurveShape::LINEAR;
            env.points.push_back(pt);
        }
        env.enabled = r.read_u32();
    } else {
        // FEV1: inline name string, full point data
        env.name = read_fev_string(r);
        env.dsp_effect_index = r.read_s32();
        env.envelope_flags = r.read_u32();
        env.envelope_flags2 = r.read_u32();

        uint32_t point_count = r.read_u32();
        env.points.reserve(point_count);
        for (uint32_t i = 0; i < point_count; ++i) {
            env.points.push_back(read_effect_envelope_point(r));
        }

        auto md = r.read_bytes(4);
        std::memcpy(env.mapping_data, md.data(), 4);
        env.enabled = r.read_u32();
    }
    return env;
}

// ---------------------------------------------------------------------------
// Sound instances
// ---------------------------------------------------------------------------

FevSoundInstance read_sound_instance(BinaryReader& r) {
    FevSoundInstance si;
    si.sound_definition_index = r.read_u16();
    si.start_position = r.read_f32();
    si.length = r.read_f32();
    si.start_mode = static_cast<FevSoundStartMode>(r.read_u32());
    si.loop_mode = static_cast<FevSoundLoopMode>(r.read_u16());
    si.autopitch_parameter = static_cast<FevAutopitchParameter>(r.read_u16());
    si.loop_count = r.read_s32();
    si.autopitch_enabled = static_cast<FevAutopitchEnabled>(r.read_u32());
    si.autopitch_reference = r.read_f32();
    si.autopitch_at_min = r.read_f32();
    si.fine_tune = r.read_f32();
    si.volume = r.read_f32();
    si.volume_randomization = r.read_f32();
    si.pitch = r.read_f32();
    si.fade_in_type = r.read_u32();
    si.fade_out_type = r.read_u32();
    return si;
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

FevLayer read_layer(BinaryReader& r, bool is_simple_event, const FevParseContext& ctx) {
    FevLayer layer;
    if (!is_simple_event) {
        auto lf = r.read_bytes(2);
        std::memcpy(layer.layer_flags, lf.data(), 2);
        layer.priority = r.read_s16();
        layer.control_parameter_index = r.read_s16();
    }

    uint16_t sound_instance_count = r.read_u16();
    uint16_t effect_envelope_count = r.read_u16();

    layer.sound_instances.reserve(sound_instance_count);
    for (uint16_t i = 0; i < sound_instance_count; ++i) {
        layer.sound_instances.push_back(read_sound_instance(r));
    }

    layer.effect_envelopes.reserve(effect_envelope_count);
    for (uint16_t i = 0; i < effect_envelope_count; ++i) {
        layer.effect_envelopes.push_back(read_effect_envelope(r, ctx));
    }

    return layer;
}

// ---------------------------------------------------------------------------
// Event parameter flags
// ---------------------------------------------------------------------------

FevEventParameterFlags read_event_parameter_flags(BinaryReader& r) {
    FevEventParameterFlags flags;
    auto raw = r.read_bytes(4);
    std::memcpy(flags.raw, raw.data(), 4);

    // Byte 0 bit layout (confirmed via Ghidra RE of fmod_designercl.exe
    // FUN_00414752 and fmod_event.dll FUN_1001e700):
    //   bit 0 (0x01): primary — event's primary parameter
    //   bit 1 (0x02): loop — parameter loops back at max (loopmode=0)
    //   bit 2 (0x04): oneshot_and_stop_event — stop event at max (loopmode=1)
    //   bit 3 (0x08): oneshot — stop at max (loopmode=2)
    //   bit 4 (0x10): auto_distance — runtime-only, set if name is "(distance)"
    //   bit 5 (0x20): auto_listener_angle — runtime-only, set if "(listener angle)"
    //   bit 6 (0x40): keyoff_on_silence — send key-off when event goes silent
    //   bit 7 (0x80): reserved (always 0 in FEV files)
    // Bytes 1-3: reserved (always 0)
    uint8_t byte0 = flags.raw[0];
    flags.primary = byte0 & 1;
    flags.loop = (byte0 >> 1) & 1;
    flags.oneshot_and_stop_event = (byte0 >> 2) & 1;
    flags.oneshot = (byte0 >> 3) & 1;
    flags.keyoff_on_silence = (byte0 >> 6) & 1;
    return flags;
}

// ---------------------------------------------------------------------------
// Event parameters
// ---------------------------------------------------------------------------

FevEventParameter read_event_parameter(BinaryReader& r, const FevParseContext& ctx) {
    FevEventParameter param;
    param.name = ctx.read_indexed_name(r);
    param.velocity = r.read_f32();
    param.minimum_value = r.read_f32();
    param.maximum_value = r.read_f32();
    param.flags = read_event_parameter_flags(r);
    param.seek_speed = r.read_f32();
    // Variable-length tail: a u32 value, a u32 count, then count * u32 extra values.
    // The ksy documents this as fixed 8-byte padding (always 0), but real client files
    // contain nonzero counts with additional data.
    param.unknown_value = r.read_u32();
    uint32_t extra_count = r.read_u32();
    param.unknown_extra.reserve(extra_count);
    for (uint32_t i = 0; i < extra_count; ++i) {
        param.unknown_extra.push_back(r.read_u32());
    }
    return param;
}

// ---------------------------------------------------------------------------
// 3D flags
// ---------------------------------------------------------------------------

FevEvent3DFlags read_3d_flags(BinaryReader& r) {
    FevEvent3DFlags flags;
    auto raw = r.read_bytes(4);
    std::memcpy(flags.raw, raw.data(), 4);

    // FMOD_MODE-derived bitfield. Confirmed via Ghidra RE of fmod_event.dll
    // EventI::getPropertyByIndex @ 10017550 / setPropertyByIndex @ 10017a40.
    //
    // Byte 0:
    //   bits 7-5: FMOD_CREATESTREAM(0x80), FMOD_SOFTWARE(0x40), FMOD_HARDWARE(0x20)
    //   bit 4 (0x10): mode_3d — FMOD_3D
    //   bit 3 (0x08): mode_2d — FMOD_2D
    //   bits 2-0: FMOD_LOOP_BIDI(0x04), FMOD_LOOP_NORMAL(0x02), FMOD_LOOP_OFF(0x01)
    uint8_t b0 = flags.raw[0];
    flags.mode_3d = (b0 >> 4) & 1;
    flags.mode_2d = (b0 >> 3) & 1;

    // Byte 1: creation/open flags (typically 0 for events)
    // Byte 2:
    //   bits 7-6: reserved / higher rolloff bits (FMOD_3D_LINEARROLLOFF, etc.)
    //   bit 5 (0x20 in byte = bit 21 of u32): rolloff_linear — FMOD_3D_LINEARSQUAREROLLOFF
    //   bit 4 (0x10 in byte = bit 20 of u32): rolloff_logarithmic — FMOD_3D_INVERSEROLLOFF
    //   bit 3 (0x08 in byte = bit 19 of u32): position_world_relative — FMOD_3D_WORLDRELATIVE
    //   bit 2 (0x04 in byte = bit 18 of u32): position_head_relative — FMOD_3D_HEADRELATIVE
    //   bit 1 (0x02 in byte = bit 17 of u32): unique — FMOD_UNIQUE
    //   bit 0 (0x01 in byte = bit 16 of u32): ignore_geometry (FEV-specific packing)
    uint8_t b2 = flags.raw[2];
    flags.rolloff_linear = (b2 >> 5) & 1;
    flags.rolloff_logarithmic = (b2 >> 4) & 1;
    flags.position_world_relative = (b2 >> 3) & 1;
    flags.position_head_relative = (b2 >> 2) & 1;
    flags.unique = (b2 >> 1) & 1;
    flags.ignore_geometry = b2 & 1;

    // Byte 3:
    //   bit 6 (0x40 = bit 30 of u32): FMOD_3D_IGNOREGEOMETRY in FMOD_MODE
    //     (canonical position; the runtime getPropertyByIndex reads bit 30)
    //   bit 2 (0x04 = bit 26 of u32): FMOD_3D_CUSTOMROLLOFF
    //   Binary analysis confirmed events with ignoregeometry=Yes have b3=0x40.
    //   Note: ignore_geometry may be stored redundantly at both bit 16 (byte 2)
    //   and bit 30 (byte 3) depending on FMOD version / FEV writer behavior.
    return flags;
}

// ---------------------------------------------------------------------------
// Event flags
// ---------------------------------------------------------------------------

FevEventFlags read_event_flags(BinaryReader& r) {
    FevEventFlags flags;
    auto raw = r.read_bytes(4);
    std::memcpy(flags.raw, raw.data(), 4);

    // Event behavior flags at runtime offset +0x60. Confirmed via Ghidra RE of
    // fmod_event.dll getPropertyByIndex @ 10017550 / setPropertyByIndex @ 10017a40,
    // and fmod_designercl.exe FUN_00426509 (oneshot flag writer).
    //
    // Byte 0: reserved (always 0)
    // Byte 1: rolloff type flags (mutually exclusive):
    //   bit 0 (0x01 in byte = bit 8 of u32, 0x0100): inverse rolloff
    //   bit 1 (0x02 in byte = bit 9, 0x0200): linear squared rolloff
    //   bit 2 (0x04 in byte = bit 10, 0x0400): linear rolloff
    //   bit 3 (0x08 in byte = bit 11, 0x0800): logarithmic rolloff
    flags.rolloff_type = flags.raw[1] & 0x0F;  // Lower nibble of byte 1

    // Byte 2:
    //   bit 7 (0x80 in byte = bit 23, 0x800000): custom rolloff curve
    //   bit 3 (0x08 in byte = bit 19, 0x080000): oneshot flag
    uint8_t b2 = flags.raw[2];
    if (b2 & 0x80) flags.rolloff_type |= 0x10;  // Mark custom rolloff in high bit
    flags.oneshot = (b2 >> 3) & 1;

    // Byte 3: reserved (always 0)
    return flags;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

FevEvent read_event(BinaryReader& r, const FevParseContext& ctx) {
    FevEvent ev;

    // is_simple_event: 8 = complex (false), 16 = simple (true)
    ev.event_type = static_cast<FevEventType>(r.read_u32());
    bool is_simple = (ev.event_type == FevEventType::SIMPLE);

    ev.name = ctx.read_indexed_name(r);

    auto guid = r.read_bytes(16);
    std::memcpy(ev.guid, guid.data(), 16);

    ev.volume = r.read_f32();
    ev.pitch = r.read_f32();
    ev.pitch_randomization = r.read_f32();
    ev.volume_randomization = r.read_f32();
    ev.priority = r.read_u16();
    ev.max_instances = r.read_u16();
    ev.max_playbacks = r.read_u32();
    ev.steal_priority = r.read_u32();

    ev.threed_flags = read_3d_flags(r);
    ev.threed_min_distance = r.read_f32();
    ev.threed_max_distance = r.read_f32();
    ev.event_flags = read_event_flags(r);

    ev.speaker_l = r.read_f32();
    ev.speaker_r = r.read_f32();
    ev.speaker_c = r.read_f32();
    ev.speaker_lfe = r.read_f32();
    ev.speaker_lr = r.read_f32();
    ev.speaker_rr = r.read_f32();
    ev.speaker_ls = r.read_f32();
    ev.speaker_rs = r.read_f32();

    ev.threed_cone_inside_angle = r.read_f32();
    ev.threed_cone_outside_angle = r.read_f32();
    ev.threed_cone_outside_volume = r.read_f32();

    ev.max_playbacks_behavior = static_cast<FevEventMaxPlaybackBehavior>(r.read_u32());

    ev.threed_doppler_factor = r.read_f32();
    ev.reverb_dry_level = r.read_f32();
    ev.reverb_wet_level = r.read_f32();
    ev.threed_speaker_spread = r.read_f32();

    ev.fade_in_time = r.read_u16();
    ev.fade_in_time_flag = r.read_u16();
    ev.fade_out_time = r.read_u16();
    ev.fade_out_time_flag = r.read_u16();

    ev.spawn_intensity = r.read_f32();
    ev.spawn_intensity_randomization = r.read_f32();
    ev.threed_pan_level = r.read_f32();
    ev.threed_position_randomization = r.read_u32();

    if (!is_simple) {
        // Complex event: layer_count + layers
        uint32_t layer_count = r.read_u32();
        ev.layers.reserve(layer_count);
        for (uint32_t i = 0; i < layer_count; ++i) {
            ev.layers.push_back(read_layer(r, false, ctx));
        }
    } else {
        // Simple event: single layer (no layer header fields)
        ev.layers.push_back(read_layer(r, true, ctx));
    }

    if (!is_simple) {
        // Parameters
        uint32_t param_count = r.read_u32();
        ev.parameters.reserve(param_count);
        for (uint32_t i = 0; i < param_count; ++i) {
            ev.parameters.push_back(read_event_parameter(r, ctx));
        }

        // User properties
        uint32_t up_count = r.read_u32();
        ev.user_properties.reserve(up_count);
        for (uint32_t i = 0; i < up_count; ++i) {
            ev.user_properties.push_back(read_user_property(r));
        }
    }

    // Event extra flags (4 bytes) - present for both simple and complex
    ev.category_instance_count = r.read_u32();

    // Category name
    ev.category = read_fev_string(r);

    return ev;
}

// ---------------------------------------------------------------------------
// Event groups (recursive)
// ---------------------------------------------------------------------------

FevEventGroup read_event_group(BinaryReader& r, const FevParseContext& ctx) {
    FevEventGroup grp;
    grp.name = ctx.read_indexed_name(r);

    uint32_t user_prop_count = r.read_u32();
    grp.user_properties.reserve(user_prop_count);
    for (uint32_t i = 0; i < user_prop_count; ++i) {
        grp.user_properties.push_back(read_user_property(r));
    }

    uint32_t subgroup_count = r.read_u32();
    uint32_t event_count = r.read_u32();

    grp.subgroups.reserve(subgroup_count);
    for (uint32_t i = 0; i < subgroup_count; ++i) {
        grp.subgroups.push_back(read_event_group(r, ctx));
    }

    grp.events.reserve(event_count);
    for (uint32_t i = 0; i < event_count; ++i) {
        grp.events.push_back(read_event(r, ctx));
    }

    return grp;
}

// ---------------------------------------------------------------------------
// Sound definition configs
// ---------------------------------------------------------------------------

FevSoundDefinitionConfig read_sound_definition_config(BinaryReader& r) {
    FevSoundDefinitionConfig cfg;
    cfg.play_mode = static_cast<FevPlayMode>(r.read_u32());
    cfg.spawn_time_min = r.read_u32();
    cfg.spawn_time_max = r.read_u32();
    cfg.maximum_spawned_sounds = r.read_u32();
    cfg.volume = r.read_f32();
    cfg.volume_rand_method = r.read_u32();
    cfg.volume_random_min = r.read_f32();
    cfg.volume_random_max = r.read_f32();
    cfg.volume_randomization = r.read_f32();
    cfg.pitch = r.read_f32();
    cfg.pitch_rand_method = r.read_u32();
    cfg.pitch_random_min = r.read_f32();
    cfg.pitch_random_max = r.read_f32();
    cfg.pitch_randomization = r.read_f32();
    cfg.pitch_randomization_behavior = static_cast<FevPitchRandomizationBehavior>(r.read_u32());
    cfg.threed_position_randomization = r.read_f32();
    cfg.trigger_delay_min = r.read_u16();
    cfg.trigger_delay_max = r.read_u16();
    cfg.spawn_count = r.read_u16();
    return cfg;
}

// ---------------------------------------------------------------------------
// Waveforms
// ---------------------------------------------------------------------------

FevWaveform read_waveform(BinaryReader& r) {
    FevWaveform wf;
    wf.type = static_cast<FevWaveformType>(r.read_u32());
    wf.weight = r.read_u32();

    switch (wf.type) {
        case FevWaveformType::WAVETABLE: {
            FevWavetableParams wp;
            wp.filename = read_fev_string(r);
            wp.bank_name = read_fev_string(r);
            wp.percentage_locked = r.read_u32();
            wp.length_ms = r.read_u32();
            wf.params = std::move(wp);
            break;
        }
        case FevWaveformType::OSCILLATOR: {
            FevOscillatorParams op;
            op.type = static_cast<FevOscillatorType>(r.read_u32());
            op.frequency = r.read_f32();
            wf.params = op;
            break;
        }
        case FevWaveformType::DONT_PLAY:
            wf.params = std::monostate{};
            break;
        case FevWaveformType::PROGRAMMER:
            wf.params = std::monostate{};
            break;
        default:
            wf.params = std::monostate{};
            break;
    }

    return wf;
}

// ---------------------------------------------------------------------------
// Sound definitions
// ---------------------------------------------------------------------------

FevSoundDefinition read_sound_definition(BinaryReader& r) {
    FevSoundDefinition sd;
    sd.name = read_fev_string(r);
    sd.config_index = r.read_u32();

    uint32_t wf_count = r.read_u32();
    sd.waveforms.reserve(wf_count);
    for (uint32_t i = 0; i < wf_count; ++i) {
        sd.waveforms.push_back(read_waveform(r));
    }

    return sd;
}

// ---------------------------------------------------------------------------
// Reverb definitions
// ---------------------------------------------------------------------------

FevReverbDefinition read_reverb_definition(BinaryReader& r) {
    FevReverbDefinition rv;
    rv.name = read_fev_string(r);
    rv.room = r.read_s32();
    rv.room_hf = r.read_s32();
    rv.room_rolloff_factor = r.read_f32();
    rv.decay_time = r.read_f32();
    rv.decay_hf_ratio = r.read_f32();
    rv.reflections = r.read_s32();
    rv.reflections_delay = r.read_f32();
    rv.reverb = r.read_s32();
    rv.reverb_delay = r.read_f32();
    rv.diffusion = r.read_f32();
    rv.density = r.read_f32();
    rv.hf_reference = r.read_f32();
    rv.room_lf = r.read_f32();
    rv.lf_reference = r.read_f32();
    rv.instance = r.read_s32();
    rv.environment = r.read_s32();
    rv.env_size = r.read_f32();
    rv.env_diffusion = r.read_f32();
    rv.room_lf_b = r.read_s32();
    // 48-byte extended properties block (12 × f32)
    rv.reflections_pan[0] = r.read_f32();
    rv.reflections_pan[1] = r.read_f32();
    rv.reflections_pan[2] = r.read_f32();
    rv.reverb_pan[0] = r.read_f32();
    rv.reverb_pan[1] = r.read_f32();
    rv.reverb_pan[2] = r.read_f32();
    rv.echo_time = r.read_f32();
    rv.echo_depth = r.read_f32();
    rv.modulation_time = r.read_f32();
    rv.modulation_depth = r.read_f32();
    rv.air_absorption_hf = r.read_f32();
    rv.lf_reference_ext = r.read_f32();
    rv.lf_reference_b = r.read_f32();
    rv.flags = r.read_u32();
    return rv;
}

// ---------------------------------------------------------------------------
// Music data
// ---------------------------------------------------------------------------

// Forward declarations for recursive parsing
std::vector<FevMusicDataItem> read_music_data_items(BinaryReader& r);
std::vector<FevMusicDataChunk> read_music_data_chunks(BinaryReader& r);

std::vector<FevMusicDataItem> read_music_data_items(BinaryReader& r) {
    std::vector<FevMusicDataItem> items;
    while (r.remaining() >= 4) {
        FevMusicDataItem item;
        item.total_length = r.read_u32();
        if (item.total_length < 4) break;

        uint32_t data_len = item.total_length - 4;
        if (data_len > r.remaining()) break;

        // Parse the data portion as chunks using a sub-reader
        size_t start_pos = r.pos();
        auto sub_data = r.read_bytes(data_len);
        BinaryReader sub_reader(sub_data);
        item.chunks = read_music_data_chunks(sub_reader);

        items.push_back(std::move(item));
    }
    return items;
}

std::vector<FevMusicDataChunk> read_music_data_chunks(BinaryReader& r) {
    std::vector<FevMusicDataChunk> chunks;

    while (r.remaining() >= 4) {
        FevMusicDataChunk chunk;

        // Read 4-char type tag
        auto tag = r.read_bytes(4);
        chunk.type = std::string(reinterpret_cast<const char*>(tag.data()), 4);

        // Container types: recurse into nested music_data items
        if (chunk.type == "comp" || chunk.type == "thms" || chunk.type == "cues" ||
            chunk.type == "scns" || chunk.type == "prms" || chunk.type == "sgms" ||
            chunk.type == "smps" || chunk.type == "smpf" || chunk.type == "lnks" ||
            chunk.type == "tlns") {
            // Remaining data in this reader is the nested music data
            // Create a sub-reader for remaining bytes
            auto remaining_data = r.read_bytes(r.remaining());
            BinaryReader nested_reader(remaining_data);
            chunk.body = read_music_data_items(nested_reader);
        }
        // sett: volume + reverb (8 bytes)
        else if (chunk.type == "sett") {
            FevMdSett sett;
            sett.volume = r.read_f32();
            sett.reverb = r.read_f32();
            chunk.body = sett;
        }
        // u16 header types
        else if (chunk.type == "thmh" || chunk.type == "scnh" || chunk.type == "prmh" ||
                 chunk.type == "sgmh" || chunk.type == "lnkh" || chunk.type == "lfsh" ||
                 chunk.type == "tlnh") {
            chunk.body = r.read_u16();
        }
        // thmd: theme data
        else if (chunk.type == "thmd") {
            FevMdThmd thmd;
            thmd.theme_id = r.read_u32();
            thmd.playback_method = static_cast<FevThemePlaybackMethod>(r.read_u8());
            thmd.default_transition = static_cast<FevThemeDefaultTransition>(r.read_u8());
            thmd.quantization = static_cast<FevThemeQuantization>(r.read_u8());
            thmd.transition_timeout = r.read_u32();
            thmd.crossfade_duration = r.read_u32();

            uint16_t end_count = r.read_u16();
            thmd.end_sequence_ids.reserve(end_count);
            for (uint16_t i = 0; i < end_count; ++i) {
                thmd.end_sequence_ids.push_back(r.read_u32());
            }

            uint16_t start_count = r.read_u16();
            thmd.start_sequence_ids.reserve(start_count);
            for (uint16_t i = 0; i < start_count; ++i) {
                thmd.start_sequence_ids.push_back(r.read_u32());
            }

            chunk.body = std::move(thmd);
        }
        // entl: cue entry list
        else if (chunk.type == "entl") {
            FevMdEntl entl;
            uint16_t count = r.read_u16();
            entl.names_length = r.read_u16();

            entl.ids.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                entl.ids.push_back(r.read_u32());
            }

            entl.cue_names.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                entl.cue_names.push_back(read_null_terminated_string(r));
            }

            chunk.body = std::move(entl);
        }
        // scnd: sound data
        else if (chunk.type == "scnd") {
            FevMdScnd scnd;
            scnd.scene_id = r.read_u32();

            uint16_t count = r.read_u16();
            scnd.cue_instances.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                FevCueInstance ci;
                ci.cue_id = r.read_u32();
                ci.condition_id = r.read_u32();
                scnd.cue_instances.push_back(ci);
            }

            chunk.body = std::move(scnd);
        }
        // prmd: parameter data (u32)
        else if (chunk.type == "prmd") {
            chunk.body = r.read_u32();
        }
        // sgmd: segment data with nested music data
        else if (chunk.type == "sgmd") {
            FevMdSgmd sgmd;
            sgmd.segment_id = r.read_u32();
            sgmd.segment_length = r.read_u32();
            sgmd.timeline_id = r.read_u32();
            sgmd.time_signature_beats = r.read_u8();
            sgmd.time_signature_beat_value = r.read_u8();
            sgmd.beats_per_minute = r.read_f32();
            sgmd.segment_tempo = r.read_f32();
            auto sb = r.read_bytes(4);
            std::memcpy(sgmd.sync_beats, sb.data(), 4);

            chunk.body = std::move(sgmd);

            // Remaining data is nested music data (the "data" field in ksy)
            if (r.remaining() > 0) {
                auto nested_data = r.read_bytes(r.remaining());
                BinaryReader nested_reader(nested_data);
                chunk.sgmd_nested_items = read_music_data_items(nested_reader);
            }
        }
        // smph: sample header
        else if (chunk.type == "smph") {
            FevMdSmph smph;
            smph.playback_mode = static_cast<FevSamplePlaybackMode>(r.read_u8());
            smph.count = r.read_u32();
            chunk.body = smph;
        }
        // str: string list
        else if (chunk.type == "str ") {
            FevMdStr str;
            uint32_t count = r.read_u32();
            str.total_string_data_size = r.read_u32();

            str.name_end_offsets.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                str.name_end_offsets.push_back(r.read_u32());
            }

            str.names.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                str.names.push_back(read_null_terminated_string(r));
            }

            if (count == 0 && r.remaining() >= 1) {
                // end_marker: single 0x00 byte
                r.read_u8();
            }

            chunk.body = std::move(str);
        }
        // smpm: sample map (u32)
        else if (chunk.type == "smpm") {
            chunk.body = r.read_u32();
        }
        // smp: sample reference
        else if (chunk.type == "smp ") {
            FevMdSmp smp;
            smp.bank_name = read_fev_string(r);
            smp.index = r.read_u32();
            chunk.body = std::move(smp);
        }
        // lnkd: link data
        else if (chunk.type == "lnkd") {
            FevMdLnkd lnkd;
            lnkd.segment_1_id = r.read_u32();
            lnkd.segment_2_id = r.read_u32();

            auto tb = r.read_bytes(4);
            std::memcpy(lnkd.transition_behavior.raw, tb.data(), 4);
            // Byte 0 bit layout (big-endian bit numbering):
            //   b1: padding
            //   b1: at_segment_end
            //   b1: on_bar
            //   b1: on_beat
            //   + 4 bits lower
            uint8_t b0 = lnkd.transition_behavior.raw[0];
            lnkd.transition_behavior.at_segment_end = (b0 >> 6) & 1;
            lnkd.transition_behavior.on_bar = (b0 >> 5) & 1;
            lnkd.transition_behavior.on_beat = (b0 >> 4) & 1;

            chunk.body = std::move(lnkd);
        }
        // lfsd: link from-segment data
        else if (chunk.type == "lfsd") {
            FevMdLfsd lfsd;
            lfsd.from_segment_id = r.read_u32();

            uint16_t count = r.read_u16();
            lfsd.link_ids.reserve(count);
            for (uint16_t i = 0; i < count; ++i) {
                lfsd.link_ids.push_back(r.read_u32());
            }

            chunk.body = std::move(lfsd);
        }
        // tlnd: timeline data
        else if (chunk.type == "tlnd") {
            FevMdTlnd tlnd;
            tlnd.timeline_id = r.read_u32();
            chunk.body = tlnd;
        }
        // cond: condition (empty body)
        else if (chunk.type == "cond") {
            chunk.body = std::monostate{};
        }
        // cms: condition match set
        else if (chunk.type == "cms ") {
            FevMdCms cms;
            cms.condition_type = static_cast<FevConditionType_Cms>(r.read_u8());
            cms.theme_id = r.read_u32();
            cms.cue_id = r.read_u32();
            chunk.body = std::move(cms);
        }
        // cprm: condition parameter
        else if (chunk.type == "cprm") {
            FevMdCprm cprm;
            cprm.condition_type = static_cast<FevConditionType_Cprm>(r.read_u16());
            cprm.param_id = r.read_u32();
            cprm.value_1 = r.read_u32();
            cprm.value_2 = r.read_u32();
            chunk.body = std::move(cprm);
        }
        // thm / lnk : per-item containers (each wraps sub-chunks like lnkd, thmd, cond)
        else if (chunk.type == "thm " || chunk.type == "lnk ") {
            auto remaining_data = r.read_bytes(r.remaining());
            BinaryReader nested_reader(remaining_data);
            chunk.body = read_music_data_items(nested_reader);
        }
        // scn : scene container - handle like other containers if it appears
        else {
            // Unknown chunk type: skip remaining bytes
            if (r.remaining() > 0) {
                r.skip(r.remaining());
            }
            chunk.body = std::monostate{};
        }

        chunks.push_back(std::move(chunk));
    }

    return chunks;
}

// ---------------------------------------------------------------------------
// Top-level parse
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// RIFF FEV chunk helpers
// ---------------------------------------------------------------------------

// Find a chunk by ID within a RIFF/LIST body (no recursion into sub-LISTs).
std::span<const uint8_t> find_riff_chunk(std::span<const uint8_t> body, uint32_t chunk_id) {
    size_t pos = 0;
    while (pos + 8 <= body.size()) {
        uint32_t cid;
        std::memcpy(&cid, body.data() + pos, 4);
        uint32_t csz;
        std::memcpy(&csz, body.data() + pos + 4, 4);

        if (cid == chunk_id) {
            size_t end = pos + 8 + csz;
            if (end > body.size()) end = body.size();
            return body.subspan(pos + 8, end - pos - 8);
        }

        // LIST/RIFF chunks: skip the form-type byte and recurse into their body
        if (cid == RIFF_MAGIC || cid == 0x5453494C /* "LIST" */) {
            pos += 12; // skip id(4) + size(4) + form_type(4), then walk sub-chunks
            continue;
        }

        size_t advance = 8 + csz;
        if (advance % 2) ++advance;
        pos += advance;
    }
    return {};
}

// Parse a STRR string reference table: count(u32) + offsets[count](u32) + string pool.
std::vector<std::string> parse_strr(std::span<const uint8_t> data) {
    if (data.size() < 4) return {};
    BinaryReader r(data);
    uint32_t count = r.read_u32();
    std::vector<uint32_t> offsets(count);
    for (uint32_t i = 0; i < count; ++i) offsets[i] = r.read_u32();
    size_t str_base = r.pos();

    std::vector<std::string> strings;
    strings.reserve(count);
    for (uint32_t off : offsets) {
        size_t abs = str_base + off;
        if (abs >= data.size()) { strings.emplace_back(); continue; }
        // Find null terminator
        size_t end = abs;
        while (end < data.size() && data[end] != 0) ++end;
        strings.emplace_back(reinterpret_cast<const char*>(data.data() + abs), end - abs);
    }
    return strings;
}

// Parse a RIFF-based FEV file.
FevFile fev_parse_riff(std::span<const uint8_t> data) {
    // Skip RIFF header: "RIFF"(4) + size(4) + "FEV "(4) = 12 bytes
    auto body = data.subspan(12);

    // Find chunks within LIST/PROJ
    auto strr_data = find_riff_chunk(body, 0x52525453); // "STRR"
    auto lgcy_data = find_riff_chunk(body, 0x5943474C); // "LGCY"
    auto obct_data = find_riff_chunk(body, 0x5443424F); // "OBCT"
    auto prop_data = find_riff_chunk(body, 0x504F5250); // "PROP"

    if (lgcy_data.empty()) {
        throw FevError("RIFF FEV: missing LGCY chunk");
    }

    // Parse STRR table
    std::vector<std::string> strr = parse_strr(strr_data);

    // Set up parsing context
    FevParseContext ctx;
    ctx.strr = &strr;

    // Parse OBCT manifest (same format as FEV1)
    FevFile fev;
    if (!obct_data.empty()) {
        BinaryReader mr(obct_data);
        uint32_t manifest_count = mr.read_u32();
        fev.manifest.reserve(manifest_count);
        for (uint32_t i = 0; i < manifest_count; ++i) {
            FevManifestEntry entry;
            entry.type = static_cast<FevManifestType>(mr.read_u32());
            entry.value = mr.read_u32();
            fev.manifest.push_back(entry);
        }
    }

    // Parse LGCY body
    BinaryReader r(lgcy_data);

    // Pool sizes (same as FEV1 header bytes 8-15)
    fev.sound_def_names_pool_size = r.read_u32();
    fev.waveform_names_pool_size = r.read_u32();

    // Project name
    fev.project_name = read_fev_string(r);

    // Banks — format differs from FEV1:
    // bank_count(u32), lang_count(u32),
    // per-bank: load_mode(u32), max_streams(s32),
    //   [ck0(u32), ck1(u32), unk(u32)] × lang_count, name(string)
    uint32_t bank_count = r.read_u32();
    ctx.lang_count = (bank_count > 0) ? r.read_u32() : 1;
    fev.banks.reserve(bank_count);
    for (uint32_t i = 0; i < bank_count; ++i) {
        FevBank bank;
        bank.load_mode = static_cast<FevBankLoadMode>(r.read_u32());
        bank.max_streams = r.read_s32();
        for (uint32_t lc = 0; lc < ctx.lang_count; ++lc) {
            uint32_t ck0 = r.read_u32();
            uint32_t ck1 = r.read_u32();
            r.read_u32(); // unk
            if (lc == 0) {
                bank.fsb_checksum[0] = ck0;
                bank.fsb_checksum[1] = ck1;
            }
        }
        bank.name = read_fev_string(r);
        fev.banks.push_back(std::move(bank));
    }

    // Root event category (same recursive format as FEV1 — inline strings)
    fev.root_category = read_event_category(r);

    // Event groups and everything after.
    // The RIFF FEV effect envelope format is variable-length and not fully
    // reverse-engineered. If parsing fails mid-event, we keep whatever was
    // successfully parsed (banks, categories, partial event groups) and
    // continue — this is better than failing the entire file.
    try {
    uint32_t root_group_count = r.read_u32();
    fev.event_groups.reserve(root_group_count);
    for (uint32_t i = 0; i < root_group_count; ++i) {
        fev.event_groups.push_back(read_event_group(r, ctx));
    }

    // Sound definition configs
    uint32_t sdc_count = r.read_u32();
    fev.sound_definition_configs.reserve(sdc_count);
    for (uint32_t i = 0; i < sdc_count; ++i) {
        fev.sound_definition_configs.push_back(read_sound_definition_config(r));
    }

    // Sound definitions (inline strings — same as FEV1)
    uint32_t sd_count = r.read_u32();
    fev.sound_definitions.reserve(sd_count);
    for (uint32_t i = 0; i < sd_count; ++i) {
        fev.sound_definitions.push_back(read_sound_definition(r));
    }

    // Reverb definitions
    uint32_t rv_count = r.read_u32();
    fev.reverb_definitions.reserve(rv_count);
    for (uint32_t i = 0; i < rv_count; ++i) {
        fev.reverb_definitions.push_back(read_reverb_definition(r));
    }

    // Music data
    if (r.remaining() > 0) {
        auto music_span = r.read_bytes(r.remaining());
        BinaryReader music_reader(music_span);
        fev.music_data.items = read_music_data_items(music_reader);
    }

    } catch (const std::exception&) {
        // RIFF FEV envelope format is partially understood — return what we have.
        // Banks, categories, and partial event groups are still useful for FDP generation.
    }

    return fev;
}

// Parse a FEV1 binary file (magic already verified by caller).
FevFile fev_parse_fev1(std::span<const uint8_t> data) {
    BinaryReader r(data);
    FevFile fev;

    r.read_u32();  // skip magic "FEV1"

    // Version (always 0x00004000 for LU)
    fev.version = r.read_u32();

    // Name-pool allocation hints (not checksums — see fev.h for RE details)
    fev.sound_def_names_pool_size = r.read_u32();
    fev.waveform_names_pool_size = r.read_u32();

    // Manifest: count + array of (type, value) pairs
    uint32_t manifest_count = r.read_u32();
    fev.manifest.reserve(manifest_count);
    for (uint32_t i = 0; i < manifest_count; ++i) {
        FevManifestEntry entry;
        entry.type = static_cast<FevManifestType>(r.read_u32());
        entry.value = r.read_u32();
        fev.manifest.push_back(entry);
    }

    // Project name (u32-prefixed string)
    fev.project_name = read_fev_string(r);

    // Banks
    uint32_t bank_count = r.read_u32();
    fev.banks.reserve(bank_count);
    for (uint32_t i = 0; i < bank_count; ++i) {
        FevBank bank;
        bank.load_mode = static_cast<FevBankLoadMode>(r.read_u32());
        bank.max_streams = r.read_s32();
        bank.fsb_checksum[0] = r.read_u32();
        bank.fsb_checksum[1] = r.read_u32();
        bank.name = read_fev_string(r);
        fev.banks.push_back(std::move(bank));
    }

    // Root event category (recursive tree)
    fev.root_category = read_event_category(r);

    // Event groups
    FevParseContext ctx; // default: FEV1 mode (no STRR)
    uint32_t root_group_count = r.read_u32();
    fev.event_groups.reserve(root_group_count);
    for (uint32_t i = 0; i < root_group_count; ++i) {
        fev.event_groups.push_back(read_event_group(r, ctx));
    }

    // Sound definition configs
    uint32_t sdc_count = r.read_u32();
    fev.sound_definition_configs.reserve(sdc_count);
    for (uint32_t i = 0; i < sdc_count; ++i) {
        fev.sound_definition_configs.push_back(read_sound_definition_config(r));
    }

    // Sound definitions
    uint32_t sd_count = r.read_u32();
    fev.sound_definitions.reserve(sd_count);
    for (uint32_t i = 0; i < sd_count; ++i) {
        fev.sound_definitions.push_back(read_sound_definition(r));
    }

    // Reverb definitions
    uint32_t rv_count = r.read_u32();
    fev.reverb_definitions.reserve(rv_count);
    for (uint32_t i = 0; i < rv_count; ++i) {
        fev.reverb_definitions.push_back(read_reverb_definition(r));
    }

    // Music data (items until end of stream)
    if (r.remaining() > 0) {
        auto music_span = r.read_bytes(r.remaining());
        BinaryReader music_reader(music_span);
        fev.music_data.items = read_music_data_items(music_reader);
    }

    return fev;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

FevFile fev_parse(std::span<const uint8_t> data) {
    if (data.size() < 16) {
        throw FevError("FEV: file too small for header");
    }

    uint32_t magic;
    std::memcpy(&magic, data.data(), 4);

    if (magic == RIFF_MAGIC) {
        // RIFF-based FEV (FMOD Designer 4.45, version 0x00450000)
        if (data.size() < 12) throw FevError("RIFF FEV: file too small");
        uint32_t form_type;
        std::memcpy(&form_type, data.data() + 8, 4);
        if (form_type != 0x20564546) // "FEV "
            throw FevError("RIFF FEV: not FEV form type");
        return fev_parse_riff(data);
    }

    if (magic == FEV1_MAGIC) {
        return fev_parse_fev1(data);
    }

    throw FevError("FEV: invalid magic (expected FEV1 or RIFF)");
}

bool fev_verify_bank_checksum(const FevBank& bank,
                              std::span<const uint8_t> decrypted_fsb_data) {
    auto fsb_ck = fsb_read_bank_checksums(decrypted_fsb_data);
    return fsb_ck[0] == bank.fsb_checksum[0] && fsb_ck[1] == bank.fsb_checksum[1];
}

} // namespace lu::assets
