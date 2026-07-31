// engine/reverse/stb_tables.hpp
//
// Stub parser interface for the .stb shift-table family
// ([ORIGINAL] path string `assets/tactics/shift/*.stb`, PORT_GAPS.md:162).
//
// Format UNREVERSED — @reverser task R1 (phase-5 PLAN B3) determines
// whether .stb is a zlib blob (78 DA magic), XML, or something else, and
// whether the family is packed inside .atf `binary_records` secondary
// records instead. No .stb assets exist in this dump. Until R1 lands,
// parse/parse_file return kFamilyUnavailable and no table of this family
// can load — a missing family is a normal condition (ADR-005 D3).
//
// Note: `heel %s not found in shift table for %s` (PORT_GAPS.md:189-190)
// shows shift tables are keyed by heel node per animation frame.
//
// R1 verdict (TABLE_FORMATS.md): DEAD format — strings 0x8F7979B0/0x8F7979AC unreferenced.

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace resf2::reverse::stb {

enum class ParseError {
    kOk = 0,
    kInputEmpty,
    kFamilyUnavailable,   // format unreversed (R1)
};

[[nodiscard]] inline const char* to_string(ParseError e) noexcept {
    switch (e) {
        case ParseError::kOk:                return "ok";
        case ParseError::kInputEmpty:        return "input is empty";
        case ParseError::kFamilyUnavailable: return "family unavailable (format unreversed)";
    }
    return "unknown error";
}

// Parsed .stb table — placeholder shape, to be defined by R1.
struct ParsedTable {
    std::string              name;
    std::vector<std::string> candidates;
    std::vector<std::uint8_t> record;
};

[[nodiscard]] inline auto parse(std::span<const std::byte> /*data*/)
    -> std::expected<ParsedTable, ParseError> {
    return std::unexpected(ParseError::kFamilyUnavailable);
}

[[nodiscard]] inline auto parse_file(const std::string& path)
    -> std::expected<ParsedTable, ParseError> {
    if (path.empty()) return std::unexpected(ParseError::kInputEmpty);
    return std::unexpected(ParseError::kFamilyUnavailable);
}

}  // namespace resf2::reverse::stb
