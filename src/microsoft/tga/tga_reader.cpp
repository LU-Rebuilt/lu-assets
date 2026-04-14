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

} // namespace lu::assets
