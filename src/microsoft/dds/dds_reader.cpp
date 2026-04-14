#include "microsoft/dds/dds_reader.h"
#include "common/binary_reader/binary_reader.h"
#include <string>

#include <cstring>

namespace lu::assets {

DdsFile dds_parse_header(std::span<const uint8_t> data) {
    if (data.size() < 128) { // 4 magic + 124 header
        throw DdsError("DDS: file too small for header");
    }

    BinaryReader r(data);

    // Magic "DDS "
    uint32_t magic = r.read_u32();
    if (magic != DDS_MAGIC) {
        throw DdsError("DDS: invalid magic (expected 0x20534444)");
    }

    DdsFile dds{};

    // Header (124 bytes)
    dds.header.size = r.read_u32();
    if (dds.header.size != 124) {
        throw DdsError("DDS: invalid header size " + std::to_string(dds.header.size));
    }

    dds.header.flags = r.read_u32();
    dds.header.height = r.read_u32();
    dds.header.width = r.read_u32();
    dds.header.pitch_or_linear_size = r.read_u32();
    dds.header.depth = r.read_u32();
    dds.header.mip_map_count = r.read_u32();

    // Reserved1[11] — Microsoft spec says must be zero, stored for completeness
    for (int i = 0; i < 11; ++i) {
        dds.header.reserved1[i] = r.read_u32();
    }

    // Pixel format (32 bytes)
    dds.header.pixel_format.size = r.read_u32();
    dds.header.pixel_format.flags = r.read_u32();
    dds.header.pixel_format.four_cc = r.read_u32();
    dds.header.pixel_format.rgb_bit_count = r.read_u32();
    dds.header.pixel_format.r_bit_mask = r.read_u32();
    dds.header.pixel_format.g_bit_mask = r.read_u32();
    dds.header.pixel_format.b_bit_mask = r.read_u32();
    dds.header.pixel_format.a_bit_mask = r.read_u32();

    // Caps
    dds.header.caps = r.read_u32();
    dds.header.caps2 = r.read_u32();
    dds.header.caps3 = r.read_u32();
    dds.header.caps4 = r.read_u32();
    dds.header.reserved2 = r.read_u32();

    // Fill convenience fields
    dds.width = dds.header.width;
    dds.height = dds.header.height;
    dds.mip_count = dds.header.mip_map_count > 0 ? dds.header.mip_map_count : 1;

    dds.is_compressed = (dds.header.pixel_format.flags & DDPF_FOURCC) != 0;
    dds.four_cc = dds.header.pixel_format.four_cc;
    dds.bits_per_pixel = dds.header.pixel_format.rgb_bit_count;

    // Data offset: right after header (and DX10 extended header if present)
    dds.data_offset = 128;
    if (dds.is_compressed && dds.four_cc == FOURCC_DX10) {
        dds.data_offset += 20; // DX10 extended header
    }

    return dds;
}

} // namespace lu::assets
