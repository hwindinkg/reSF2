#pragma once
#include <cstdint>

/// Represents a half-open range [begin, end) into a string buffer.
/// Used by the original engine's Condition system for string matching.
struct NameRange {
    const char* begin;
    const char* end;
};

/// Compares a null-terminated C-string with a NameRange [begin, end).
/// Returns low byte = 1 for exact match, 0 for mismatch.
/// [ORIGINAL] FUN_1002bb10 (x86) / stringEqualWithRange — VERIFIED GREEN.
uint32_t stringEqualWithRange(const char* str, const NameRange& range) noexcept;

/// Compares two NameRange [begin, end) for byte-exact equality.
/// Returns low byte = 1 for exact match, 0 for mismatch.
/// [ORIGINAL] FUN_1000cb90 (x86) / nameRangeEqual — VERIFIED GREEN.
uint32_t nameRangeEqual(const NameRange& lhs, const NameRange& rhs) noexcept;

/// Compares NameRange [begin, end) with a C-string (order: range first, then string).
/// Returns low byte = 1 for exact match, 0 for mismatch.
/// [ORIGINAL] FUN_1000cc00 (x86) / nameRangeEqualsStr — VERIFIED GREEN.
uint32_t nameRangeEqualsStr(const NameRange& range, const char* str) noexcept;
