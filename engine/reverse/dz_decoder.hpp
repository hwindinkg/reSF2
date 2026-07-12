// engine/reverse/dz_decoder.hpp
//
// DZ (derbh) decompressor — clean-room reimplementation.
//
// Algorithm reverse-engineered from libs3e_android.so function at 0x389f8
// (and helpers at 0x37adc, 0x37a5c, 0x37e28, 0x3751c, 0x37c44, 0x3772c).
//
// The DZ format is Marmalade's custom compression, which is an LZMA-variant
// range coder with:
//   - 32-bit range coder (range + code registers)
//   - 5-byte context window for probability modeling
//   - CRC32-derived hash for context table lookup
//   - Bit-tree decoding for literals and match lengths
//   - LZ77-style match references
//
// The streaming nature (overlapping file offsets in the archive) means
// the entire archive's data section is one continuous compressed stream.
// Each file's "offset" is the position in the stream where that file's
// decoding STARTS, and the decompressor state carries over between files.

#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace resf2::dz {

class DzRangeDecoder {
public:
    DzRangeDecoder(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0) {
        range_ = 0xFFFFFFFF;
        code_ = 0;
        // Read first 4 bytes into code register
        for (int i = 0; i < 4 && pos_ < size_; ++i) {
            code_ = (code_ << 8) | data_[pos_++];
        }
    }

    // Decode a single bit with probability model.
    // prob is a reference to a uint16_t in [1, 0xFFFF-0x800] (typically 0x400 = 1024).
    // Returns 0 or 1.
    int decode_bit(uint16_t& prob) {
        normalize();
        uint32_t bound = (range_ >> 11) * prob;
        if (code_ < bound) {
            range_ = bound;
            prob += (0x800 - prob) >> 5;  // increase probability of 0
            return 0;
        } else {
            code_ -= bound;
            range_ -= bound;
            prob -= prob >> 5;  // decrease probability of 0 (increase 1)
            return 1;
        }
    }

    // Decode a bit-tree of numBits levels.
    // Returns a value in [0, (1 << numBits) - 1].
    uint32_t decode_bit_tree(uint16_t* probs, int num_bits) {
        uint32_t m = 1;
        for (int i = 0; i < num_bits; ++i) {
            m = (m << 1) + decode_bit(probs[m]);
        }
        return m - (1u << num_bits);
    }

    // Decode a literal byte using context modeling.
    // The context is derived from the previous byte (prev_byte) and
    // the match byte if this is a match (match_byte, 0 if literal).
    uint8_t decode_literal(uint8_t prev_byte, uint8_t match_byte, bool is_match) {
        // Probability table: 256 entries for literal context
        // Each entry is a bit-tree of 8 levels
        // Context: prev_byte >> 4 (high nibble) gives the table index
        uint16_t* probs = literal_probs_ + (prev_byte >> 4) * 256;
        uint8_t symbol = 0;
        // Match mode uses XOR-based context
        if (is_match) {
            symbol = decode_byte_with_match(probs, match_byte);
        } else {
            symbol = decode_byte_simple(probs);
        }
        return symbol;
    }

    // Check if we've consumed all input
    bool finished() const { return pos_ >= size_; }

    // Get current position in input
    size_t position() const { return pos_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
    uint32_t range_;
    uint32_t code_;

    // Probability tables (initialized to 0x400 = 1024)
    // Literal context: 16 contexts × 256 symbols × 8 bits
    uint16_t literal_probs_[16 * 256 * 8];

    void normalize() {
        if (range_ < 0x1000000) {
            range_ <<= 8;
            if (pos_ < size_) {
                code_ = (code_ << 8) | data_[pos_++];
            } else {
                code_ <<= 8;
            }
        }
    }

    uint8_t decode_byte_simple(uint16_t* probs) {
        uint32_t m = 1;
        for (int i = 0; i < 8; ++i) {
            m = (m << 1) + decode_bit(probs[m]);
        }
        return (uint8_t)(m - 256);
    }

    uint8_t decode_byte_with_match(uint16_t* probs, uint8_t match_byte) {
        uint32_t m = 1;
        for (int i = 0; i < 8; ++i) {
            uint32_t match_bit = (match_byte >> (7 - i)) & 1;
            uint32_t prob_idx = m + (match_bit << 8);
            uint32_t bit = decode_bit(probs[prob_idx]);
            m = (m << 1) + bit;
            if (bit != match_bit) {
                // Diverged from match — continue as simple bit-tree
                for (int j = i + 1; j < 8; ++j) {
                    m = (m << 1) + decode_bit(probs[m]);
                }
                break;
            }
        }
        return (uint8_t)(m - 256);
    }
};

// DZ (derbh) decompressor.
// This is a clean-room reimplementation based on algorithm analysis of
// libs3e_android.so. The algorithm is an LZMA-variant range coder.
class DzDecompressor {
public:
    // Decompress a DZ-compressed block.
    // compressed: pointer to compressed data
    // comp_size: size of compressed data
    // uncomp_size: expected decompressed size
    // Returns empty vector on failure.
    static std::vector<uint8_t> decompress(const uint8_t* compressed, size_t comp_size,
                                            size_t uncomp_size);

    // Decompress with streaming state (for multi-file archives).
    // The DZ format uses overlapping offsets — files share one continuous
    // compressed stream. To decompress file N, you must first decode files
    // 0..N-1.
    //
    // state: persistent decoder state (range, code, window, position)
    // compressed: pointer to the FULL compressed stream (from archive data section start)
    // offset: position in stream where this file's decoding starts
    // uncomp_size: expected decompressed size
    static std::vector<uint8_t> decompress_streaming(
        const uint8_t* compressed, size_t comp_size,
        size_t offset, size_t uncomp_size);

private:
    // Decoder state (persistent across files in streaming mode)
    struct DecoderState {
        uint32_t range = 0xFFFFFFFF;
        uint32_t code = 0;
        uint8_t window[5] = {0, 0, 0, 0, 0};
        int window_pos = 0;
        size_t input_pos = 0;
        bool initialized = false;
    };

    // CRC32 table (polynomial 0x04C11DB7, big-endian)
    static const uint32_t CRC_TABLE[256];

    // Compute context hash from 5-byte window
    static uint32_t context_hash(const uint8_t window[5]) {
        uint32_t crc = 0;
        for (int i = 1; i < 5; ++i) {
            crc = ((crc << 8) ^ CRC_TABLE[(crc >> 24) ^ window[i]]) & 0xFFFFFFFF;
        }
        return crc;
    }
};

}  // namespace resf2::dz
