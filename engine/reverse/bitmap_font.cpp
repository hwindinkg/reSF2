// engine/reverse/bitmap_font.cpp
//
// Implementation of the AngelCode BMFont text format parser.

#include "bitmap_font.hpp"

#include <cctype>
#include <fstream>
#include <memory>
#include <sstream>

namespace resf2::reverse::font {

namespace {

// Parse a single "key=value" token from a line, advancing the position.
// Returns true if a key=value pair was found.
bool parse_token(std::string_view line, std::size_t& pos,
                 std::string& key, std::string& value) {
    // Skip whitespace
    while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
    if (pos >= line.size()) return false;

    // Read key until '='
    std::size_t eq = line.find('=', pos);
    if (eq == std::string_view::npos) return false;
    key = std::string(line.substr(pos, eq - pos));
    pos = eq + 1;

    // Read value: if it starts with '"', read until closing '"'.
    // Otherwise read until whitespace.
    if (pos < line.size() && line[pos] == '"') {
        ++pos;
        std::size_t end = line.find('"', pos);
        if (end == std::string_view::npos) return false;
        value = std::string(line.substr(pos, end - pos));
        pos = end + 1;
    } else {
        std::size_t start = pos;
        while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
        value = std::string(line.substr(start, pos - start));
    }
    return true;
}

// Parse all key=value tokens from a line (after the leading word like "info", "common", etc.)
std::unordered_map<std::string, std::string> parse_tokens(std::string_view line) {
    std::unordered_map<std::string, std::string> result;
    std::size_t pos = 0;
    // Skip the leading word
    while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;

    std::string key, value;
    while (parse_token(line, pos, key, value)) {
        result[key] = value;
    }
    return result;
}

int to_int(const std::string& s, int default_val = 0) {
    try { return std::stoi(s); } catch (...) { return default_val; }
}

bool to_bool(const std::string& s) {
    return s == "1" || s == "true" || s == "True";
}

void parse_padding(const std::string& s, std::int32_t out[4]) {
    // Format: "left,top,right,bottom"
    std::size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        std::size_t comma = s.find(',', start);
        std::string part = (comma == std::string::npos)
            ? s.substr(start)
            : s.substr(start, comma - start);
        out[i] = to_int(part);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
}

void parse_spacing(const std::string& s, std::int32_t out[2]) {
    std::size_t comma = s.find(',');
    out[0] = to_int(s.substr(0, comma));
    out[1] = (comma == std::string::npos) ? 0 : to_int(s.substr(comma + 1));
}

}  // namespace

const char* to_string(ParseError e) noexcept {
    switch (e) {
        case ParseError::kOk:                 return "ok";
        case ParseError::kInputEmpty:         return "input is empty";
        case ParseError::kMalformedLine:      return "malformed line";
        case ParseError::kMissingInfoLine:    return "missing info line";
        case ParseError::kMissingCommonLine:  return "missing common line";
    }
    return "unknown error";
}

auto parse(std::string_view text) -> std::expected<ParsedFont, ParseError> {
    if (text.empty()) return std::unexpected(ParseError::kInputEmpty);

    ParsedFont result;
    bool has_info = false;
    bool has_common = false;

    std::size_t line_start = 0;
    while (line_start < text.size()) {
        std::size_t line_end = text.find('\n', line_start);
        if (line_end == std::string_view::npos) line_end = text.size();
        std::string_view line = text.substr(line_start, line_end - line_start);
        line_start = line_end + 1;

        // Trim trailing \r
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty()) continue;

        // Determine line type by first word
        std::size_t word_end = 0;
        while (word_end < line.size() && !std::isspace(static_cast<unsigned char>(line[word_end]))) ++word_end;
        std::string_view type = line.substr(0, word_end);

        if (type == "info") {
            auto tokens = parse_tokens(line);
            result.info.face = tokens.count("face") ? tokens["face"] : "";
            result.info.size = to_int(tokens["size"]);
            result.info.bold = to_bool(tokens["bold"]);
            result.info.italic = to_bool(tokens["italic"]);
            result.info.charset = tokens.count("charset") ? tokens["charset"] : "";
            result.info.unicode = to_bool(tokens["unicode"]);
            result.info.stretch_h = tokens.count("stretchH") ? to_int(tokens["stretchH"]) : 100;
            result.info.smooth = to_bool(tokens["smooth"]);
            result.info.aa = to_int(tokens["aa"]);
            if (tokens.count("padding")) parse_padding(tokens["padding"], result.info.padding);
            if (tokens.count("spacing")) parse_spacing(tokens["spacing"], result.info.spacing);
            has_info = true;
        } else if (type == "common") {
            auto tokens = parse_tokens(line);
            result.common.line_height = to_int(tokens["lineHeight"]);
            result.common.base = to_int(tokens["base"]);
            result.common.scale_w = to_int(tokens["scaleW"]);
            result.common.scale_h = to_int(tokens["scaleH"]);
            result.common.pages = to_int(tokens["pages"]);
            result.common.packed = to_bool(tokens["packed"]);
            has_common = true;
        } else if (type == "page") {
            auto tokens = parse_tokens(line);
            Page p;
            p.id = to_int(tokens["id"]);
            p.file = tokens.count("file") ? tokens["file"] : "";
            result.pages.push_back(p);
        } else if (type == "chars") {
            // Just a count line: "chars count=N" -- nothing to do
        } else if (type == "char") {
            auto tokens = parse_tokens(line);
            Char c;
            c.id = to_int(tokens["id"]);
            c.x = to_int(tokens["x"]);
            c.y = to_int(tokens["y"]);
            c.width = to_int(tokens["width"]);
            c.height = to_int(tokens["height"]);
            c.xoffset = to_int(tokens["xoffset"]);
            c.yoffset = to_int(tokens["yoffset"]);
            c.xadvance = to_int(tokens["xadvance"]);
            c.page = to_int(tokens["page"]);
            c.chnl = to_int(tokens["chnl"]);
            result.char_index[c.id] = result.chars.size();
            result.chars.push_back(c);
        } else if (type == "kernings") {
            // count line
        } else if (type == "kerning") {
            auto tokens = parse_tokens(line);
            Kerning k;
            k.first = to_int(tokens["first"]);
            k.second = to_int(tokens["second"]);
            k.amount = to_int(tokens["amount"]);
            std::uint64_t key = (static_cast<std::uint64_t>(k.first) << 32)
                              | static_cast<std::uint32_t>(k.second);
            result.kerning_index[key] = k.amount;
            result.kernings.push_back(k);
        }
        // Unknown line types are silently skipped
    }

