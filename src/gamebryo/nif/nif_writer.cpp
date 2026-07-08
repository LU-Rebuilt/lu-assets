#include "gamebryo/nif/nif_writer.h"
#include "common/binary_writer/binary_writer.h"

namespace lu::assets {

std::vector<uint8_t> nif_write(const NifFile& nif) {
    if (nif.header_line.empty() || nif.header_line.back() != '\n') {
        throw NifError("NIF write: header_line must be the original text line ending in \\n");
    }
    if (nif.version < 0x14020007) {
        throw NifError("NIF write: only versions with a block-size table (>= 20.2.0.7) are supported");
    }
    if (nif.block_data.size() != nif.block_type_indices.size()) {
        throw NifError("NIF write: block_data/block_type_indices size mismatch");
    }

    BinaryWriter w;

    // Text header line, preserved verbatim (exporter spelling/formatting varies).
    w.write_bytes(reinterpret_cast<const uint8_t*>(nif.header_line.data()),
                  nif.header_line.size());

    // Binary header — field order mirrors nif_parse.
    w.write_u32(nif.version);
    w.write_u8(nif.endian);
    w.write_u32(nif.user_version);
    w.write_u32(static_cast<uint32_t>(nif.block_data.size()));

    if (nif.user_version >= 10) {
        w.write_u32(nif.user_version_2);
    }
    if (nif.user_version >= 3) {
        for (const std::string& s : nif.export_info) {
            w.write_string8(s);
        }
    }

    // Block type table
    w.write_u16(static_cast<uint16_t>(nif.block_types.size()));
    for (const std::string& name : nif.block_types) {
        w.write_string32(name);
    }
    for (uint16_t idx : nif.block_type_indices) {
        w.write_u16(idx);
    }

    // Block sizes — recomputed from the raw block bytes so edits stay consistent.
    for (const auto& block : nif.block_data) {
        w.write_u32(static_cast<uint32_t>(block.size()));
    }

    // String table
    w.write_u32(static_cast<uint32_t>(nif.string_table.size()));
    w.write_u32(nif.string_table_max_len);
    for (const std::string& s : nif.string_table) {
        w.write_string32(s);
    }

    // Groups
    w.write_u32(static_cast<uint32_t>(nif.groups.size()));
    for (uint32_t g : nif.groups) {
        w.write_u32(g);
    }

    // Blocks
    for (const auto& block : nif.block_data) {
        w.write_bytes(block.data(), block.size());
    }

    // Footer
    w.write_u32(static_cast<uint32_t>(nif.roots.size()));
    for (int32_t root : nif.roots) {
        w.write_s32(root);
    }

    return std::move(w.data());
}

} // namespace lu::assets
