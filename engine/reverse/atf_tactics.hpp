// engine/reverse/atf_tactics.hpp
//
// Read-only parser for the .atf tactics blob format.
//
// .atf files are zlib-compressed custom blobs containing weapon-pair
// combat tactics. The decompressed format is:
//   u32 LE version           (always 1)
//   cstr  weapon_a_name      (null-terminated ASCII; empty for single-weapon files)
//   cstr  weapon_b_name      (null-terminated ASCII)
//   u8[]  binary_data        (rest of the file)
//
// The binary_data section starts with:
//   u32 LE record_count_or_size
//   u16 LE stride            (constant 858 across all observed files)
//   u16 LE unknown
//   u8[]  tactics_records
//
// Full byte layout of the tactics_records is deferred to Stage 4.x.
// This parser exposes the binary section as a std::span for downstream
// consumers (physics, battle logic).
//
// Stage 5 task S5.2.

#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <zlib.h>

namespace resf2::reverse::atf {

// Header of the decompressed .atf file.
struct Header {
    std::uint32_t version = 0;        // always 1
    std::string   weapon_a_name;      // empty for single-weapon files
    std::string   weapon_b_name;
};

// Binary section prefix (first 8 bytes after the weapon names).
struct BinaryPrefix {
    std::uint32_t record_count = 0;   // or total byte count (unclear)
    std::uint16_t stride = 0;         // constant 858 across observed files
    std::uint16_t unknown = 0;
};

// Parsed ATF tactics file.
struct ParsedTactics {
    Header        header;
    BinaryPrefix  binary_prefix;
    // The full decompressed payload (kept alive for the span).
    std::vector<std::byte> decompressed;
    // View into the binary records section (after the 8-byte prefix).
    std::span<const std::byte> binary_records;
};

// Error codes.
enum class ParseError {
    kOk = 0,
    kInputEmpty,
    kZlibDecompressFailed,
    kBadVersion,           // version != 1
    kMissingNullTerminator,
    kBinarySectionTooSmall,
};

[[nodiscard]] const char* to_string(ParseError e) noexcept;

// Decompress and parse a .atf file.
// `compressed` is the raw .atf file contents (zlib-compressed).
[[nodiscard]] auto parse(std::span<const std::byte> compressed)
    -> std::expected<ParsedTactics, ParseError>;

// Convenience: parse from a file on disk.
[[nodiscard]] auto parse_file(const std::string& path)
    -> std::expected<ParsedTactics, ParseError>;

}  // namespace resf2::reverse::atf
