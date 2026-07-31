// engine/reverse/sts_tables.hpp
//
// Stub parser interface for the .sts shift-table family
// ([ORIGINAL] path string `assets/tactics/shiftTables/*.sts`,
// PORT_GAPS.md:163).
//
// Format UNREVERSED — @reverser task R1 (phase-5 PLAN B3) determines
// whether .sts is a zlib blob (78 DA magic), XML, or something else, and
// whether the family is packed inside .atf `binary_records` secondary
// records instead. No .sts assets exist in this dump. Until R1 lands,
// parse/parse_file return kFamilyUnavailable and no table of this family
// can load — a missing family is a normal condition (ADR-005 D3).

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace resf2::reverse::sts {

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

// Parsed .sts table — placeholder shape, to be defined by R1.
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

}  // namespace resf2::reverse::sts
