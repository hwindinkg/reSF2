// engine/reverse/atf_tactics.cpp
//
// Implementation of the .atf tactics blob parser.

#include "atf_tactics.hpp"

#include <cstring>
#include <fstream>
#include <memory>

namespace resf2::reverse::atf {

namespace {

// Decompress a zlib stream. Returns the decompressed bytes.
// Returns an empty vector on failure.
[[nodiscard]] std::vector<std::byte> zlib_decompress(std::span<const std::byte> src) {
    // Use zlib's inflate() with a growing output buffer.
    z_stream zs{};
    zs.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(src.data()));
    zs.avail_in = static_cast<uInt>(src.size());

    if (inflateInit(&zs) != Z_OK) return {};

    std::vector<std::byte> out;
    out.resize(64 * 1024);  // start with 64 KB
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
            // Grow buffer
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

// Read a null-terminated ASCII string from `data` starting at `offset`.
// Updates `offset` to point past the null terminator.
// Returns empty string and sets error if no null found within reasonable bounds.
[[nodiscard]] std::string read_cstring(std::span<const std::byte> data,
                                       std::size_t& offset) {
    std::string result;
    while (offset < data.size()) {
        char c = std::to_integer<char>(data[offset]);
        ++offset;
        if (c == '\0') return result;
        result.push_back(c);
    }
    return result;  // unterminated; caller should check
}

[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::byte> data,
                                        std::size_t offset) {
    if (offset + 4 > data.size()) return 0;
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset]))
         | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 1])) << 8)
         | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 2])) << 16)
         | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 3])) << 24);
}

[[nodiscard]] std::uint16_t read_u16_le(std::span<const std::byte> data,
                                        std::size_t offset) {
    if (offset + 2 > data.size()) return 0;
    return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[offset]))
         | (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(data[offset + 1])) << 8);
}

}  // namespace

const char* to_string(ParseError e) noexcept {
    switch (e) {
        case ParseError::kOk:                    return "ok";
        case ParseError::kInputEmpty:            return "input is empty";
        case ParseError::kZlibDecompressFailed:  return "zlib decompression failed";
        case ParseError::kBadVersion:            return "bad version (expected 1)";
        case ParseError::kMissingNullTerminator: return "missing null terminator in weapon name";
        case ParseError::kBinarySectionTooSmall: return "binary section too small for prefix";
    }
    return "unknown error";
}

auto parse(std::span<const std::byte> compressed)
    -> std::expected<ParsedTactics, ParseError> {

    if (compressed.empty()) {
        return std::unexpected(ParseError::kInputEmpty);
    }

    auto decompressed = zlib_decompress(compressed);
    if (decompressed.empty()) {
        return std::unexpected(ParseError::kZlibDecompressFailed);
    }

    ParsedTactics result;
    result.decompressed = std::move(decompressed);
    auto data = std::span<const std::byte>(result.decompressed);

    if (data.size() < 4) {
        return std::unexpected(ParseError::kBinarySectionTooSmall);
    }

    // Read version (u32 LE at offset 0)
    result.header.version = read_u32_le(data, 0);
    if (result.header.version != 1) {
        return std::unexpected(ParseError::kBadVersion);
    }

    // Read weapon A name (null-terminated, starting at offset 4)
    std::size_t offset = 4;
    result.header.weapon_a_name = read_cstring(data, offset);
    if (offset > data.size() || (offset - 4) > 256) {
        // Sanity check: weapon names should be < 256 chars
        return std::unexpected(ParseError::kMissingNullTerminator);
    }

    // Read weapon B name (null-terminated)
    result.header.weapon_b_name = read_cstring(data, offset);
    if (offset > data.size() || (offset - 4 - result.header.weapon_a_name.size() - 1) > 256) {
        return std::unexpected(ParseError::kMissingNullTerminator);
    }

    // Read binary prefix: u32 record_count + u16 stride + u16 unknown
    if (offset + 8 > data.size()) {
        return std::unexpected(ParseError::kBinarySectionTooSmall);
    }
    result.binary_prefix.record_count = read_u32_le(data, offset);
    result.binary_prefix.stride       = read_u16_le(data, offset + 4);
    result.binary_prefix.unknown      = read_u16_le(data, offset + 6);

    // The rest is binary records
    std::size_t records_start = offset + 8;
    if (records_start > data.size()) {
        result.binary_records = {};
    } else {
        result.binary_records = data.subspan(records_start);
    }

    return result;
}

auto parse_file(const std::string& path)
    -> std::expected<ParsedTactics, ParseError> {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return std::unexpected(ParseError::kInputEmpty);
    auto size = static_cast<std::size_t>(f.tellg());
    if (size == 0) return std::unexpected(ParseError::kInputEmpty);
    f.seekg(0);
    std::vector<std::byte> buffer(size);
    f.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    if (!f) return std::unexpected(ParseError::kInputEmpty);
    return parse(std::span<const std::byte>(buffer));
}

}  // namespace resf2::reverse::atf
