#include "fmod/fev/fev_writer.h"
#include "common/binary_writer/binary_writer.h"

#include <cstring>
#include <string>
#include <variant>

namespace lu::assets {

namespace {

// ---------------------------------------------------------------------------
// Write context — mirrors the reader's FevParseContext.
// ---------------------------------------------------------------------------
// In RIFF mode, indexed names are written as u32 STRR indices (not inline
// strings), effect-envelope points are written position-only (their full
// records are collected into eprp for the separate EPRP chunk), and banks use
// per-language checksum arrays. In FEV1 mode everything is inline.
struct FevWriteContext {
    bool riff = false;
    // Collects (position, value, curve) for every envelope point in traversal
    // order — becomes the EPRP chunk. Only used in RIFF mode.
    std::vector<const FevEffectEnvelopePoint*> eprp;
};

// ---------------------------------------------------------------------------
// String helpers (mirror read_fev_string / read_null_terminated_string)
// ---------------------------------------------------------------------------

// u32-length-prefixed string. The reader strips the trailing NUL that FEV includes
// in the length, so the writer re-adds it: an empty string writes just a 0 length
// (no bytes), a non-empty string writes (content.size() + 1) as the length followed
// by the content and one NUL. This reproduces every real client string exactly (all
// observed FEV strings use exactly strlen+1); a mismatch would surface immediately in
// the round-trip sweep.
void write_fev_string(BinaryWriter& w, const std::string& s) {
    if (s.empty()) {
        w.write_u32(0);
        return;
    }
    w.write_u32(static_cast<uint32_t>(s.size() + 1));
    w.write_bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    w.write_u8(0);
}

// A NUL-terminated ASCII string (used inside some music-data chunks).
void write_null_terminated_string(BinaryWriter& w, const std::string& s) {
    w.write_bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    w.write_u8(0);
}

// Write a name field: a u32 STRR index in RIFF mode (the exact slot recorded at
// parse time in name_strr_index), an inline u32-prefixed string in FEV1 mode.
void write_indexed_name(BinaryWriter& w, const std::string& name,
                        int32_t strr_index, const FevWriteContext& ctx) {
    if (ctx.riff) {
        w.write_u32(static_cast<uint32_t>(strr_index));
    } else {
        write_fev_string(w, name);
    }
}

// ---------------------------------------------------------------------------
// User properties
// ---------------------------------------------------------------------------

void write_user_property(BinaryWriter& w, const FevUserProperty& prop) {
    write_fev_string(w, prop.name);
    w.write_u32(static_cast<uint32_t>(prop.type));
    switch (prop.type) {
        case FevUserPropertyType::INTEGER:
            w.write_u32(std::get<uint32_t>(prop.value));
            break;
        case FevUserPropertyType::FLOAT:
            w.write_f32(std::get<float>(prop.value));
            break;
        case FevUserPropertyType::STRING:
            write_fev_string(w, std::get<std::string>(prop.value));
            break;
        default:
            // Reader treats unknown types as integer.
            w.write_u32(std::get<uint32_t>(prop.value));
            break;
    }
}

// ---------------------------------------------------------------------------
// Event categories (recursive)
// ---------------------------------------------------------------------------

void write_event_category(BinaryWriter& w, const FevEventCategory& cat) {
    write_fev_string(w, cat.name);
    w.write_f32(cat.volume);
    w.write_f32(cat.pitch);
    w.write_s32(cat.max_streams);
    w.write_u32(static_cast<uint32_t>(cat.max_playback_behavior));

    w.write_u32(static_cast<uint32_t>(cat.subcategories.size()));
    for (const auto& sub : cat.subcategories) {
        write_event_category(w, sub);
    }
}

// ---------------------------------------------------------------------------
// Effect envelopes
// ---------------------------------------------------------------------------

void write_effect_envelope(BinaryWriter& w, const FevEffectEnvelope& env,
                           FevWriteContext& ctx) {
    w.write_s32(env.control_parameter_index);

    if (ctx.riff) {
        // RIFF: no inline name, no flags2/mapping. 5-word header (dsp, extra,
        // flags, extra), position-only points, 2-word tail. Full (x, y, curve)
        // point records go to the EPRP chunk in this traversal order.
        w.write_s32(env.dsp_effect_index);
        w.write_u32(env.riff_extra[0]);
        w.write_u32(env.envelope_flags);
        w.write_u32(env.riff_extra[1]);
        w.write_u32(static_cast<uint32_t>(env.points.size()));
        for (const auto& pt : env.points) {
            w.write_u32(pt.position);
            ctx.eprp.push_back(&pt);
        }
        w.write_u32(env.riff_tail[0]);
        w.write_u32(env.riff_tail[1]);
        return;
    }

    // FEV1: inline name string, full point records, flags2 and mapping bytes.
    write_fev_string(w, env.name);
    w.write_s32(env.dsp_effect_index);
    w.write_u32(env.envelope_flags);
    w.write_u32(env.envelope_flags2);

    w.write_u32(static_cast<uint32_t>(env.points.size()));
    for (const auto& pt : env.points) {
        w.write_u32(pt.position);
        w.write_f32(pt.value);
        w.write_u32(static_cast<uint32_t>(pt.curve_shape));
    }

    w.write_bytes(env.mapping_data, 4);
    w.write_u32(env.enabled);
}

// ---------------------------------------------------------------------------
// Sound instances
// ---------------------------------------------------------------------------

void write_sound_instance(BinaryWriter& w, const FevSoundInstance& si) {
    w.write_u16(si.sound_definition_index);
    w.write_f32(si.start_position);
    w.write_f32(si.length);
    w.write_u32(static_cast<uint32_t>(si.start_mode));
    w.write_u16(static_cast<uint16_t>(si.loop_mode));
    w.write_u16(static_cast<uint16_t>(si.autopitch_parameter));
    w.write_s32(si.loop_count);
    w.write_u32(static_cast<uint32_t>(si.autopitch_enabled));
    w.write_f32(si.autopitch_reference);
    w.write_f32(si.autopitch_at_min);
    w.write_f32(si.fine_tune);
    w.write_f32(si.volume);
    w.write_f32(si.volume_randomization);
    w.write_f32(si.pitch);
    w.write_u32(si.fade_in_type);
    w.write_u32(si.fade_out_type);
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

void write_layer(BinaryWriter& w, const FevLayer& layer, bool is_simple_event,
                 FevWriteContext& ctx) {
    if (!is_simple_event) {
        w.write_bytes(layer.layer_flags, 2);
        w.write_s16(layer.priority);
        w.write_s16(layer.control_parameter_index);
    }

    w.write_u16(static_cast<uint16_t>(layer.sound_instances.size()));
    w.write_u16(static_cast<uint16_t>(layer.effect_envelopes.size()));

    for (const auto& si : layer.sound_instances) {
        write_sound_instance(w, si);
    }
    for (const auto& env : layer.effect_envelopes) {
        write_effect_envelope(w, env, ctx);
    }
}

// ---------------------------------------------------------------------------
// Event parameters
// ---------------------------------------------------------------------------

void write_event_parameter(BinaryWriter& w, const FevEventParameter& param,
                           const FevWriteContext& ctx) {
    write_indexed_name(w, param.name, param.name_strr_index, ctx);
    w.write_f32(param.velocity);
    w.write_f32(param.minimum_value);
    w.write_f32(param.maximum_value);
    w.write_bytes(param.flags.raw, 4); // flag bitfield replayed verbatim
    w.write_f32(param.seek_speed);
    w.write_u32(param.unknown_value);
    w.write_u32(static_cast<uint32_t>(param.unknown_extra.size()));
    for (uint32_t v : param.unknown_extra) {
        w.write_u32(v);
    }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

void write_event(BinaryWriter& w, const FevEvent& ev, FevWriteContext& ctx) {
    w.write_u32(static_cast<uint32_t>(ev.event_type));
    bool is_simple = (ev.event_type == FevEventType::SIMPLE);

    write_indexed_name(w, ev.name, ev.name_strr_index, ctx);
    w.write_bytes(ev.guid, 16);

    w.write_f32(ev.volume);
    w.write_f32(ev.pitch);
    w.write_f32(ev.pitch_randomization);
    w.write_f32(ev.volume_randomization);
    w.write_u16(ev.priority);
    w.write_u16(ev.max_instances);
    w.write_u32(ev.max_playbacks);
    w.write_u32(ev.steal_priority);

    w.write_bytes(ev.threed_flags.raw, 4);
    w.write_f32(ev.threed_min_distance);
    w.write_f32(ev.threed_max_distance);
    w.write_bytes(ev.event_flags.raw, 4);

    if (ctx.riff) {
        // Two version-gated u32 between event_flags and speaker levels (RIFF).
        w.write_u32(ev.riff_extra_after_flags[0]);
        w.write_u32(ev.riff_extra_after_flags[1]);
    }

    w.write_f32(ev.speaker_l);
    w.write_f32(ev.speaker_r);
    w.write_f32(ev.speaker_c);
    w.write_f32(ev.speaker_lfe);
    w.write_f32(ev.speaker_lr);
    w.write_f32(ev.speaker_rr);
    w.write_f32(ev.speaker_ls);
    w.write_f32(ev.speaker_rs);

    w.write_f32(ev.threed_cone_inside_angle);
    w.write_f32(ev.threed_cone_outside_angle);
    w.write_f32(ev.threed_cone_outside_volume);

    w.write_u32(static_cast<uint32_t>(ev.max_playbacks_behavior));

    w.write_f32(ev.threed_doppler_factor);
    w.write_f32(ev.reverb_dry_level);
    w.write_f32(ev.reverb_wet_level);
    w.write_f32(ev.threed_speaker_spread);

    w.write_u16(ev.fade_in_time);
    w.write_u16(ev.fade_in_time_flag);
    w.write_u16(ev.fade_out_time);
    w.write_u16(ev.fade_out_time_flag);

    w.write_f32(ev.spawn_intensity);
    w.write_f32(ev.spawn_intensity_randomization);
    w.write_f32(ev.threed_pan_level);
    w.write_u32(ev.threed_position_randomization);

    if (ctx.riff) {
        // One version-gated u32 before the layer list (RIFF).
        w.write_u32(ev.riff_extra_before_layers);
    }

    if (!is_simple) {
        w.write_u32(static_cast<uint32_t>(ev.layers.size()));
        for (const auto& layer : ev.layers) {
            write_layer(w, layer, false, ctx);
        }
    } else {
        // Simple event: a single layer with no header fields. The reader always
        // stores exactly one; guard so a hand-built struct can't emit a bad file.
        write_layer(w, ev.layers.at(0), true, ctx);
    }

    if (!is_simple) {
        w.write_u32(static_cast<uint32_t>(ev.parameters.size()));
        for (const auto& param : ev.parameters) {
            write_event_parameter(w, param, ctx);
        }

        w.write_u32(static_cast<uint32_t>(ev.user_properties.size()));
        for (const auto& up : ev.user_properties) {
            write_user_property(w, up);
        }
    }

    w.write_u32(ev.category_instance_count);
    write_fev_string(w, ev.category);
}

// ---------------------------------------------------------------------------
// Event groups (recursive)
// ---------------------------------------------------------------------------

void write_event_group(BinaryWriter& w, const FevEventGroup& grp,
                       FevWriteContext& ctx) {
    write_indexed_name(w, grp.name, grp.name_strr_index, ctx);

    w.write_u32(static_cast<uint32_t>(grp.user_properties.size()));
    for (const auto& up : grp.user_properties) {
        write_user_property(w, up);
    }

    w.write_u32(static_cast<uint32_t>(grp.subgroups.size()));
    w.write_u32(static_cast<uint32_t>(grp.events.size()));

    for (const auto& sub : grp.subgroups) {
        write_event_group(w, sub, ctx);
    }
    for (const auto& ev : grp.events) {
        write_event(w, ev, ctx);
    }
}

// ---------------------------------------------------------------------------
// Sound definition configs
// ---------------------------------------------------------------------------

void write_sound_definition_config(BinaryWriter& w, const FevSoundDefinitionConfig& cfg,
                                   const FevWriteContext& ctx) {
    w.write_u32(static_cast<uint32_t>(cfg.play_mode));
    w.write_u32(cfg.spawn_time_min);
    w.write_u32(cfg.spawn_time_max);
    w.write_u32(cfg.maximum_spawned_sounds);
    w.write_f32(cfg.volume);
    w.write_u32(cfg.volume_rand_method);
    w.write_f32(cfg.volume_random_min);
    w.write_f32(cfg.volume_random_max);
    w.write_f32(cfg.volume_randomization);
    w.write_f32(cfg.pitch);
    w.write_u32(cfg.pitch_rand_method);
    w.write_f32(cfg.pitch_random_min);
    w.write_f32(cfg.pitch_random_max);
    w.write_f32(cfg.pitch_randomization);
    w.write_u32(static_cast<uint32_t>(cfg.pitch_randomization_behavior));
    w.write_f32(cfg.threed_position_randomization);
    w.write_u16(cfg.trigger_delay_min);
    w.write_u16(cfg.trigger_delay_max);
    w.write_u16(cfg.spawn_count);
    if (ctx.riff) {
        // Two version-gated trailing u32 (RIFF config).
        w.write_u32(cfg.riff_extra[0]);
        w.write_u32(cfg.riff_extra[1]);
    }
}

// ---------------------------------------------------------------------------
// Waveforms / sound definitions
// ---------------------------------------------------------------------------

void write_waveform(BinaryWriter& w, const FevWaveform& wf) {
    w.write_u32(static_cast<uint32_t>(wf.type));
    w.write_u32(wf.weight);

    switch (wf.type) {
        case FevWaveformType::WAVETABLE: {
            const auto& wp = std::get<FevWavetableParams>(wf.params);
            write_fev_string(w, wp.filename);
            write_fev_string(w, wp.bank_name);
            w.write_u32(wp.percentage_locked);
            w.write_u32(wp.length_ms);
            break;
        }
        case FevWaveformType::OSCILLATOR: {
            const auto& op = std::get<FevOscillatorParams>(wf.params);
            w.write_u32(static_cast<uint32_t>(op.type));
            w.write_f32(op.frequency);
            break;
        }
        default:
            // DONT_PLAY / PROGRAMMER / unknown: no params.
            break;
    }
}

void write_sound_definition(BinaryWriter& w, const FevSoundDefinition& sd,
                            const FevWriteContext& ctx) {
    // RIFF stores the sound-definition name as a u32 STRR index; FEV1 inline.
    write_indexed_name(w, sd.name, sd.name_strr_index, ctx);
    w.write_u32(sd.config_index);
    w.write_u32(static_cast<uint32_t>(sd.waveforms.size()));
    for (const auto& wf : sd.waveforms) {
        write_waveform(w, wf);
    }
}

// ---------------------------------------------------------------------------
// Reverb definitions
// ---------------------------------------------------------------------------

void write_reverb_definition(BinaryWriter& w, const FevReverbDefinition& rv) {
    write_fev_string(w, rv.name);
    w.write_s32(rv.room);
    w.write_s32(rv.room_hf);
    w.write_f32(rv.room_rolloff_factor);
    w.write_f32(rv.decay_time);
    w.write_f32(rv.decay_hf_ratio);
    w.write_s32(rv.reflections);
    w.write_f32(rv.reflections_delay);
    w.write_s32(rv.reverb);
    w.write_f32(rv.reverb_delay);
    w.write_f32(rv.diffusion);
    w.write_f32(rv.density);
    w.write_f32(rv.hf_reference);
    w.write_f32(rv.room_lf);
    w.write_f32(rv.lf_reference);
    w.write_s32(rv.instance);
    w.write_s32(rv.environment);
    w.write_f32(rv.env_size);
    w.write_f32(rv.env_diffusion);
    w.write_s32(rv.room_lf_b);
    w.write_f32(rv.reflections_pan[0]);
    w.write_f32(rv.reflections_pan[1]);
    w.write_f32(rv.reflections_pan[2]);
    w.write_f32(rv.reverb_pan[0]);
    w.write_f32(rv.reverb_pan[1]);
    w.write_f32(rv.reverb_pan[2]);
    w.write_f32(rv.echo_time);
    w.write_f32(rv.echo_depth);
    w.write_f32(rv.modulation_time);
    w.write_f32(rv.modulation_depth);
    w.write_f32(rv.air_absorption_hf);
    w.write_f32(rv.lf_reference_ext);
    w.write_f32(rv.lf_reference_b);
    w.write_u32(rv.flags);
}

// ---------------------------------------------------------------------------
// Music data (recursive, length-prefixed items)
// ---------------------------------------------------------------------------

void write_music_data_items(BinaryWriter& w, const std::vector<FevMusicDataItem>& items);

void write_music_data_chunk(BinaryWriter& w, const FevMusicDataChunk& chunk) {
    // 4-char type tag.
    w.write_bytes(reinterpret_cast<const uint8_t*>(chunk.type.data()), 4);

    const std::string& t = chunk.type;
    if (t == "comp" || t == "thms" || t == "cues" || t == "scns" || t == "prms" ||
        t == "sgms" || t == "smps" || t == "smpf" || t == "lnks" || t == "tlns" ||
        t == "thm " || t == "lnk " || t == "cond") {
        // Container types: the body is the remaining bytes of this reader (nested
        // items). "cond" is one too — it wraps nested cprm/cms condition items (or
        // is empty). See the reader for why cond is not a leaf.
        write_music_data_items(w, std::get<std::vector<FevMusicDataItem>>(chunk.body));
    } else if (t == "sett") {
        const auto& sett = std::get<FevMdSett>(chunk.body);
        w.write_f32(sett.volume);
        w.write_f32(sett.reverb);
    } else if (t == "thmh" || t == "scnh" || t == "prmh" || t == "sgmh" ||
               t == "lnkh" || t == "lfsh" || t == "tlnh") {
        w.write_u16(std::get<uint16_t>(chunk.body));
    } else if (t == "thmd") {
        const auto& thmd = std::get<FevMdThmd>(chunk.body);
        w.write_u32(thmd.theme_id);
        w.write_u8(static_cast<uint8_t>(thmd.playback_method));
        w.write_u8(static_cast<uint8_t>(thmd.default_transition));
        w.write_u8(static_cast<uint8_t>(thmd.quantization));
        w.write_u32(thmd.transition_timeout);
        w.write_u32(thmd.crossfade_duration);
        w.write_u16(static_cast<uint16_t>(thmd.end_sequence_ids.size()));
        for (uint32_t id : thmd.end_sequence_ids) w.write_u32(id);
        w.write_u16(static_cast<uint16_t>(thmd.start_sequence_ids.size()));
        for (uint32_t id : thmd.start_sequence_ids) w.write_u32(id);
    } else if (t == "entl") {
        const auto& entl = std::get<FevMdEntl>(chunk.body);
        w.write_u16(static_cast<uint16_t>(entl.ids.size()));
        w.write_u16(entl.names_length);
        for (uint32_t id : entl.ids) w.write_u32(id);
        for (const auto& name : entl.cue_names) write_null_terminated_string(w, name);
    } else if (t == "scnd") {
        const auto& scnd = std::get<FevMdScnd>(chunk.body);
        w.write_u32(scnd.scene_id);
        w.write_u16(static_cast<uint16_t>(scnd.cue_instances.size()));
        for (const auto& ci : scnd.cue_instances) {
            w.write_u32(ci.cue_id);
            w.write_u32(ci.condition_id);
        }
    } else if (t == "prmd") {
        w.write_u32(std::get<uint32_t>(chunk.body));
    } else if (t == "smpm") {
        const auto& smpm = std::get<FevMdSmpm>(chunk.body);
        w.write_u32(static_cast<uint32_t>(smpm.entries.size()));
        for (const auto& e : smpm.entries) {
            w.write_u32(e.a);
            w.write_u32(e.b);
            w.write_u32(e.c);
        }
    } else if (t == "sgmd") {
        const auto& sgmd = std::get<FevMdSgmd>(chunk.body);
        w.write_u32(sgmd.segment_id);
        w.write_u32(sgmd.segment_length);
        w.write_u32(sgmd.timeline_id);
        w.write_u8(sgmd.time_signature_beats);
        w.write_u8(sgmd.time_signature_beat_value);
        w.write_f32(sgmd.beats_per_minute);
        w.write_f32(sgmd.segment_tempo);
        w.write_bytes(sgmd.sync_beats, 4);
        // Remaining bytes: nested music data.
        write_music_data_items(w, chunk.sgmd_nested_items);
    } else if (t == "smph") {
        const auto& smph = std::get<FevMdSmph>(chunk.body);
        w.write_u8(static_cast<uint8_t>(smph.playback_mode));
        w.write_u32(smph.count);
    } else if (t == "str ") {
        const auto& str = std::get<FevMdStr>(chunk.body);
        w.write_u32(static_cast<uint32_t>(str.name_end_offsets.size()));
        w.write_u32(str.total_string_data_size);
        for (uint32_t off : str.name_end_offsets) w.write_u32(off);
        for (const auto& name : str.names) write_null_terminated_string(w, name);
        if (str.name_end_offsets.empty()) {
            // end_marker: a single 0x00 byte when the list is empty (see reader).
            w.write_u8(0);
        }
    } else if (t == "smp ") {
        const auto& smp = std::get<FevMdSmp>(chunk.body);
        write_fev_string(w, smp.bank_name);
        w.write_u32(smp.index);
    } else if (t == "lnkd") {
        const auto& lnkd = std::get<FevMdLnkd>(chunk.body);
        w.write_u32(lnkd.segment_1_id);
        w.write_u32(lnkd.segment_2_id);
        w.write_bytes(lnkd.transition_behavior.raw, 4);
    } else if (t == "lfsd") {
        const auto& lfsd = std::get<FevMdLfsd>(chunk.body);
        w.write_u32(lfsd.from_segment_id);
        w.write_u16(static_cast<uint16_t>(lfsd.link_ids.size()));
        for (uint32_t id : lfsd.link_ids) w.write_u32(id);
    } else if (t == "tlnd") {
        const auto& tlnd = std::get<FevMdTlnd>(chunk.body);
        w.write_u32(tlnd.timeline_id);
    } else if (t == "cms ") {
        const auto& cms = std::get<FevMdCms>(chunk.body);
        w.write_u8(static_cast<uint8_t>(cms.condition_type));
        w.write_u32(cms.theme_id);
        w.write_u32(cms.cue_id);
    } else if (t == "cprm") {
        const auto& cprm = std::get<FevMdCprm>(chunk.body);
        w.write_u16(static_cast<uint16_t>(cprm.condition_type));
        w.write_u32(cprm.param_id);
        w.write_u32(cprm.value_1);
        w.write_u32(cprm.value_2);
    } else {
        // Unknown chunk type: the reader consumed the rest of its enclosing item as
        // opaque bytes it did not retain, so this cannot be reproduced. In practice
        // no real FEV1 file reaches this branch (verified by the round-trip sweep);
        // fail loudly rather than emit a silently-wrong file.
        std::string hex;
        for (unsigned char c : chunk.type) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02x ", c);
            hex += buf;
        }
        // No real FEV1 file reaches this branch (verified across the corpus). A garbage
        // tag here means the reader desynced upstream (mis-sized some chunk); the hex
        // makes that diagnosable rather than emitting a silently-wrong file.
        throw FevError("FEV write: unknown/unrepresentable music-data chunk type (hex: " +
                       hex + ")");
    }
}

void write_music_data_chunks(BinaryWriter& w, const std::vector<FevMusicDataChunk>& chunks) {
    for (const auto& chunk : chunks) {
        write_music_data_chunk(w, chunk);
    }
}

void write_music_data_items(BinaryWriter& w, const std::vector<FevMusicDataItem>& items) {
    for (const auto& item : items) {
        // total_length includes the 4-byte length field itself, so reserve the field,
        // write the chunk body, then patch in the real length. Recomputing (rather than
        // replaying item.total_length) keeps the file self-consistent if the body is
        // edited; for an unmodified round-trip it reproduces the original value exactly.
        size_t length_pos = w.pos();
        w.write_u32(0); // placeholder
        write_music_data_chunks(w, item.chunks);
        uint32_t total_length = static_cast<uint32_t>(w.pos() - length_pos);
        w.patch_u32(length_pos, total_length);
    }
}

// Common tail shared by FEV1 and RIFF-LGCY: root category, event groups, sound
// definition configs/definitions, reverb definitions, music data. The only
// difference between the two forms is handled inside the element writers via
// ctx.riff (indexed names, position-only envelopes, EPRP collection).
void write_fev_body_tail(BinaryWriter& w, const FevFile& fev, FevWriteContext& ctx) {
    write_event_category(w, fev.root_category);

    w.write_u32(static_cast<uint32_t>(fev.event_groups.size()));
    for (const auto& grp : fev.event_groups) {
        write_event_group(w, grp, ctx);
    }

    w.write_u32(static_cast<uint32_t>(fev.sound_definition_configs.size()));
    for (const auto& cfg : fev.sound_definition_configs) {
        write_sound_definition_config(w, cfg, ctx);
    }

    w.write_u32(static_cast<uint32_t>(fev.sound_definitions.size()));
    for (const auto& sd : fev.sound_definitions) {
        write_sound_definition(w, sd, ctx);
    }

    w.write_u32(static_cast<uint32_t>(fev.reverb_definitions.size()));
    for (const auto& rv : fev.reverb_definitions) {
        write_reverb_definition(w, rv);
    }

    write_music_data_items(w, fev.music_data.items);
}

// ---------------------------------------------------------------------------
// FEV1 (flat) writer
// ---------------------------------------------------------------------------

std::vector<uint8_t> fev_write_fev1(const FevFile& fev) {
    BinaryWriter w;
    FevWriteContext ctx; // riff = false

    w.write_u32(FEV1_MAGIC);
    w.write_u32(fev.version);
    w.write_u32(fev.sound_def_names_pool_size);
    w.write_u32(fev.waveform_names_pool_size);

    // Manifest
    w.write_u32(static_cast<uint32_t>(fev.manifest.size()));
    for (const auto& entry : fev.manifest) {
        w.write_u32(static_cast<uint32_t>(entry.type));
        w.write_u32(entry.value);
    }

    // Project name
    write_fev_string(w, fev.project_name);

    // Banks
    w.write_u32(static_cast<uint32_t>(fev.banks.size()));
    for (const auto& bank : fev.banks) {
        w.write_u32(static_cast<uint32_t>(bank.load_mode));
        w.write_s32(bank.max_streams);
        w.write_u32(bank.fsb_checksum[0]);
        w.write_u32(bank.fsb_checksum[1]);
        write_fev_string(w, bank.name);
    }

    write_fev_body_tail(w, fev, ctx);
    return std::move(w.data());
}

// ---------------------------------------------------------------------------
// RIFF (FMOD Designer 4.45) writer
// ---------------------------------------------------------------------------

// Emit a RIFF/LIST chunk header word-aligning the payload: id(4), size(4),
// then the caller's payload; a trailing pad byte is added for odd sizes. Returns
// nothing — chunks are appended in-place.
void write_riff_chunk(BinaryWriter& w, const std::string& id,
                      const std::vector<uint8_t>& payload) {
    w.write_bytes(reinterpret_cast<const uint8_t*>(id.data()), 4);
    w.write_u32(static_cast<uint32_t>(payload.size()));
    w.write_bytes(payload.data(), payload.size());
    if (payload.size() % 2) w.write_u8(0); // word alignment
}

// Build the LGCY chunk payload from the model (RIFF mode). Also fills ctx.eprp
// with every envelope point in traversal order for the EPRP chunk.
std::vector<uint8_t> build_lgcy_payload(const FevFile& fev, FevWriteContext& ctx) {
    BinaryWriter w;

    // Pool sizes (LGCY has no FEV1 magic/version/manifest — those live in FMT and
    // OBCT — but keeps the two pool-size words).
    w.write_u32(fev.sound_def_names_pool_size);
    w.write_u32(fev.waveform_names_pool_size);

    // LGCY repeats the project name inline (mirrors the FEV1 layout); PROP holds
    // the authoritative copy but the inline one is part of the byte stream.
    write_fev_string(w, fev.project_name);

    // Banks with per-language checksum arrays.
    uint32_t lang_count = fev.riff.lang_count;
    w.write_u32(static_cast<uint32_t>(fev.banks.size()));
    if (!fev.banks.empty()) {
        // lang_count word is only present when there is at least one bank
        // (matches the reader: it is read only if bank_count > 0).
        w.write_u32(lang_count);
    }
    for (const auto& bank : fev.banks) {
        w.write_u32(static_cast<uint32_t>(bank.load_mode));
        w.write_s32(bank.max_streams);
        for (uint32_t lc = 0; lc < lang_count; ++lc) {
            if (lc < bank.lang_checksums.size()) {
                const auto& ck = bank.lang_checksums[lc];
                w.write_u32(ck.ck0);
                w.write_u32(ck.ck1);
                w.write_u32(ck.unk);
            } else {
                // Defensive: a hand-built bank without a full array falls back to
                // fsb_checksum for language 0 and zeros elsewhere.
                w.write_u32(lc == 0 ? bank.fsb_checksum[0] : 0);
                w.write_u32(lc == 0 ? bank.fsb_checksum[1] : 0);
                w.write_u32(0);
            }
        }
        write_fev_string(w, bank.name);
    }

    write_fev_body_tail(w, fev, ctx);
    return std::move(w.data());
}

// Build the EPRP chunk payload: count(u32) + count × (position, value, curve).
std::vector<uint8_t> build_eprp_payload(const FevWriteContext& ctx) {
    BinaryWriter w;
    w.write_u32(static_cast<uint32_t>(ctx.eprp.size()));
    for (const FevEffectEnvelopePoint* pt : ctx.eprp) {
        w.write_u32(pt->position);
        w.write_f32(pt->value);
        w.write_u32(static_cast<uint32_t>(pt->curve_shape));
    }
    return std::move(w.data());
}

// Build the OBCT chunk payload: manifest count + (type,value) pairs.
std::vector<uint8_t> build_obct_payload(const FevFile& fev) {
    BinaryWriter w;
    w.write_u32(static_cast<uint32_t>(fev.manifest.size()));
    for (const auto& entry : fev.manifest) {
        w.write_u32(static_cast<uint32_t>(entry.type));
        w.write_u32(entry.value);
    }
    return std::move(w.data());
}

// Build the PROP chunk payload: the project name as a u32-prefixed string.
std::vector<uint8_t> build_prop_payload(const FevFile& fev) {
    BinaryWriter w;
    write_fev_string(w, fev.project_name);
    return std::move(w.data());
}

// Build the STRR chunk payload verbatim from preserved data: count(u32) +
// offsets[count](u32) + pool bytes. Reproducing the exact table keeps every u32
// index used in LGCY pointing at the same slot it came from.
std::vector<uint8_t> build_strr_payload(const FevFile& fev) {
    BinaryWriter w;
    w.write_u32(static_cast<uint32_t>(fev.riff.strr_offsets.size()));
    for (uint32_t off : fev.riff.strr_offsets) w.write_u32(off);
    w.write_bytes(fev.riff.strr_pool.data(), fev.riff.strr_pool.size());
    return std::move(w.data());
}

std::vector<uint8_t> fev_write_riff(const FevFile& fev) {
    FevWriteContext ctx;
    ctx.riff = true;

    // LGCY and EPRP: emit the preserved raw bytes when the file came from a real
    // RIFF parse (guarantees byte-perfect round-trip through any version-gated
    // field). Fall back to re-serializing from the model for hand-built FevFiles
    // (unit tests) that carry no preserved bytes. build_lgcy_payload also
    // populates ctx.eprp, so call it unconditionally to keep the fallback EPRP
    // consistent even though its output may be discarded.
    std::vector<uint8_t> lgcy_model = build_lgcy_payload(fev, ctx);
    std::vector<uint8_t> eprp_model = build_eprp_payload(ctx);
    std::vector<uint8_t> lgcy = fev.riff.lgcy_bytes.empty() ? std::move(lgcy_model)
                                                            : fev.riff.lgcy_bytes;
    std::vector<uint8_t> eprp = fev.riff.eprp_bytes.empty() ? std::move(eprp_model)
                                                            : fev.riff.eprp_bytes;
    std::vector<uint8_t> obct = build_obct_payload(fev);
    std::vector<uint8_t> prop = build_prop_payload(fev);
    std::vector<uint8_t> strr = build_strr_payload(fev);

    // Assemble the LIST "PROJ" body in the recorded chunk order.
    BinaryWriter proj;
    proj.write_bytes(reinterpret_cast<const uint8_t*>("PROJ"), 4);
    for (const std::string& id : fev.riff.chunk_order) {
        if (id == "OBCT") write_riff_chunk(proj, id, obct);
        else if (id == "PROP") write_riff_chunk(proj, id, prop);
        else if (id == "LGCY") write_riff_chunk(proj, id, lgcy);
        else if (id == "EPRP") write_riff_chunk(proj, id, eprp);
        else if (id == "STRR") write_riff_chunk(proj, id, strr);
        else if (id == "LANG") write_riff_chunk(proj, id, fev.riff.lang_bytes);
        else {
            // Preserved unknown chunk.
            for (const auto& xc : fev.riff.extra_chunks) {
                if (xc.id == id) { write_riff_chunk(proj, id, xc.payload); break; }
            }
        }
    }

    // Wrap PROJ in a LIST chunk, then FMT + LIST in the top-level RIFF "FEV ".
    BinaryWriter riff_body;
    write_riff_chunk(riff_body, "FMT ", fev.riff.fmt_bytes);
    write_riff_chunk(riff_body, "LIST", proj.data());

    BinaryWriter out;
    out.write_bytes(reinterpret_cast<const uint8_t*>("RIFF"), 4);
    out.write_u32(static_cast<uint32_t>(riff_body.data().size() + 4)); // + "FEV " form
    out.write_bytes(reinterpret_cast<const uint8_t*>("FEV "), 4);
    out.write_bytes(riff_body.data().data(), riff_body.data().size());
    return std::move(out.data());
}

} // namespace

std::vector<uint8_t> fev_write(const FevFile& fev) {
    return fev.is_riff ? fev_write_riff(fev) : fev_write_fev1(fev);
}

} // namespace lu::assets