    if (!has_info)   return std::unexpected(ParseError::kMissingInfoLine);
    if (!has_common) return std::unexpected(ParseError::kMissingCommonLine);
    return result;
}

auto parse_file(const std::string& path)
    -> std::expected<std::pair<std::shared_ptr<std::string>, ParsedFont>, ParseError> {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return std::unexpected(ParseError::kInputEmpty);
    auto size = static_cast<std::size_t>(f.tellg());
    if (size == 0) return std::unexpected(ParseError::kInputEmpty);
    f.seekg(0);
    auto buffer = std::make_shared<std::string>(size, '\0');
    f.read(buffer->data(), static_cast<std::streamsize>(size));
    if (!f) return std::unexpected(ParseError::kInputEmpty);
    auto parsed = parse(*buffer);
    if (!parsed) return std::unexpected(parsed.error());
    return std::make_pair(buffer, std::move(*parsed));
}

std::int32_t kerning_amount(const ParsedFont& font,
                            std::int32_t first,
                            std::int32_t second) noexcept {
    std::uint64_t key = (static_cast<std::uint64_t>(first) << 32)
                      | static_cast<std::uint32_t>(second);
    auto it = font.kerning_index.find(key);
    return it != font.kerning_index.end() ? it->second : 0;
}

}  // namespace resf2::reverse::font
