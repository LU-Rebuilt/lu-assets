#include "microsoft/tga/tga_reader.h"

#include <stb_image.h>

namespace lu::assets {

TgaImage tga_load(std::span<const uint8_t> data) {
    if (data.empty()) {
        throw TgaError("TGA: empty data");
    }

    TgaImage image;

    // Request 4 channels (RGBA) for consistent output
    int desired_channels = 4;

    stbi_uc* pixels = stbi_load_from_memory(
        data.data(),
        static_cast<int>(data.size()),
        &image.width,
        &image.height,
        &image.channels,
        desired_channels
    );

    if (!pixels) {
        throw TgaError("TGA: stb_image decode failed: " + std::string(stbi_failure_reason()));
    }

    // channels reports the original channel count; actual data has desired_channels
    image.channels = desired_channels;
    size_t pixel_count = static_cast<size_t>(image.width) * image.height * desired_channels;
    image.pixel_data.assign(pixels, pixels + pixel_count);

    stbi_image_free(pixels);

    return image;
}

TgaFile tga_parse(std::span<const uint8_t> data) {
    if (data.size() < 18) {
        throw TgaError("TGA: file too small for 18-byte header");
    }

    auto u16 = [&](size_t off) -> uint16_t {
        return static_cast<uint16_t>(data[off] | (data[off + 1] << 8));
    };

    TgaFile tga;
    tga.id_length = data[0];
    tga.color_map_type = data[1];
    tga.image_type = data[2];
    tga.color_map_first = u16(3);
    tga.color_map_length = u16(5);
    tga.color_map_depth = data[7];
    tga.x_origin = u16(8);
    tga.y_origin = u16(10);
    tga.width = u16(12);
    tga.height = u16(14);
    tga.bits_per_pixel = data[16];
    tga.descriptor = data[17];

    tga.payload.assign(data.begin() + 18, data.end());
    return tga;
}

} // namespace lu::assets

