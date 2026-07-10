#pragma once
// nif_geometry.h — Extract world-space triangle meshes from parsed NIF data.
//
// Shared between NIF viewer (for rendering) and navmesh tool (for collision fallback).

#include "gamebryo/nif/nif_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lu::assets {

struct NifExtractedMesh {
    std::vector<float> vertices;  // xyz interleaved, world space
    std::vector<float> normals;   // xyz interleaved
    std::vector<float> uvs;       // uv interleaved, first UV set only (NifVertex::u/v)
    std::vector<uint32_t> indices;
    std::string name;
    uint32_t block_index = 0;     // source NifMesh::block_index, for looking up the owning
                                   // NifNode (data_ref) e.g. to resolve its texturing property.
};

struct NifExtractionResult {
    std::vector<NifExtractedMesh> meshes;
    uint32_t total_vertices = 0;
    uint32_t total_triangles = 0;
};

struct NifRenderVertex {
    float position[3] = {0, 0, 0};
    float normal[3] = {0, 1, 0};
    float uv[2] = {0, 0};
    // Second authored UV channel when present; falls back to uv for consumers
    // that need stable texture coordinate data.
    float uv2[2] = {0, 0};
    float color[4] = {1, 1, 1, 1};
};

// One authored NiSkinData influence for a render vertex. This intentionally
// preserves bone indices relative to the mesh's NifSkinInstance bone list;
// renderers decide how many weights to upload or normalize.
struct NifRenderSkinInfluence {
    uint16_t bone_index = 0;
    float weight = 0.0f;
};

struct NifRenderPropertySource {
    bool present = false;
    uint32_t property_block = 0;
    uint32_t owner_node_block = 0;
    uint32_t inheritance_depth = 0; // 0 = geometry node, 1 = parent, ...
    uint32_t duplicates_on_owner = 0;
};

struct NifRenderPropertySources {
    NifRenderPropertySource material;
    NifRenderPropertySource texturing;
    NifRenderPropertySource alpha;
    NifRenderPropertySource vertex_color;
    NifRenderPropertySource z_buffer;
    NifRenderPropertySource specular;
    NifRenderPropertySource shade;
    NifRenderPropertySource stencil;
};

// Nearest-property-by-type candidate state. This is diagnostic/parser data;
// consumers explicitly decide when to promote it to effective GPU state.
struct NifRenderAuthoredState {
    NifRenderPropertySources sources;
    bool has_alpha = false;
    uint16_t alpha_flags = 0;
    uint8_t alpha_threshold = 0;
    bool has_vertex_color = false;
    uint16_t vertex_color_flags = 0;
    bool has_z_buffer = false;
    uint16_t z_buffer_flags = 3;
    bool has_specular = false;
    uint16_t specular_flags = 0;
    bool has_shade = false;
    uint16_t shade_flags = 1;
    bool has_stencil = false;
    uint16_t stencil_flags = 0;
    uint32_t stencil_ref = 0;
    uint32_t stencil_mask = 0xFFFFFFFFu;
    bool has_sort_adjust = false;
    uint32_t sort_adjust_node_block = 0;
    uint32_t sort_adjust_inheritance_depth = 0;
    uint32_t sorting_mode = 0;
};

struct NifRenderNodeSource {
    uint32_t block_index = 0;
    std::string name;
    std::string type_name;
};

struct NifRenderMaterial {
    std::string name;
    float ambient[3] = {0.25f, 0.25f, 0.25f};
    float diffuse[4] = {0.7f, 0.7f, 0.7f, 1.0f};
    float specular[4] = {0.0f, 0.0f, 0.0f, 10.0f};
    float emissive[3] = {0.0f, 0.0f, 0.0f};
    std::string diffuse_texture;
    bool diffuse_texture_has_alpha_format = false;
    uint32_t diffuse_texture_alpha_format = 0;
    bool diffuse_texture_has_clamp_mode = false;
    uint8_t diffuse_texture_clamp_mode = 3;
    NifTextureTransform diffuse_texture_transform;
    // Additional NiTexturingProperty texture slots. These are authored asset
    // references only; shader-specific meaning remains the consumer's job.
    std::string dark_texture;
    bool dark_texture_has_clamp_mode = false;
    uint8_t dark_texture_clamp_mode = 3;
    NifTextureTransform dark_texture_transform;
    std::string detail_texture;
    bool detail_texture_has_clamp_mode = false;
    uint8_t detail_texture_clamp_mode = 3;
    NifTextureTransform detail_texture_transform;
    std::string gloss_texture;
    bool gloss_texture_has_clamp_mode = false;
    uint8_t gloss_texture_clamp_mode = 3;
    NifTextureTransform gloss_texture_transform;
    std::string glow_texture;
    bool glow_texture_has_clamp_mode = false;
    uint8_t glow_texture_clamp_mode = 3;
    NifTextureTransform glow_texture_transform;
    // Raw NiAlphaProperty state. Consumers decide how these bits map to a
    // render API's blend/test state; this parser only preserves authored data.
    bool has_alpha_property = false;
    uint16_t alpha_flags = 0;
    uint8_t alpha_threshold = 0;
    // Direct properties preserve the legacy extraction behavior above. The
    // resolved candidate includes inherited properties and their provenance.
    NifRenderPropertySources direct_property_sources;
    NifRenderAuthoredState resolved_state;
};

struct NifRenderMesh {
    std::vector<NifRenderVertex> vertices;
    std::vector<uint32_t> indices;
    NifRenderMaterial material;
    std::string name;
    uint32_t source_mesh_block = 0;
    uint32_t source_node_block = 0;
    // Geometry node first, followed by its ancestors toward the root.
    std::vector<NifRenderNodeSource> source_node_chain;
    bool parent_cycle_detected = false;
    bool multiple_parents_detected = false;
    // True when the source NiTriShapeData/NiTriStripsData contained authored
    // vertex colors, independent of whether a renderer chooses to use them.
    bool has_vertex_colors = false;
    // LOD metadata is inherited from the closest NiLODNode parent and carries
    // the file-authored distance band without making visibility decisions here.
    bool has_lod_range = false;
    uint32_t lod_parent_block = 0;
    uint32_t lod_level = 0;
    float lod_near = 0.0f;
    float lod_far = 0.0f;
    float lod_center[3] = {0.0f, 0.0f, 0.0f};
    // Skinning metadata is copied from NiSkinInstance/NiSkinData so animation
    // importers can bind meshes to skeletons without reparsing raw NIF blocks.
    bool is_skinned = false;
    uint32_t skin_instance_block = 0;
    int32_t skeleton_root_block = -1;
    std::vector<int32_t> skin_bone_node_blocks;
    std::vector<std::string> skin_bone_names;
    std::vector<std::vector<NifRenderSkinInfluence>> vertex_influences;
};

struct NifRenderExtractionResult {
    std::vector<NifRenderMesh> meshes;
    uint32_t total_vertices = 0;
    uint32_t total_triangles = 0;
};

// Extract all meshes from a parsed NIF file.
// pos/scale apply a world transform (for navmesh object placement).
NifExtractionResult extractNifGeometry(const NifFile& nif,
                                        float pos_x = 0, float pos_y = 0, float pos_z = 0,
                                        float scale = 1.0f);

// Extract a strict static render view of the scene graph: only geometry reached
// through NiTriShape/NiTriStrips nodes is returned, with parent transforms and
// property refs resolved. Orphan NiTriShapeData/NiTriStripsData blocks are
// intentionally ignored so parser bugs are not hidden as plausible render data.
NifRenderExtractionResult extractNifRenderGeometry(const NifFile& nif);

} // namespace lu::assets
