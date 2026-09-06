// WebP decoder — libwebp (FetchContent).
//
// The game ships .webp variants of its location atlases (e.g.
// res/locations/arena/arena.7995a5ab.webp) — the recommended fallback for
// the ASTC .ktx files, which this layer does not CPU-decode.

#include "texture.hpp"

#include <cstring>
#include <webp/decode.h>

namespace sf2::data {

bool decode_webp(const std::uint8_t* data, std::size_t size, Texture& out) {
    if (data == nullptr || size == 0) {
        return false;
    }
    if (size > static_cast<std::size_t>(INT32_MAX)) {
        return false;
    }
    int w = 0;
    int h = 0;
    std::uint8_t* pixels = WebPDecodeRGBA(data, static_cast<int>(size), &w, &h);
    if (pixels == nullptr || w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        if (pixels != nullptr) {
            WebPFree(pixels);
        }
        return false;
    }
    Texture tex;
    tex.w = w;
    tex.h = h;
    tex.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0);
    std::memcpy(tex.rgba.data(), pixels, static_cast<std::size_t>(w) * h * 4);
    WebPFree(pixels);
    out = std::move(tex);
    return true;
}

} // namespace sf2::data
