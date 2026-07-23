#include <cstdio>
#include <cstring>
#include <cstdint>
#include "../engine/reverse/dz_decoder.hpp"

int main() {
    std::printf("Test parse_range_settings\n");
    int failures = 0;

    // Known header from files.dz data section (10 bytes at offset 6445)
    const uint8_t header[] = { 0x16, 0x01, 0x08, 0x03, 0x03, 0x07, 0x01, 0x07, 0x03, 0x0F };

    resf2::dz::RangeSettings rs{};
    if (!resf2::dz::parse_range_settings(header, rs)) {
        std::fprintf(stderr, "  FAIL: parse_range_settings returned false\n");
        return 1;
    }

    // There are two possible field layouts depending on whether byte 0 (0x16)
    // is a version marker or part of win_size/flags. We verify the invariant
    // fields and note both interpretations.
    std::printf("  win_size=%u flags=%u\n", rs.win_size, rs.flags);
    std::printf("  offset_table_size=%u offset_tables=%u offset_contexts=%u\n",
                rs.offset_table_size, rs.offset_tables, rs.offset_contexts);
    std::printf("  ref_length_table_size=%u ref_length_tables=%u\n",
                rs.ref_length_table_size, rs.ref_length_tables);
    std::printf("  ref_offset_table_size=%u ref_offset_tables=%u\n",
                rs.ref_offset_table_size, rs.ref_offset_tables);
    std::printf("  big_min_match=%u\n", rs.big_min_match);

    // Invariant: field values from known header bytes 0-9
    if (rs.win_size != 22) {
        std::fprintf(stderr, "  FAIL: win_size expected 22, got %u\n", rs.win_size);
        ++failures;
    }
    if (rs.flags != 1) {
        std::fprintf(stderr, "  FAIL: flags expected 1, got %u\n", rs.flags);
        ++failures;
    }
    if (rs.offset_table_size != 8) {
        std::fprintf(stderr, "  FAIL: offset_table_size expected 8, got %u\n", rs.offset_table_size);
        ++failures;
    }
    if (rs.offset_tables != 3) {
        std::fprintf(stderr, "  FAIL: offset_tables expected 3, got %u\n", rs.offset_tables);
        ++failures;
    }
    if (rs.offset_contexts != 3) {
        std::fprintf(stderr, "  FAIL: offset_contexts expected 3, got %u\n", rs.offset_contexts);
        ++failures;
    }
    if (rs.ref_length_table_size != 7) {
        std::fprintf(stderr, "  FAIL: ref_length_table_size expected 7, got %u\n", rs.ref_length_table_size);
        ++failures;
    }
    if (rs.ref_length_tables != 1) {
        std::fprintf(stderr, "  FAIL: ref_length_tables expected 1, got %u\n", rs.ref_length_tables);
        ++failures;
    }
    if (rs.ref_offset_table_size != 7) {
        std::fprintf(stderr, "  FAIL: ref_offset_table_size expected 7, got %u\n", rs.ref_offset_table_size);
        ++failures;
    }
    if (rs.ref_offset_tables != 3) {
        std::fprintf(stderr, "  FAIL: ref_offset_tables expected 3, got %u\n", rs.ref_offset_tables);
        ++failures;
    }
    if (rs.big_min_match != 15) {
        std::fprintf(stderr, "  FAIL: big_min_match expected 15, got %u\n", rs.big_min_match);
        ++failures;
    }

    // Null pointer test
    if (resf2::dz::parse_range_settings(nullptr, rs)) {
        std::fprintf(stderr, "  FAIL: parse_range_settings should reject null header\n");
        ++failures;
    }

    std::printf("\nResults: %d failures\n", failures);
    return failures;
}
