// PNG/JPEG decoder — stb_image (vendored single header).
//
// stb_image decodes PNG/JPEG/GIF/BMP/PSD/TGA/etc. into 8-bit channels.
// The game's .png files include indexed-color (palette) PNGs (e.g.
// res/fight/ui.62bee150.png); stb_image handles those, expanding to RGBA.

#include "texture.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

namespace sf2::data {

bool decode_png_or_jpeg(const std::uint8_t* data, std::size_t size, Texture& out) {
    if (data == nullptr || size == 0) {
        return false;
    }
    int w = 0;
    int h = 0;
    int channels = 0;
    // stbi_load_from_memory wants the file size as int; cap at INT_MAX.
    if (size > static_cast<std::size_t>(INT32_MAX)) {
        return false;
    }
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &channels, 4);
    if (pixels == nullptr) {
        return false;
    }
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        stbi_image_free(pixels);
        return false;
    }
    Texture tex;
    tex.w = w;
    tex.h = h;
    tex.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
    std::memcpy(tex.rgba.data(), pixels, static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(pixels);
    out = std::move(tex);
    return true;
}

} // namespace sf2::data
