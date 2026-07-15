#include "havok/packfile/hkx_packfile_writer.h"
#include "common/binary_writer/binary_writer.h"

#include <string>

namespace lu::assets {

namespace {

// Writes a fixed-size, NUL-padded string field with the trailing-0xFF sentinel byte
// that every real HkxPackfileHeader::contentsVersion and HkxSectionHeader::sectionTag
// field carries as the field's very last byte (see hkx_packfile_types.h for the corpus
// evidence). The string plus its NUL terminator must fit within field_size - 1 bytes,
// leaving at least the sentinel byte; real fields are always well under this (14 of 16,
// 14-ish of 20 bytes used).
void write_fixed_str_havok_padded(BinaryWriter& w, const std::string& s, size_t field_size) {
    if (s.size() + 1 >= field_size) {
        throw HkxPackfileError("HKX packfile write: fixed string '" + s +
                                "' too long for " + std::to_string(field_size) + "-byte field");
    }
    w.write_bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    w.write_zeros(field_size - s.size() - 1); // NUL + zero padding up to the sentinel byte
    w.write_u8(0xFF); // sentinel byte, constant across the entire real corpus
}

} // namespace

std::vector<uint8_t> hkx_packfile_write(const HkxPackfile& file) {
    if (file.sections.size() != file.section_data.size()) {
        throw HkxPackfileError("HKX packfile write: sections/section_data size mismatch");
    }

    BinaryWriter w;
    const HkxPackfileHeader& h = file.header;

    // ---- 64-byte header ----
    w.write_u32(h.magic0);
    w.write_u32(h.magic1);
    w.write_u32(h.userTag);
    w.write_u32(h.fileVersion);
    w.write_u8(h.pointerSize);
    w.write_u8(h.littleEndian);
    w.write_u8(h.reusePaddingOptimization);
    w.write_u8(h.emptyBaseClassOptimization);
    w.write_u32(static_cast<uint32_t>(file.sections.size()));
    w.write_u32(h.contentsSectionIndex);
    w.write_u32(h.contentsSectionOffset);
    w.write_u32(h.contentsClassNameSectionIndex);
    w.write_u32(h.contentsClassNameSectionOffset);
    write_fixed_str_havok_padded(w, h.contentsVersion, 16);
    w.write_u32(h.flags);
    w.write_s16(h.maxPredicate);
    w.write_s16(h.predicateArraySizePlusPadding);

    // ---- Section header table ----
    // Offsets are recomputed from the actual sub-region blob sizes (rather than replayed
    // from HkxSectionHeader verbatim) so that a consumer which edits section_data (e.g.
    // to patch a fixup) produces a self-consistent file, mirroring how nif_writer.cpp
    // recomputes NIF block_sizes from block_data rather than trusting stale sizes. For an
    // unmodified round-trip this yields byte-identical offsets to the original, since the
    // parser sliced those same blobs using these same offset fields.
    //
    // absoluteDataStart is recomputed too: the real corpus shows sections are always
    // perfectly contiguous (section[i] ends exactly where section[i+1] begins, with the
    // first section starting right after the section header table), so it is derived
    // from the running byte position rather than replayed, keeping the file consistent if
    // a section's total size changes.
    size_t section_table_pos = w.pos();
    // Reserve space for the section header table; offsets get patched in after sizes are known.
    for (size_t i = 0; i < file.sections.size(); ++i) {
        write_fixed_str_havok_padded(w, file.sections[i].sectionTag, 20);
        w.write_zeros(7 * 4); // absoluteDataStart..endOffset, patched below
    }

    std::vector<uint32_t> absoluteDataStarts(file.sections.size());
    uint32_t running_offset = static_cast<uint32_t>(w.pos());
    for (size_t i = 0; i < file.section_data.size(); ++i) {
        const HkxSectionData& sd = file.section_data[i];
        absoluteDataStarts[i] = running_offset;

        w.write_bytes(sd.data.data(), sd.data.size());
        w.write_bytes(sd.localFixups.data(), sd.localFixups.size());
        w.write_bytes(sd.globalFixups.data(), sd.globalFixups.size());
        w.write_bytes(sd.virtualFixups.data(), sd.virtualFixups.size());
        w.write_bytes(sd.exports.data(), sd.exports.size());
        w.write_bytes(sd.imports.data(), sd.imports.size());

        running_offset += static_cast<uint32_t>(sd.total_size());
    }

    // Patch the section header table's offset fields now that sizes are known.
    for (size_t i = 0; i < file.sections.size(); ++i) {
        const HkxSectionData& sd = file.section_data[i];
        uint32_t localFixupsOffset   = static_cast<uint32_t>(sd.data.size());
        uint32_t globalFixupsOffset  = localFixupsOffset + static_cast<uint32_t>(sd.localFixups.size());
        uint32_t virtualFixupsOffset = globalFixupsOffset + static_cast<uint32_t>(sd.globalFixups.size());
        uint32_t exportsOffset       = virtualFixupsOffset + static_cast<uint32_t>(sd.virtualFixups.size());
        uint32_t importsOffset       = exportsOffset + static_cast<uint32_t>(sd.exports.size());
        uint32_t endOffset          = importsOffset + static_cast<uint32_t>(sd.imports.size());

        size_t entry_pos = section_table_pos + i * 48 + 20; // past the 20-byte sectionTag
        w.patch_u32(entry_pos + 0,  absoluteDataStarts[i]);
        w.patch_u32(entry_pos + 4,  localFixupsOffset);
        w.patch_u32(entry_pos + 8,  globalFixupsOffset);
        w.patch_u32(entry_pos + 12, virtualFixupsOffset);
        w.patch_u32(entry_pos + 16, exportsOffset);
        w.patch_u32(entry_pos + 20, importsOffset);
        w.patch_u32(entry_pos + 24, endOffset);
    }

    return std::move(w.data());
}

} // namespace lu::assets
