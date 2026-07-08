#include "microsoft/tga/tga_writer.h"
#include "common/binary_writer/binary_writer.h"

namespace lu::assets {

std::vector<uint8_t> tga_write(const TgaFile& tga) {
    BinaryWriter w;

    w.write_u8(tga.id_length);
    w.write_u8(tga.color_map_type);
    w.write_u8(tga.image_type);
    w.write_u16(tga.color_map_first);
    w.write_u16(tga.color_map_length);
    w.write_u8(tga.color_map_depth);
    w.write_u16(tga.x_origin);
    w.write_u16(tga.y_origin);
    w.write_u16(tga.width);
    w.write_u16(tga.height);
    w.write_u8(tga.bits_per_pixel);
    w.write_u8(tga.descriptor);

    w.write_bytes(tga.payload.data(), tga.payload.size());

    return std::move(w.data());
}

} // namespace lu::assets
