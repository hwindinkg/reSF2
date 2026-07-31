// engine/reverse/zlib_blob.hpp
//
// Shared zlib inflate helper for the binary reverse parsers.
//
// Extracted from atf_tactics.cpp (ADR-005 D3): 4 known zlib users
// (atf / tbs / stb / sts) satisfy the 3+ rule for extraction.
// Header-only so every family parser can use it without a new .cpp.

#pragma once

#include <cstddef>
#include <span>
#include <vector>
#include <zlib.h>

namespace resf2::reverse {

// Decompress a zlib stream. Returns the decompressed bytes.
// Returns an empty vector on failure.
[[nodiscard]] inline std::vector<std::byte> zlib_inflate(std::span<const std::byte> src) {
    z_stream zs{};
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src.data()));
    zs.avail_in = static_cast<uInt>(src.size());

    if (inflateInit(&zs) != Z_OK) return {};

    std::vector<std::byte> out;
    out.resize(64 * 1024);
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());

    int ret = Z_OK;
    while (true) {
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_END) break;
        if (ret != Z_OK) {
            inflateEnd(&zs);
            return {};
        }
        if (zs.avail_out == 0) {
            std::size_t old_size = out.size();
            out.resize(old_size * 2);
            zs.next_out = reinterpret_cast<Bytef*>(out.data() + old_size);
            zs.avail_out = static_cast<uInt>(out.size() - old_size);
        }
    }
    out.resize(out.size() - zs.avail_out);
    inflateEnd(&zs);
    return out;
}

}  // namespace resf2::reverse
