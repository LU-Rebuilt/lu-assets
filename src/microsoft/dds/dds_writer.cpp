#include "microsoft/dds/dds_writer.h"
#include "common/binary_writer/binary_writer.h"

namespace lu::assets {

std::vector<uint8_t> dds_write(const DdsFile& dds, std::span<const uint8_t> payload) {
    BinaryWriter w;

    w.write_u32(DDS_MAGIC);

    const DdsHeader& h = dds.header;
    w.write_u32(h.size);
    w.write_u32(h.flags);
    w.write_u32(h.height);
    w.write_u32(h.width);
    w.write_u32(h.pitch_or_linear_size);
    w.write_u32(h.depth);
    w.write_u32(h.mip_map_count);
    for (int i = 0; i < 11; ++i) {
        w.write_u32(h.reserved1[i]);
    }

    w.write_u32(h.pixel_format.size);
    w.write_u32(h.pixel_format.flags);
    w.write_u32(h.pixel_format.four_cc);
    w.write_u32(h.pixel_format.rgb_bit_count);
    w.write_u32(h.pixel_format.r_bit_mask);
    w.write_u32(h.pixel_format.g_bit_mask);
    w.write_u32(h.pixel_format.b_bit_mask);
    w.write_u32(h.pixel_format.a_bit_mask);

    w.write_u32(h.caps);
    w.write_u32(h.caps2);
    w.write_u32(h.caps3);
    w.write_u32(h.caps4);
    w.write_u32(h.reserved2);

    w.write_bytes(payload.data(), payload.size());

    return std::move(w.data());
}

} // namespace lu::assets
