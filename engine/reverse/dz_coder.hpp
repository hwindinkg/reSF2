// engine/reverse/dz_coder.hpp
//
// Marmalade Derbh "DZ Coder" (coder mask 0x0004) — decoder.
//
// [ORIGINAL] Reversed from dzip.exe (Marmalade "Derbh commpress tool"):
//   DZCoder::init        FUN_00409d90   (reads the 10-byte coder header)
//   DZCoder::openEntry   FUN_00409f50   (reset models, init range decoder)
//   DZCoder::readChunk   FUN_0040a3f0 → FUN_0040a2f0  (main symbol loop)
//   decodeMatch          FUN_004097e0   (offset symbol + LZ77 copy)
//   RangeDecoder::init   FUN_00408810
//   RangeDecoder::decode FUN_00408e70
//   Model::init          FUN_00408fe0
//   Model::rescale       FUN_004087b0 / FUN_004088a0
//
// The same class is embedded in the game binaries at DzipFile+0xa0
// (ShadowFight2.s86 FUN_102c9778 registers it via Derbh::addCoder).
//
// Algorithm: carry-less (Subbotin) range decoder driving adaptive
// frequency models held in complete binary trees. Alphabet of the main
// model is 0x202 symbols:
//   0x000..0x0FF  literal byte
//   0x100..0x200  match, length = sym - 0xFE  (2..258)
//   0x201         end of block
//   >= 0x202      cross-entry reference (only present when some entry
//                 carries flag bit 0; not used by Shadow Fight 2 archives)

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace resf2::dz {

// [ORIGINAL] DZ coder settings, stored as 10 raw bytes in the archive
// directly after the entry table (dzip.exe FUN_00417aa0 registers these
// as coder parameters 0x14..0x1d; FUN_00409d90 reads them back).
struct DzCoderParams {
    uint8_t win_size = 0x10;              // WinSize
    uint8_t flags = 0x01;                 // Flags
    uint8_t offset_table_size = 0x08;     // OffsetTableSize
    uint8_t offset_tables = 0x03;         // OffsetTables
    uint8_t offset_contexts = 0x03;       // OffsetContexts
    uint8_t ref_length_table_size = 0x07; // RefLengthTableSize
    uint8_t ref_length_tables = 0x01;     // RefLengthTables
    uint8_t ref_offset_table_size = 0x07; // RefOffsetTableSize
    uint8_t ref_offset_tables = 0x03;     // RefOffsetTables
    uint8_t big_min_match = 0x0F;         // BigMinMatch

    static constexpr size_t kHeaderSize = 10;

    // Returns false if the header fails the validity test the original
    // performs in FUN_00409d90 (WinSize < 31, Flags < 4, OffsetContexts <= 8).
    static bool parse(const uint8_t* header, size_t size, DzCoderParams& out);
};

// Decode a single archive block.
//
// `in` points at the block's first byte (entry.offset in the .dz file) and
// `out_size` is the entry's uncompressed size. Returns an empty vector if
// the stream is malformed or requires the unimplemented cross-entry
// reference path.
//
// The original decodes in windows of max(MinBufSize, 1 << WinSize) bytes and
// resolves back-references that reach into the previous window through
// DZCoder+0x40. Decoding the whole block into one contiguous buffer produces
// the identical byte stream — the windowing is a memory bound only.
std::vector<uint8_t> dz_decode_block(const DzCoderParams& params,
                                     const uint8_t* in, size_t in_size,
                                     size_t out_size);

}  // namespace resf2::dz
