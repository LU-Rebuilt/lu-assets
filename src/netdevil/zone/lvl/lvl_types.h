#pragma once

#include "netdevil/common/ldf/ldf_types.h"
#include "netdevil/zone/luz/luz_types.h"  // Vec3, Quat

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <stdexcept>

namespace lu::assets {

// LVL (Scene/Level) file parser — complete format per lu_formats/files/lvl.ksy.
//
// Chunked binary format. Each chunk begins with the 20-byte CHNK header:
//   magic "CHNK" (4) | chunk_id u32 (4) | header_version u16 (2) |
//   data_version u16 (2) | total_size u32 (4) | data_offset u32 (4)
//
// total_size is the FULL chunk size including the 20-byte header.
// data_offset is the absolute file offset where the chunk payload begins.
// Chunk boundary: next chunk starts at chunk_pos + total_size.
//
// Four chunk IDs (from lu_formats/files/lvl.ksy enums):
//   1000 (fib)         — file info block: version, revision, chunk offsets
//   2000 (environment) — lighting, skydome NIF paths, editor color palette
//   2001 (object)      — scene objects (LOT, position, rotation, LDF config)
//   2002 (particle)    — particle system placements (PSB paths, LDF config)
//
// RE sources (legouniverse.exe):
//   ReadLvlObjectData @ 0103ba20
//   ReadSceneAndLayer @ 004b6d00
//
// References:
//   - lcdr/lu_formats lvl.ksy (github.com/lcdr/lu_formats) — authoritative format spec
//   - DarkflameServer (github.com/DarkflameUniverse/DarkflameServer) — Zone.cpp LoadLevel()

struct LvlError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr uint32_t LVL_CHUNK_MAGIC = 0x4B4E4843; // "CHNK"

// ── Environment (chunk 2000) ──────────────────────────────────────────────────

// Per-entry in the cull data table (version >= 40).
// Controls distance-based culling per object group.
struct LvlCullVal {
    uint32_t group_id = 0;
    float    min      = 0.0f;
    float    max      = 0.0f;
};

// Scene draw-distance settings (min and max table entries, version >= 39).
struct LvlDrawDistances {
    float fog_near            = 0.0f;
    float fog_far             = 0.0f;
    float post_fog_solid      = 0.0f;
    float post_fog_fade       = 0.0f;
    float static_obj_distance = 0.0f;
    float dynamic_obj_distance= 0.0f;
};

// Scene lighting parameters.
// Verified from lu_formats/files/lvl.ksy lighting_info type.
struct LvlLightingInfo {
    // version >= 45
    float blend_time = 0.0f;        // Transition blend time for lighting changes

    // Always present
    float ambient[3]    = {};       // Ambient light color (RGB)
    float specular[3]   = {};       // Specular light color (RGB)
    float upper_hemi[3] = {};       // Upper hemisphere light color (RGB)
    Vec3  position;                 // Main light source position (absent in v35/36 files)
    // v35/36 only: the light is stored as two direction angles instead of a position
    // (observed in early-build LUP maps; every other version has the vec3).
    bool  has_light_angles = false;
    float light_angles[2] = {0.0f, 0.0f};

    // version >= 39
    bool            has_draw_distances = false;
    LvlDrawDistances min_draw;      // Minimum draw-distance settings
    LvlDrawDistances max_draw;      // Maximum draw-distance settings

    // version >= 40
    std::vector<LvlCullVal> cull_vals;

    // version 31–38 only (replaced by draw distances in v39)
    float fog_near = 0.0f;
    float fog_far  = 0.0f;

    // version >= 31
    float fog_color[3] = {};        // Fog color (RGB)

    // version >= 36
    float dir_light[3] = {};        // Directional light color (RGB)

    // version < 42 only
    Vec3  start_position;           // Player spawn position (moved to LUZ in v42+)
    Quat  start_rotation;           // Player spawn rotation (version 33–41)
    bool  has_spawn = false;
};

// Skydome NIF model filenames.
// Verified from lu_formats/files/lvl.ksy skydome_info type.
struct LvlSkydomeInfo {
    std::string filename;           // Main skydome NIF path (u4_str)
    // version >= 34 (all live client files are version 38+, so always present)
    std::string sky_layer_filename; // Additional sky layer NIF
    std::string ring_layer[4];      // Ring/cloud layer NIFs (0-3)
};

// Editor color palette (version >= 37).
struct LvlEditorColor { float r, g, b; };
struct LvlEditorSettings {
    std::vector<LvlEditorColor> saved_colors;
};

// Top-level environment data (chunk 2000).
struct LvlEnvironmentData {
    LvlLightingInfo  lighting;
    LvlSkydomeInfo   skydome;
    LvlEditorSettings editor;
    bool             has_editor = false;  // version >= 37
};

// ── Object (chunk 2001) ───────────────────────────────────────────────────────

// A single render attribute on an object's render technique.
// Each attr has a 64-byte name, a float count (always 4), an is_color flag,
// and 4 float values. Verified from lu_formats/files/lvl.ksy render_attr type.
struct LvlRenderAttr {
    std::string name;           // Shader parameter name (64-byte null-terminated ASCII)
    uint32_t    num_floats = 0; // Usually 4 — stored verbatim from file
    bool        is_color   = false;
    float       values[4]  = {};
};

// Render technique block attached to objects (version >= 7).
// Verified from lu_formats/files/lvl.ksy render_technique type.
struct LvlRenderTechnique {
    std::string              name;  // Technique name (64-byte ASCII; only valid when attrs non-empty)
    std::vector<LvlRenderAttr> attrs;
};

// Node type enum values from lu_formats/files/lvl.ksy node_type enum.
enum class LvlNodeType : uint32_t {
    EnvironmentObj    = 0,
    Building          = 1,
    Enemy             = 2,
    NPC               = 3,
    Rebuilder         = 4,
    Spawned           = 5,
    Cannon            = 6,
    Bouncer           = 7,
    Exhibit           = 8,
    MovingPlatform    = 9,
    Springpad         = 10,
    Sound             = 11,
    Particle          = 12,
    GenericPlaceholder= 13,
    ErrorMarker       = 14,
    PlayerStart       = 15,
};

struct LvlObject {
    uint64_t    object_id  = 0;
    uint32_t    lot        = 0;             // LEGO Object Template ID
    LvlNodeType node_type  = LvlNodeType::Building; // version >= 38 only
    uint32_t    glom_id    = 0;             // Agglomeration group ID (version >= 32)
    Vec3        position;
    Quat        rotation;                   // Stored as XYZW; file is WXYZ, converted
    float       scale      = 1.0f;

