#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include "../engine/reverse/dz_decoder.hpp"

int main() {
    std::printf("DZ Probability Layout Analysis\n\n");

    // === Known RangeSettings from file 0 ===
    resf2::dz::RangeSettings rs{};
    rs.win_size = 22;
    rs.flags = 1;
    rs.offset_table_size = 8;
    rs.offset_tables = 3;
    rs.offset_contexts = 3;
    rs.ref_length_table_size = 7;
    rs.ref_length_tables = 1;
    rs.ref_offset_table_size = 7;
    rs.ref_offset_tables = 3;
    rs.big_min_match = 15;

    std::printf("RangeSettings:\n");
    std::printf("  offset_table_size=%u offset_tables=%u offset_contexts=%u\n",
           rs.offset_table_size, rs.offset_tables, rs.offset_contexts);
    std::printf("  ref_length_table_size=%u ref_length_tables=%u\n",
           rs.ref_length_table_size, rs.ref_length_tables);
    std::printf("  ref_offset_table_size=%u ref_offset_tables=%u\n",
           rs.ref_offset_table_size, rs.ref_offset_tables);
    std::printf("  big_min_match=%u\n", rs.big_min_match);

    // DZ format: probability table has 640 uint16 entries
    // The table is organized as:
    //
    // Header region [0..127]: core decision tables (256 bytes)
    //   [0..95]:     is_match[12][8]             = 96 entries
    //   [96..107]:   is_rep[12]                  = 12 entries
    //   [108..119]:  is_rep0[12]                 = 12 entries
    //   [120..131]:  is_rep1[12]                 = 12 entries
    //   [132..143]:  is_rep2[12]                 = 12 entries
    //   [144..179]:  len_choice[pos_state * 3 + bit]  = 36 entries (12 pos_states)
    //   [180..215]:  rep_len_choice[12][3]       = 36 entries
    //   [216.....]:  dist_slot tables             = ?
    //
    // Context region [128..639]: per-context tables
    //   Table 0: literal bit-trees (384 entries = 0x600 bytes at prob+0x100)
    //     - Actually: (1 << offset_contexts) contexts × literal tables
    //     - 8 contexts => each gets some portion
    //   Remaining: ref-offset tables, ref-length tables

    constexpr int PROB_ENTRIES = 640;

    // Index constants
    constexpr int IDX_IS_MATCH    = 0;
    constexpr int IDX_IS_REP      = 96;
    constexpr int IDX_IS_REP0     = 108;
    constexpr int IDX_IS_REP1     = 120;
    constexpr int IDX_IS_REP2     = 132;
    constexpr int IDX_LEN_CHOICE  = 144;
    constexpr int IDX_REP_LEN_CHOICE = 180;
    constexpr int IDX_DIST_SLOT   = 216;

    int n_dist_slots = 36; // plan says "36-ish"
    int n_literal_entries = 384;
    int ctx_tables_start = IDX_DIST_SLOT + n_dist_slots;
    // ctx_tables_start = 216 + 36 = 252
    // Then literal starts at 252, using 384 → ends at 636
    // Remaining: 640 - 636 = 4 entries (slack)

    int n_contexts = 1 << rs.offset_contexts; // 8
    int literal_per_ctx = n_literal_entries / n_contexts; // 48

    // ref_offset + ref_length tables after literals
    int ref_tables_start = ctx_tables_start + n_literal_entries; // 636

    std::printf("\nProposed layout:\n");
    std::printf("  is_match:        [%3d..%3d] (%d)\n", IDX_IS_MATCH, IDX_IS_REP-1, 96);
    std::printf("  is_rep:          [%3d..%3d] (%d)\n", IDX_IS_REP, IDX_IS_REP0-1, 12);
    std::printf("  is_rep0:         [%3d..%3d] (%d)\n", IDX_IS_REP0, IDX_IS_REP1-1, 12);
    std::printf("  is_rep1:         [%3d..%3d] (%d)\n", IDX_IS_REP1, IDX_IS_REP2-1, 12);
    std::printf("  is_rep2:         [%3d..%3d] (%d)\n", IDX_IS_REP2, IDX_LEN_CHOICE-1, 12);
    std::printf("  len_choice:      [%3d..%3d] (%d)\n", IDX_LEN_CHOICE, IDX_REP_LEN_CHOICE-1, 36);
    std::printf("  rep_len_choice:  [%3d..%3d] (%d)\n", IDX_REP_LEN_CHOICE, IDX_DIST_SLOT-1, 36);
    std::printf("  dist_slot:       [%3d..%3d] (%d)\n", IDX_DIST_SLOT, ctx_tables_start-1, n_dist_slots);
    std::printf("  literal:         [%3d..%3d] (%d)\n", ctx_tables_start, ref_tables_start-1, n_literal_entries);
    std::printf("    %d contexts × %d entries each\n", n_contexts, literal_per_ctx);
    std::printf("  ref_tables:      [%3d..%3d] (%d)\n", ref_tables_start, PROB_ENTRIES-1, PROB_ENTRIES - ref_tables_start);

    std::printf("  total: %d\n", PROB_ENTRIES);
    std::printf("  slack: %d\n", PROB_ENTRIES - ref_tables_start);

    // Test: verify indices don't overflow
    if (ref_tables_start > PROB_ENTRIES) {
        std::fprintf(stderr, "OVERFLOW: context tables exceed %d entries!\n", PROB_ENTRIES);
        return 1;
    }

    std::printf("\nAll indices valid.\n");
    return 0;
}
