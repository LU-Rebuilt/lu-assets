#pragma once

#include <cstdint>
#include <stdexcept>

namespace lu::assets {

// References:
//   - lcdr/lu_formats pk.ksy, sd0.ksy (github.com/lcdr/lu_formats) — archive and compression specs

// PK (NDPK) archive format types.
//
// Binary format verified against legouniverse.exe (FUN_01037410):
// - 7-byte header: "ndpk" + 01 FF 00 (DAT_01514e48)
// - File data entries with 5-byte dividers (FF 00 00 DD 00) between them
// - TOC at end of file:
//     [u32 entry_count]
//     [entry_count * 100 bytes: PackIndexEntry array]
//     [u32 toc_offset]   (offset to entry_count)
//     [u32 file_revision]

struct PkError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

inline constexpr uint8_t PK_MAGIC[] = {0x6E, 0x64, 0x70, 0x6B, 0x01, 0xFF, 0x00};
inline constexpr size_t PK_HEADER_SIZE = 7;
inline constexpr size_t PK_ENTRY_SIZE = 100; // Confirmed from RE: local_670 * 100

// 5-byte divider between data entries (DAT_01514e50)
inline constexpr uint8_t PK_DATA_DIVIDER[] = {0xFF, 0x00, 0x00, 0xDD, 0x00};

// Pack index entry layout (100 bytes total, packed).
// Verified from DarkflameServer Pack.h PackRecord and Ghidra RE at FUN_01037410.
//   +0x00 (0):  u32 crc             - CRC32 hash of the file path
//   +0x04 (4):  s32 lower_crc       - Lower 32 bits of extended CRC (-1 if unused)
//   +0x08 (8):  s32 upper_crc       - Upper 32 bits of extended CRC (-1 if unused)
//   +0x0C (12): u32 uncompressed_size - Original file size in bytes
//   +0x10 (16): char[32] uncompressed_hash - MD5 hash of uncompressed data (hex ASCII)
//   +0x30 (48): u32 padding1        - Padding after uncompressed hash
//   +0x34 (52): u32 compressed_size  - Compressed data size (or 0/same if not compressed)
//   +0x38 (56): char[32] compressed_hash - MD5 hash of compressed data (hex ASCII)
//   +0x58 (88): u32 padding2        - Padding after compressed hash
//   +0x5C (92): u32 data_offset     - Absolute file offset to data in .pk file
//   +0x60 (96): u32 is_compressed   - Low byte: 1=SD0 compressed, 0=uncompressed
struct PackIndexEntry {
    uint32_t crc;
    int32_t lower_crc;
    int32_t upper_crc;
    uint32_t uncompressed_size;
    char uncompressed_hash[32];
    uint32_t padding1;
    uint32_t compressed_size;
    char compressed_hash[32];
    uint32_t padding2;
    uint32_t data_offset;
    uint32_t is_compressed;
};

static_assert(sizeof(PackIndexEntry) == 100, "PackIndexEntry must be exactly 100 bytes");

} // namespace lu::assets
