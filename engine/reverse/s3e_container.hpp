// engine/reverse/s3e_container.hpp
//
// Read-only parser for the Marmalade S3E binary container format.
//
// The .s3e file shipped inside the Shadow Fight 2 APK is:
//   1. An LZMA1 legacy wrapper (props + dict_size + uncomp_size + stream)
//   2. After decompression, a Marmalade S3E binary with magic "XE3U"
//
// This header exposes the second-level parser. LZMA1 decompression is the
// caller's responsibility — see s3e_lzma.hpp for a helper (Stage 4).
//
// Stage 2 scope: header + embedded config + import-name table.
// Stage 4 will extend this with relocation + section-table parsing.
//
// Requires C++23 for std::expected.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace resf2::reverse::s3e {

// Magic at offset 0 of the decompressed S3E payload.
inline constexpr std::array<std::byte, 4> kMagic{
    std::byte{'X'}, std::byte{'E'}, std::byte{'3'}, std::byte{'U'}};

// Header is exactly 76 bytes (0x4c). The embedded config text starts
// immediately after, at offset 0x4c.
inline constexpr std::size_t kHeaderSize = 0x4c;

// Error codes returned by the parser.
enum class ParseError {
    kOk = 0,
    kInputEmpty,
    kInputTooSmall,        // < kHeaderSize
    kBadMagic,             // first 4 bytes != "XE3U"
    kBadConfigOffset,      // header.config_offset < kHeaderSize or > size
    kBadConfigLength,      // config extends past end of buffer
    kBadImportTableOffset, // header.import_table_offset past end
    kBadImportTableLength, // import table extends past end
};

// Convert a ParseError to a human-readable string.
[[nodiscard]] const char* to_string(ParseError e) noexcept;

// Parsed view of the 76-byte S3E header.
//
// Field names follow the byte offsets observed in ShadowFight2.bin. The
// semantic meaning of several fields is still a hypothesis (see
// docs/09_s3e_binary_format.md for the full reasoning).
struct Header {
    std::array<std::byte, 4> magic;          // 0x00  "XE3U"
    std::uint32_t u32_04;                    // 0x04  272384 -- code size?
    std::uint32_t u32_08;                    // 0x08  0x010c000a -- version/flags
    std::uint32_t import_table_start;       // 0x0c  0x1521 -- start of import table (= end of config)
    std::uint32_t u32_10;                    // 0x10  0x43d30 -- reloc table end?
    std::uint32_t u32_14;                    // 0x14  0x45251 -- next section end
    std::uint32_t vaddr_18;                  // 0x18  0x8042c8 -- load vaddr
    std::uint32_t vaddr_1c;                  // 0x1c  0x825d5c -- load vaddr
    std::uint32_t vaddr_20;                  // 0x20  0x849519 -- load vaddr (near EOF)
    std::uint32_t u32_24;                    // 0x24  140
    std::uint32_t u32_28;                    // 0x28  0
    std::uint32_t config_offset;             // 0x2c  0x4c (= kHeaderSize)
    std::uint32_t config_length;             // 0x30  5333
    std::uint32_t u32_34;                    // 0x34  flags
    std::uint32_t vaddr_38;                  // 0x38  load vaddr (near EOF)
    std::uint32_t u32_3c;                    // 0x3c  296 -- final section size?
    std::uint32_t u32_40;                    // 0x40  12
    std::uint32_t vaddr_44;                  // 0x44  0x7b8000 -- GOT vaddr?
    std::uint32_t u32_48;                    // 0x48  0
};

static_assert(sizeof(Header) == kHeaderSize,
              "Header must be exactly 76 bytes (0x4c)");

// One entry in the import-name table -- a null-terminated ASCII identifier
// that the S3E binary expects the loader to resolve at load time.
struct ImportEntry {
    std::uint32_t offset;   // file offset of the name string
    std::string   name;     // the name (without trailing NUL)
};

// Result of a successful parse.
struct ParsedFile {
    Header                   header;
    std::string_view         config_text;     // embedded Marmalade .icf text
    std::vector<ImportEntry> imports;         // function names from import table
    std::span<const std::byte> raw;           // the entire input buffer
};

// Parse a decompressed S3E payload.
//
// `data` must point to the decompressed bytes (i.e. the LZMA output, not
// the raw .s3e file). The parser does not copy data -- `ParsedFile::raw`
// and `ParsedFile::config_text` are views into the input. The caller must
// keep the input alive for the lifetime of the ParsedFile.
//
// On success returns a ParsedFile. On failure returns a ParseError; no
// ParsedFile is produced.
[[nodiscard]] auto parse(std::span<const std::byte> data)
    -> std::expected<ParsedFile, ParseError>;

// Convenience overload: parse from a file on disk. Reads the file into
// memory, then calls parse(). The returned ParsedFile keeps the buffer
// alive via the shared_ptr.
[[nodiscard]] auto parse_file(const std::string& path)
    -> std::expected<std::pair<std::shared_ptr<std::vector<std::byte>>, ParsedFile>,
                     ParseError>;

// Dump a human-readable summary to a string (for debugging / RE tools).
[[nodiscard]] std::string dump_summary(const ParsedFile& f);

}  // namespace resf2::reverse::s3e
