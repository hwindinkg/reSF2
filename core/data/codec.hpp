#pragma once

// Base64 codec (RFC 4648, standard alphabet with padding) - the `ri`
// encode/decode step of the SF2User envelope (FLOW_STATIC section 3.1:
// `Aa.load`: base64 -> un-zstd -> XML; `Aa.save`: XML -> zstd -> base64).
// Header-only, no dependencies.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace sf2::data {

namespace codec_detail {

inline const char* alphabet() {
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

inline int decode_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace codec_detail

// Encodes bytes to base64 text (with `=` padding).
inline std::string base64_encode(const std::uint8_t* data, std::size_t size) {
    const char* alpha = codec_detail::alphabet();
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t a = data[i];
        const std::uint32_t b = i + 1 < size ? data[i + 1] : 0;
        const std::uint32_t c = i + 2 < size ? data[i + 2] : 0;
        const std::uint32_t triple = (a << 16) | (b << 8) | c;
        out.push_back(alpha[(triple >> 18) & 0x3F]);
        out.push_back(alpha[(triple >> 12) & 0x3F]);
        out.push_back(i + 1 < size ? alpha[(triple >> 6) & 0x3F] : '=');
        out.push_back(i + 2 < size ? alpha[triple & 0x3F] : '=');
    }
    return out;
}

inline std::string base64_encode(const std::vector<std::uint8_t>& data) {
    return data.empty() ? std::string() : base64_encode(data.data(), data.size());
}

inline std::string base64_encode(const std::string& text) {
    return text.empty()
               ? std::string()
               : base64_encode(reinterpret_cast<const std::uint8_t*>(text.data()),
                               text.size());
}

// Decodes base64 text (whitespace rejected; `=` padding required at the
// tail). Throws std::runtime_error on malformed input.
inline std::vector<std::uint8_t> base64_decode(const std::string& text) {
    if (text.size() % 4 != 0) {
        throw std::runtime_error("base64_decode: length not a multiple of 4");
    }
    std::vector<std::uint8_t> out;
    out.reserve((text.size() / 4) * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        int vals[4];
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = text[i + static_cast<std::size_t>(k)];
            if (c == '=') {
                vals[k] = 0;
                ++pad;
            } else {
                if (pad > 0) {
                    throw std::runtime_error("base64_decode: padding mid-quantum");
                }
                vals[k] = codec_detail::decode_value(c);
                if (vals[k] < 0) {
                    throw std::runtime_error("base64_decode: bad character");
                }
            }
        }
        if (pad > 2) {
            throw std::runtime_error("base64_decode: too much padding");
        }
        const std::uint32_t triple = (static_cast<std::uint32_t>(vals[0]) << 18) |
                                     (static_cast<std::uint32_t>(vals[1]) << 12) |
                                     (static_cast<std::uint32_t>(vals[2]) << 6) |
                                     static_cast<std::uint32_t>(vals[3]);
        out.push_back(static_cast<std::uint8_t>((triple >> 16) & 0xFF));
        if (pad < 2) out.push_back(static_cast<std::uint8_t>((triple >> 8) & 0xFF));
        if (pad < 1) out.push_back(static_cast<std::uint8_t>(triple & 0xFF));
    }
    return out;
}

}  // namespace sf2::data