    // Config string: u32 char_count, then char_count × UTF-16LE chars.
    // Decoded to ASCII and parsed as text LDF (key=type:value\n pairs).
    std::vector<LdfEntry> config;           // Parsed LDF key-value config

    // Render technique (version >= 7, only when num_render_attrs > 0)
    LvlRenderTechnique render_technique;
};

// ── Particles (chunk 2002) ────────────────────────────────────────────────────

// A particle system placement in the scene.
// effect_names is a semicolon-separated list of PSB effect paths (u4_wstr).
// config is an optional LDF config string (u4_wstr).
// Verified from lu_formats/files/lvl.ksy particle type.
struct LvlParticle {
    uint16_t    priority     = 0;   // Spawn priority (version >= 43)
    Vec3        position;
    Quat        rotation;           // File is WXYZ; stored as XYZW
    std::string effect_names;       // ASCII-decoded PSB path(s), semicolon-separated
    std::vector<LdfEntry> config;   // Parsed LDF config (may be empty)
};

// ── Top-level file ─────────────────────────────────────────────────────────────

// Old (pre-CHNK) files only: axis-less environment volume. The chunked format dropped
// this section entirely; nothing equivalent exists in CHNK files. Two on-disk shapes,
// keyed by the same has_revision_field boundary as the file header (see LvlFile):
//   version >= 36 (retail): position + LDF config string (friction, interaction_distance,
//                  soundFalloffFar, ...) as u32 char_count + UTF-16LE, same encoding as
//                  object configs.
//   version < 36 (leaked pre-release, 1 attested sample with non-empty blocks): position
//                  + a fixed 260-WCHAR (520-byte) null-padded ambient-sound-name buffer —
//                  no length prefix, no LDF key=type:value encoding, just a bare name
//                  (e.g. "amb_birds_heavy_piratemap03"). 260 matches Win32 MAX_PATH,
//                  suggesting this predates the switch to a generic LDF config string.
struct LvlEnvBlock {
    Vec3 position;
    std::vector<LdfEntry> config;       // version >= 36 only
    std::string old_name;               // version < 36 only (fixed 260-WCHAR buffer)
};

struct LvlFile {
    uint32_t version  = 0;          // From fib chunk (chunk 1000)
    uint32_t revision = 0;          // Build revision from fib chunk

    // Old pre-chunked container (no CHNK framing; shipped alongside chunked files even
    // in 1.10.64). Layout: u16 version ×2, then lighting/skydome/editor(v>=37)/objects/
    // env-blocks/particles laid out sequentially. Every RETAIL sample (versions 36-43,
    // 616 files) has a u8 + u32 revision between the version pair and the lighting data.
    // A small corpus of pre-release LEAKED files (versions 31-34, absent from every
    // retail client tree) has a shorter header with no discoverable format-version
    // pattern in any Ghidra-reachable binary (this predates any client build we have
    // Ghidra RE access to) — confirmed empirically byte-for-byte, by parsing the fields
    // that follow (ambient/specular/fog colors, world-space positions) and checking which
    // header size produces sane values instead of denormalized-float garbage:
    //   version == 31:        no old_unknown_byte, no revision (0 extra bytes)
    //   version 32-35:        old_unknown_byte present (always 0 in every sample), but
    //                         no revision field (1 extra byte, not 5) — only 32/33/34
    //                         are attested in the corpus, no version-35 sample exists
    //   version >= 36:        both fields present (5 extra bytes) — matches retail;
    //                         every leaked sample at 36-40 also round-trips under this
    //                         rule, so the boundary is exactly the retail/leak split
    bool old_format = false;
    uint8_t old_unknown_byte = 0;   // header byte after the version pair; 0 in all files
    bool has_old_unknown_byte = true;  // false only for the leaked version==31 samples
    bool has_revision_field = true;    // false for leaked versions 31-33 (see above)
    std::vector<LvlEnvBlock> old_env_blocks;

    LvlEnvironmentData environment;  // From chunk 2000 (may be empty/default)
    bool               has_environment = false;
    uint16_t           env_data_version = 1;    // CHNK data_version for chunk 2000

    std::vector<LvlObject>   objects;    // From chunk 2001
    bool                     has_objects = false;
    std::vector<LvlParticle> particles;  // From chunk 2002
    bool                     has_particles = false;
};
} // namespace lu::assets
