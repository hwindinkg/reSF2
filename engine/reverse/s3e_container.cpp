// engine/reverse/s3e_container.cpp
//
// Implementation of the Marmalade S3E container parser.
// See s3e_container.hpp for the public API and docs/09_s3e_binary_format.md
// for the format specification.

#include "s3e_container.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <unordered_set>

namespace resf2::reverse::s3e {

namespace {

// Read a little-endian uint32 from a byte span at the given offset.
// Bounds-checked: returns 0 if the read would go out of bounds.
[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::byte> data,
                                        std::size_t offset) noexcept {
    if (offset + 4 > data.size()) return 0;
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset]))
         | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 1])) << 8)
         | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 2])) << 16)
         | (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(data[offset + 3])) << 24);
}

// Check that an ASCII byte is a valid C-identifier character.
[[nodiscard]] bool is_ident_char(std::byte b) noexcept {
    const auto c = std::to_integer<unsigned char>(b);
    return (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z')
        || (c >= '0' && c <= '9')
        || c == '_'
        || c == ':';
}

[[nodiscard]] bool is_ident_start(std::byte b) noexcept {
    const auto c = std::to_integer<unsigned char>(b);
    return (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z')
        || c == '_';
}

// Parse the 76-byte header into a Header struct.
[[nodiscard]] Header parse_header(std::span<const std::byte> data) noexcept {
    Header h{};
    std::memcpy(h.magic.data(), data.data(), 4);
    h.u32_04            = read_u32_le(data, 0x04);
    h.u32_08            = read_u32_le(data, 0x08);
    h.import_table_start = read_u32_le(data, 0x0c);
    h.u32_10            = read_u32_le(data, 0x10);
    h.u32_14            = read_u32_le(data, 0x14);
    h.vaddr_18          = read_u32_le(data, 0x18);
    h.vaddr_1c          = read_u32_le(data, 0x1c);
    h.vaddr_20          = read_u32_le(data, 0x20);
    h.u32_24            = read_u32_le(data, 0x24);
    h.u32_28            = read_u32_le(data, 0x28);
    h.config_offset     = read_u32_le(data, 0x2c);
    h.config_length     = read_u32_le(data, 0x30);
    h.u32_34            = read_u32_le(data, 0x34);
    h.vaddr_38          = read_u32_le(data, 0x38);
    h.u32_3c            = read_u32_le(data, 0x3c);
    h.u32_40            = read_u32_le(data, 0x40);
    h.vaddr_44          = read_u32_le(data, 0x44);
    h.u32_48            = read_u32_le(data, 0x48);
    return h;
}

// Extract null-terminated ASCII identifier strings from [start, end).
//
// The import-name table is a sequence of null-terminated strings. Some
// entries are not valid C identifiers (binary garbage from a different
// section that happens to be adjacent); we filter those out. We also
// deduplicate by offset so that the same name appearing twice in the
// table is preserved (the loader may resolve them to different slots).
[[nodiscard]] std::vector<ImportEntry>
extract_imports(std::span<const std::byte> data,
                std::uint32_t start,
                std::uint32_t end) {
    std::vector<ImportEntry> result;
    if (start >= data.size() || end > data.size() || start >= end) {
        return result;
    }

    std::size_t off = start;
    while (off < end) {
        // Skip non-identifier bytes (NULs and binary garbage).
        while (off < end && !is_ident_start(data[off])) {
            ++off;
        }
        if (off >= end) break;

        // Read the identifier.
        const std::size_t name_start = off;
        while (off < end && is_ident_char(data[off])) {
            ++off;
        }
        const std::size_t name_len = off - name_start;

        // Reject too-short or implausibly-long names.
        if (name_len >= 3 && name_len <= 64) {
            std::string name(reinterpret_cast<const char*>(data.data() + name_start),
                             name_len);
            result.push_back(ImportEntry{
                .offset = static_cast<std::uint32_t>(name_start),
                .name   = std::move(name),
            });
        }

        // Skip the NUL terminator (or whatever non-ident byte stopped us).
        if (off < end) ++off;
    }
    return result;
}

}  // namespace

const char* to_string(ParseError e) noexcept {
    switch (e) {
        case ParseError::kOk:                   return "ok";
        case ParseError::kInputEmpty:           return "input is empty";
        case ParseError::kInputTooSmall:        return "input smaller than 76-byte header";
        case ParseError::kBadMagic:             return "bad magic (expected 'XE3U')";
        case ParseError::kBadConfigOffset:      return "config offset out of bounds";
        case ParseError::kBadConfigLength:      return "config length out of bounds";
        case ParseError::kBadImportTableOffset: return "import table offset out of bounds";
        case ParseError::kBadImportTableLength: return "import table length out of bounds";
    }
    return "unknown error";
}

auto parse(std::span<const std::byte> data)
    -> std::expected<ParsedFile, ParseError> {

    if (data.empty()) {
        return std::unexpected(ParseError::kInputEmpty);
    }
    if (data.size() < kHeaderSize) {
        return std::unexpected(ParseError::kInputTooSmall);
    }

    // Verify magic.
    if (std::memcmp(data.data(), kMagic.data(), 4) != 0) {
        return std::unexpected(ParseError::kBadMagic);
    }

    Header h = parse_header(data);

    // Validate config section.
    if (h.config_offset < kHeaderSize || h.config_offset > data.size()) {
        return std::unexpected(ParseError::kBadConfigOffset);
    }
    if (h.config_length > data.size() - h.config_offset) {
        return std::unexpected(ParseError::kBadConfigLength);
    }

    // The import-name table starts where the config text ends
    // (header.import_table_start at offset 0x0c == config_offset +
    // config_length). It is followed by an 8-byte preamble (all zeros
    // in ShadowFight2.bin) and then the first null-terminated name.
    //
    // The header does NOT record an explicit end-of-import-table field.
    // The table is followed by a binary relocation section that runs up
    // to roughly u32_10 (0x43d30 in ShadowFight2.bin). extract_imports()
    // naturally stops scanning when it encounters non-identifier bytes,
    // so we pass u32_10 as the upper bound.
    const std::uint32_t table_start = h.config_offset + h.config_length;
    const std::uint32_t table_end   = h.u32_10;  // generous upper bound

    if (table_start > data.size() || table_end > data.size()
        || table_start > table_end) {
        return std::unexpected(ParseError::kBadImportTableOffset);
    }

    // Skip the 8-byte preamble that sits between config text and the
    // first name string. (In ShadowFight2.bin: bytes 0x1521..0x1529 are
    // 00 00 00 00 a0 16 00 00.)
    constexpr std::uint32_t kPreambleSize = 8;
    std::uint32_t names_start = table_start + kPreambleSize;
    if (names_start > table_end) {
        names_start = table_start;  // be lenient on misformed files
    }

    auto imports = extract_imports(data, names_start, table_end);

    std::string_view config_text(
        reinterpret_cast<const char*>(data.data() + h.config_offset),
        h.config_length);

    ParsedFile f{
        .header       = h,
        .config_text  = config_text,
        .imports      = std::move(imports),
        .raw          = data,
    };
    return f;
}

auto parse_file(const std::string& path)
    -> std::expected<std::pair<std::shared_ptr<std::vector<std::byte>>, ParsedFile>,
                     ParseError> {

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        return std::unexpected(ParseError::kInputEmpty);
    }
    const auto size = static_cast<std::size_t>(f.tellg());
    if (size == 0) {
        return std::unexpected(ParseError::kInputEmpty);
    }
    f.seekg(0);

    auto buffer = std::make_shared<std::vector<std::byte>>(size);
    f.read(reinterpret_cast<char*>(buffer->data()), static_cast<std::streamsize>(size));
    if (!f) {
        return std::unexpected(ParseError::kInputEmpty);
    }

    auto parsed = parse(std::span<const std::byte>(*buffer));
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    return std::make_pair(buffer, std::move(*parsed));
}

