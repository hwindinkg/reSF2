// engine/reverse/atf_tactics.cpp
//
// Implementation of the .atf tactics blob parser.
// See atf_tactics.hpp for the decompressed format description.

#include "atf_tactics.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>

namespace resf2::reverse::atf {

namespace {

// Decompress a zlib stream. Returns the decompressed bytes.
// Returns an empty vector on failure.
[[nodiscard]] std::vector<std::byte> zlib_decompress(std::span<const std::byte> src) {
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

// Read a null-terminated ASCII string from `data` starting at `offset`.
// Updates `offset` to point past the null terminator.
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

[[nodiscard]] std::uint8_t read_u8(std::span<const std::byte> data,
                                   std::size_t offset) {
    if (offset >= data.size()) return 0;
    return std::to_integer<std::uint8_t>(data[offset]);
}

// Extract individual animation names from the global concatenated string pool.
// The pool is a single long string of concatenated animation names without
// separators (e.g. "AssistantBigMagariYariPlayerAssistantBigMagariYariStart...").
// We split on PascalCase boundaries (lowercase->uppercase transitions) to
// recover individual names.
[[nodiscard]] std::vector<std::string> split_pool_names(const std::string& pool) {
    std::vector<std::string> names;
    if (pool.empty()) return names;

    std::string current;
    current.push_back(pool[0]);

    for (std::size_t i = 1; i < pool.size(); ++i) {
        char c = pool[i];
        char prev = pool[i - 1];
        // Detect PascalCase boundary: lowercase -> uppercase
        bool is_upper = (c >= 'A' && c <= 'Z');
        bool prev_lower = (prev >= 'a' && prev <= 'z');
        // Also detect digit boundaries (e.g. "Middle0" -> "Middle", "0...")
        bool is_digit = (c >= '0' && c <= '9');
        bool prev_alpha = (prev >= 'a' && prev <= 'z') || (prev >= 'A' && prev <= 'Z');

        if ((is_upper && prev_lower) || (is_digit && prev_alpha)) {
            if (!current.empty()) {
                names.push_back(std::move(current));
                current.clear();
            }
        }
        current.push_back(c);
    }
    if (!current.empty()) {
        names.push_back(std::move(current));
    }
    return names;
}

}  // namespace

const char* to_string(ParseError e) noexcept {
    switch (e) {
        case ParseError::kOk:                    return "ok";
        case ParseError::kInputEmpty:            return "input is empty";
        case ParseError::kZlibDecompressFailed:  return "zlib decompression failed";
        case ParseError::kBadVersion:            return "bad version (expected 1 or 2)";
        case ParseError::kMissingNullTerminator: return "missing null terminator in weapon name";
        case ParseError::kBinarySectionTooSmall: return "binary section too small for prefix";
        case ParseError::kBadStride:             return "bad stride (expected 858)";
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
    if (result.header.version != 1 && result.header.version != 2) {
        return std::unexpected(ParseError::kBadVersion);
    }

    // Read weapon A name (null-terminated, starting at offset 4)
    std::size_t offset = 4;
    result.header.weapon_a_name = read_cstring(data, offset);
    if (offset > data.size() || result.header.weapon_a_name.size() > 256) {
        return std::unexpected(ParseError::kMissingNullTerminator);
    }

    // Version 1 (pair files) has a second weapon name
    if (result.header.version == 1) {
        std::size_t pre_wb = offset;
        result.header.weapon_b_name = read_cstring(data, offset);
        if (offset > data.size() || (offset - pre_wb) > 256) {
            return std::unexpected(ParseError::kMissingNullTerminator);
        }
    }
    // Version 2 (single weapon) has no weapon_b — just the flag byte
    // is already consumed as part of the binary prefix below.

    // Binary prefix: 6 bytes
    //   u8 flags_a, u8 flags_b, u8 extra_flag, u8 padding, u16 LE stride
    if (offset + 6 > data.size()) {
        return std::unexpected(ParseError::kBinarySectionTooSmall);
    }
    result.binary_prefix.flags_a    = read_u8(data, offset);
    result.binary_prefix.flags_b    = read_u8(data, offset + 1);
    result.binary_prefix.extra_flag = read_u8(data, offset + 2);
    result.binary_prefix.padding    = read_u8(data, offset + 3);
    result.binary_prefix.stride     = read_u16_le(data, offset + 4);

    if (result.binary_prefix.stride != kRecordStride) {
        return std::unexpected(ParseError::kBadStride);
    }

    // Tactic record: 858 bytes of animation indices
    std::size_t record_start = offset + 6;
    if (record_start + kRecordStride > data.size()) {
        return std::unexpected(ParseError::kBinarySectionTooSmall);
    }
    for (std::size_t i = 0; i < kRecordStride; ++i) {
        result.animation_indices[i] = read_u8(data, record_start + i);
    }

    // String pool: starts right after the record
    std::size_t pool_start = record_start + kRecordStride;
    if (pool_start < data.size()) {
        // Read the first (huge) null-terminated string — the global pool
        std::size_t pool_end = pool_start;
        while (pool_end < data.size() &&
               std::to_integer<std::uint8_t>(data[pool_end]) != 0) {
            ++pool_end;
        }
        result.string_pool.reserve(pool_end - pool_start);
        for (std::size_t i = pool_start; i < pool_end; ++i) {
            result.string_pool.push_back(
                std::to_integer<char>(data[i]));
        }

        // Split the concatenated pool into individual animation names
        result.animation_names = split_pool_names(result.string_pool);
    }

    // Remaining binary data after the record (for downstream consumers)
    if (record_start + kRecordStride <= data.size()) {
        result.binary_records = data.subspan(record_start + kRecordStride);
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
