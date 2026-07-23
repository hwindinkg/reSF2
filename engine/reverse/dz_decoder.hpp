#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace resf2::dz {

struct RangeSettings {
    uint8_t win_size;
    uint8_t flags;
    uint8_t offset_table_size;
    uint8_t offset_tables;
    uint8_t offset_contexts;
    uint8_t ref_length_table_size;
    uint8_t ref_length_tables;
    uint8_t ref_offset_table_size;
    uint8_t ref_offset_tables;
    uint8_t big_min_match;
};

struct DzContext {
    uint16_t* prob;          // [0] probability table base (640 entries)
    uint8_t*  out_pos;       // [5] current output position
    uint32_t  range;         // [7] range coder range
    uint32_t  code;          // [8] range coder code
    uint8_t*  in_pos;        // [6] input stream position
    uint8_t*  in_end;        // implicit input end
    uint32_t  out_count;     // [9] bytes output so far
    uint32_t  out_limit;     // limit on output
    uint32_t  window_mask;   // [0xe] window size mask
    uint32_t  state;         // [0xd] decoder state (0..11)
    uint32_t  rep0;          // [0xf] recent offset 0
    uint32_t  rep1;          // [0x10] recent offset 1
    uint32_t  rep2;          // [0x11] recent offset 2
    uint32_t  rep3;          // [0x3c] recent offset 3 (from old struct)
    uint32_t  pos_state_mask;
    uint32_t  literal_ctx_bits;
    uint8_t   window[5];     // [0x17] 5-byte context window
    uint32_t  window_filled; // [0x16] how many window bytes filled

    uint32_t  total_out;
    uint32_t  input_consumed;
};

bool parse_range_settings(const uint8_t* header, RangeSettings& rs);
uint32_t crc32_window(const uint8_t* window, int len);
int dz_bit(DzContext& ctx, uint16_t& prob);
uint32_t dz_decode_bit_tree(DzContext& ctx, uint16_t* tree, uint32_t depth);
void dz_normalize(DzContext& ctx);

std::vector<uint8_t> dz_decompress(const uint8_t* compressed, size_t comp_size, size_t uncomp_size);

// Backward-compatible class wrapper
class DzDecompressor {
public:
    static std::vector<uint8_t> decompress(const uint8_t* data, size_t comp_size, size_t uncomp_size) {
        return dz_decompress(data, comp_size, uncomp_size);
    }
};

}  // namespace resf2::dz
