#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sf2::data {

// Decompresses a complete zstd frame (RFC 8478) into a new buffer.
// Throws std::runtime_error on malformed input or decompression failure.
std::vector<std::uint8_t> zstd_decompress(const std::uint8_t* data, std::size_t size);

inline std::vector<std::uint8_t> zstd_decompress(const std::vector<std::uint8_t>& data) {
    return zstd_decompress(data.data(), data.size());
}

} // namespace sf2::data