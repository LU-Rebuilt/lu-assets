#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace lu::assets {

// ============================================================================
// HKX binary packfile container — raw-block-preservation model.
// ============================================================================
//
// This module round-trips the Havok "binary packfile" container format
// (magic 0x57E0E057 0x10C0C010) byte-for-byte, following the same design as
// gamebryo/nif: preserve the container-level structure (header fields,
// section header table, per-section raw bytes) verbatim, and do NOT parse
// individual Havok class fields (hkpRigidBody internals, shape internals,
// fixup *entries*, etc). That is the job of the separate, pre-existing
// lossy `Hkx::` reader in havok/reader/ (not touched by this module).
//
// Corpus survey (see src/havok/packfile/README.md for full methodology and
// tables) of 3372 real packfile-format .hkx files shipped by the LEGO
// Universe client confirms:
//   - pointerSize is always 4 (32-bit) — no 64-bit-pointer packfiles ship.
//   - littleEndian is always 1.
//   - numSections is always 3, with tags "__classnames__", "__types__",
//     "__data__" (order varies: classnames always first, but data/types swap).
//   - Sections are perfectly contiguous: section[i].absoluteDataStart +
//     section[i].endOffset == section[i+1].absoluteDataStart, with the very
//     first section starting immediately after the section header table
//     (offset 64 + numSections*48) and the last section's end == file size.
//     There is no footer and no inter-section padding.
//   - fileVersion (packfile version) values actually present: 4, 5, 6, 7,
//     matching contentsVersion strings "Havok-4.5.0-r1", "Havok-5.1.0-r1",
//     "Havok-6.5.0-r1", "Havok-7.0.0-r1"/"Havok-7.1.0-r1" (version 7 covers
//     both 7.0 and 7.1 — the packfile version number doesn't distinguish
//     them, only contentsVersion does).
//
// A section's byte range [localFixupsOffset, endOffset) — i.e. everything
// after the raw data payload — was investigated in depth to see whether the
// local/global/virtual fixup tables need structured (count, entry[]) parsing
// to round-trip. They do NOT: local fixup entries are a clean multiple of 8
// bytes (uint32 srcOffset, uint32 dstOffset pairs) with no terminator, but
// global/virtual fixup entries are 12 bytes each (uint32, uint32, uint32)
// PLUS a variable-length trailing run of 0xFFFFFFFF bytes (0, 4, or 8 bytes)
// that isn't a fixed-size sentinel entry — it's simply how much of a final
// 0xFFFFFFFF-filled "grown-then-trimmed" entry survived serialization. Since
// the region's total byte length is already fully determined by the section
// header's own offset fields (this offset to the next offset), and the
// content is meaningless to us at the container level, every sub-region
// (local fixups, global fixups, virtual fixups, exports, imports, and the
// data payload itself) is preserved as one opaque raw-byte blob per section
// rather than parsed into entry lists. This sidesteps the terminator-length
// quirk entirely: the exact bytes are captured and replayed, whatever they
// are.
// ============================================================================

