#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sf2::data {

// Decompresses a complete zstd frame (RFC 8478) into a new buffer.
// Throws std::runtime_error on malformed input or decompression failure.
std::vector<std::uint8_t> zstd_decompress(const std::uint8_t* data, std::size_t size);

// Compresses bytes into one zstd frame at `level` (1-22, default 3).
// The `Aa.save` half of the SF2User envelope. Throws on failure.
std::vector<std::uint8_t> zstd_compress(const std::uint8_t* data, std::size_t size,
                                        int level = 3);

inline std::vector<std::uint8_t> zstd_decompress(const std::vector<std::uint8_t>& data) {
    return zstd_decompress(data.data(), data.size());
}

} // namespace sf2::data