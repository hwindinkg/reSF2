// Texture format dispatch — sniffs magic bytes, falls back to extension.

#include "texture.hpp"

#include <cstring>
#include <fstream>
#include <string>

namespace sf2::data {

namespace {

std::string lower_ext(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return "";
    }
    std::string ext = path.substr(dot);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

} // namespace

bool decode_texture_bytes(const std::uint8_t* data, std::size_t size,
                          const std::string& ext, Texture& out) {
    if (data == nullptr || size < 8) {
        return false;
    }
    // PNG magic.
    static const std::uint8_t kPng[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    // JPEG magic.
    static const std::uint8_t kJpeg[3] = {0xFF, 0xD8, 0xFF};
    // WebP: "RIFF"...."WEBP".
    static const std::uint8_t kRiff[4] = {'R', 'I', 'F', 'F'};
    static const std::uint8_t kWebp[4] = {'W', 'E', 'B', 'P'};
    // DDS magic.
    static const std::uint8_t kDds[4] = {'D', 'D', 'S', ' '};
    // KTX magic.
    static const std::uint8_t kKtx[12] = {0xAB, 'K', 'T', 'X', ' ', '1', '1',
                                          0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    // Crunch (CRN) magic: 0x7848 = "Hx". Not supported (see README).
    static const std::uint8_t kCrn[2] = {0x48, 0x78};

    if (std::memcmp(data, kPng, 8) == 0 || std::memcmp(data, kJpeg, 3) == 0 ||
        ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
        return decode_png_or_jpeg(data, size, out);
    }
    if (size >= 12 && std::memcmp(data, kRiff, 4) == 0 &&
        std::memcmp(data + 8, kWebp, 4) == 0) {
        return decode_webp(data, size, out);
    }
    if (std::memcmp(data, kDds, 4) == 0) {
        return decode_dds(data, size, out);
    }
    if (std::memcmp(data, kKtx, 12) == 0) {
        return decode_ktx(data, size, out);
    }
    // AVIF is deferred: the game ships .webp fallbacks for avif-only content.
    (void)kCrn;
    return false;
}

bool decode_texture(const std::string& path, Texture& out) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return false;
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size <= 0) {
        return false;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(data.data()), size);
    if (!in) {
        return false;
    }
    return decode_texture_bytes(data.data(), data.size(), lower_ext(path), out);
}

} // namespace sf2::data