struct HkxPackfileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// 64-byte packfile header. Field order/sizes verified via a raw memcpy-shaped
// read against many real files (see README.md); every field here is written
// back verbatim by hkx_packfile_write rather than recomputed, since fields
// like `flags`/`maxPredicate`/`predicateArraySizePlusPadding` are
// version-independent sentinels (-1 / 0xFFFFFFFF in every sampled file) that
// carry no information this module could derive if it started needing to.
struct HkxPackfileHeader {
    uint32_t magic0 = 0x57E0E057;
    uint32_t magic1 = 0x10C0C010;
    uint32_t userTag = 0;
    uint32_t fileVersion = 0;             // Packfile version: 4, 5, 6, or 7 in the real corpus.
    uint8_t pointerSize = 4;              // Always 4 (32-bit) in every shipped LU file.
    uint8_t littleEndian = 1;             // Always 1.
    uint8_t reusePaddingOptimization = 0;
    uint8_t emptyBaseClassOptimization = 1;
    uint32_t numSections = 0;             // Always 3 in the real corpus.
    uint32_t contentsSectionIndex = 0;    // Index of the section holding the root object (__data__).
    uint32_t contentsSectionOffset = 0;   // Always 0 in the real corpus.
    uint32_t contentsClassNameSectionIndex = 0;  // Index of the __classnames__ section.
    uint32_t contentsClassNameSectionOffset = 0; // Byte offset of the root class's name string.
    // Contents version string, e.g. "Havok-7.1.0-r1". Stored trimmed at the first NUL; the
    // writer re-pads to 16 bytes with zeros EXCEPT the very last byte, which is always
    // 0xFF, not 0x00 — verified across the full 3372-file real corpus (every single
    // sample has the pattern [string][0x00...][0xFF] as the final byte of the 16-byte
    // field, never all-zero padding). Likely a Havok debug-buffer sentinel byte baked in
    // by the original serializer, but whatever its origin, it's constant, so
    // hkx_packfile_write hardcodes it rather than modeling "the last padding byte" as a
    // general field (see write_fixed_str_havok_padded in hkx_packfile_writer.cpp).
    std::string contentsVersion;
    uint32_t flags = 0xFFFFFFFF;
    int16_t maxPredicate = -1;
    int16_t predicateArraySizePlusPadding = -1;
};

// 48-byte section header entry.
struct HkxSectionHeader {
    // Section tag, e.g. "__classnames__", "__types__", "__data__". Stored trimmed at the
    // first NUL; the writer re-pads to 20 bytes with zeros except the last byte, which is
    // always 0xFF in the real corpus (same sentinel-padding pattern as
    // HkxPackfileHeader::contentsVersion — see that field's comment).
    std::string sectionTag;
    uint32_t absoluteDataStart = 0;  // File-relative byte offset where this section's bytes begin.
    uint32_t localFixupsOffset = 0;  // Section-relative offset: end of data payload / start of local fixups.
    uint32_t globalFixupsOffset = 0; // Section-relative offset: end of local fixups / start of global fixups.
    uint32_t virtualFixupsOffset = 0;// Section-relative offset: end of global fixups / start of virtual fixups.
    uint32_t exportsOffset = 0;      // Section-relative offset: end of virtual fixups / start of exports.
    uint32_t importsOffset = 0;      // Section-relative offset: end of exports / start of imports.
    uint32_t endOffset = 0;          // Section-relative offset: end of imports / total section length.
};

// One section's raw bytes, split into sub-regions purely by the header's own offset
// fields (no entry-level interpretation — see the module-level comment above for why).
// Concatenating data + localFixups + globalFixups + virtualFixups + exports + imports
// reproduces the section's exact original byte range.
struct HkxSectionData {
    std::vector<uint8_t> data;           // [0, localFixupsOffset)
    std::vector<uint8_t> localFixups;    // [localFixupsOffset, globalFixupsOffset)
    std::vector<uint8_t> globalFixups;   // [globalFixupsOffset, virtualFixupsOffset)
    std::vector<uint8_t> virtualFixups;  // [virtualFixupsOffset, exportsOffset)
    std::vector<uint8_t> exports;        // [exportsOffset, importsOffset)
    std::vector<uint8_t> imports;        // [importsOffset, endOffset)

    size_t total_size() const {
        return data.size() + localFixups.size() + globalFixups.size() +
               virtualFixups.size() + exports.size() + imports.size();
    }
};

// A parsed HKX binary packfile: header + section header table + per-section raw bytes.
// The round-trip guarantee comes ONLY from replaying these fields in original order — see
// hkx_packfile_writer.cpp. There is no typed object model for Havok class contents; that
// stays a job for the pre-existing lossy Hkx:: reader (havok/reader/), which is unaffected
// by this module.
struct HkxPackfile {
    HkxPackfileHeader header;
    std::vector<HkxSectionHeader> sections;      // Section header table, in file order.
    std::vector<HkxSectionData> section_data;     // Per-section raw bytes, 1:1 with `sections`.
};

} // namespace lu::assets
