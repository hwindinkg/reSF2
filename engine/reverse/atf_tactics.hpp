// engine/reverse/atf_tactics.hpp
//
// Read-only parser for the .atf tactics blob format.
//
// .atf files are zlib-compressed custom blobs containing per-warrior
// combat tactics.  Each file stores a single tactic record (858 u8
// animation indices) plus a string pool of animation names.
//
// Decompressed layout:
//   u32 LE  version             (1 = weapon-pair, 2 = single-weapon)
//   cstr    weapon_a_name       (null-terminated ASCII)
//   [v=1]   cstr weapon_b_name  (null-terminated ASCII; empty cstr "\0"
//                                when both weapons are the same type)
//   --- binary prefix (6 bytes) ---
//     u8    flags_a             (weapon-dependent hash/type)
//     u8    flags_b             (weapon-dependent)
//     u8    extra_flag          (0 or 1 — purpose unknown)
//     u8    padding             (always 0)
//     u16 LE stride             (constant 858 across all observed files)
//   --- tactic record (stride bytes) ---
//     u8[858] animation indices (values index into the string pool)
//   --- string pool ---
//     cstr    global_pool       (huge concatenated animation name list)
//     ...     additional data   (weapon name list, per-weapon animation
//                                names, secondary records)
//
// The parser exposes the 858-byte record as `animation_indices` and
// extracts the global animation name pool as `string_pool`.  Individual
// animation names used by the record can be obtained via `resolve_name()`.
//
// Stage 4.5 — Step 4.5 of the port plan.

#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <zlib.h>

namespace resf2::reverse::atf {

// The constant stride observed across every .atf file.
inline constexpr std::size_t kRecordStride = 858;

// Header of the decompressed .atf file.
struct Header {
    std::uint32_t version = 0;        // 1 = pair, 2 = single-weapon
    std::string   weapon_a_name;
    std::string   weapon_b_name;      // empty for v=2 single-weapon files
};

// Binary section prefix (6 bytes after the weapon names).
struct BinaryPrefix {
    std::uint8_t  flags_a = 0;        // weapon-dependent hash/type byte
    std::uint8_t  flags_b = 0;        // weapon-dependent
    std::uint8_t  extra_flag = 0;     // 0 or 1
    std::uint8_t  padding = 0;        // always 0
    std::uint16_t stride = 0;         // constant 858
};

// Parsed ATF tactics file.
struct ParsedTactics {
    Header        header;
    BinaryPrefix  binary_prefix;

    // The full decompressed payload (kept alive for spans).
    std::vector<std::byte> decompressed;

    // The 858-byte tactic record: animation indices (u8 values).
    // Each byte is an index into the string pool entries.
    std::array<std::uint8_t, kRecordStride> animation_indices{};

    // The global animation name pool (one huge concatenated string).
    // Individual names are concatenated without separators; use
    // `animation_names` for the parsed-out list.
    std::string string_pool;

    // Parsed individual animation names extracted from the string pool.
    // Index 0 is the first name found.  The record bytes index into
    // a subset of these names.
    std::vector<std::string> animation_names;

    // View into any remaining binary data after the record.
    std::span<const std::byte> binary_records;

    // Resolve an animation name by its pool index.
    // Returns empty string_view if index is out of range.
    [[nodiscard]] std::string_view resolve_name(std::size_t index) const {
        if (index < animation_names.size()) return animation_names[index];
        return {};
    }
};

// Error codes.
enum class ParseError {
    kOk = 0,
    kInputEmpty,
    kZlibDecompressFailed,
    kBadVersion,           // version not 1 or 2
    kMissingNullTerminator,
    kBinarySectionTooSmall,
    kBadStride,            // stride != 858
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