std::string dump_summary(const ParsedFile& f) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "S3E file summary\n"
        "  magic            : %c%c%c%c\n"
        "  file size        : %zu bytes\n"
        "  u32_04           : 0x%08x (%u)\n"
        "  u32_08           : 0x%08x\n"
        "  import_table_start : 0x%08x\n"
        "  u32_10           : 0x%08x\n"
        "  u32_14           : 0x%08x\n"
        "  vaddr_18         : 0x%08x\n"
        "  vaddr_1c         : 0x%08x\n"
        "  vaddr_20         : 0x%08x\n"
        "  config_offset    : 0x%08x\n"
        "  config_length    : 0x%08x (%u)\n"
        "  vaddr_38         : 0x%08x\n"
        "  u32_3c           : 0x%08x (%u)\n"
        "  vaddr_44         : 0x%08x\n"
        "  imports          : %zu entries\n",
        std::to_integer<char>(f.header.magic[0]),
        std::to_integer<char>(f.header.magic[1]),
        std::to_integer<char>(f.header.magic[2]),
        std::to_integer<char>(f.header.magic[3]),
        f.raw.size(),
        f.header.u32_04, f.header.u32_04,
        f.header.u32_08,
        f.header.import_table_start,
        f.header.u32_10,
        f.header.u32_14,
        f.header.vaddr_18,
        f.header.vaddr_1c,
        f.header.vaddr_20,
        f.header.config_offset,
        f.header.config_length, f.header.config_length,
        f.header.vaddr_38,
        f.header.u32_3c, f.header.u32_3c,
        f.header.vaddr_44,
        f.imports.size());
    return std::string(buf);
}

}  // namespace resf2::reverse::s3e
