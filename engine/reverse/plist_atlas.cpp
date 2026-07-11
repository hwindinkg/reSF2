// engine/reverse/plist_atlas.cpp
//
// Implementation of the Cocos2d-x TexturePacker v2 plist atlas parser.
// Uses a minimal hand-written XML parser (no external dependencies) so
// the engine has zero third-party requirements at this stage.

#include "plist_atlas.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>

namespace resf2::reverse::plist {

namespace {

// ---------- Minimal XML parser ----------
// We only need to handle the subset of XML that plist files use:
//   <plist><dict>
//     <key>frames</key>
//     <dict>
//       <key>sprite_name.png</key>
//       <dict>
//         <key>frame</key>          <string>{{x,y},{w,h}}</string>
//         <key>offset</key>         <string>{dx,dy}</string>
//         <key>rotated</key>        <true/> | <false/>
//         <key>sourceColorRect</key><string>{{sx,sy},{sw,sh}}</string>
//         <key>sourceSize</key>     <string>{srcw,srch}</string>
//       </dict>
//       ...
//     </dict>
//     <key>metadata</key>
//     <dict>
//       <key>format</key>            <integer>2</integer>
//       <key>realTextureFileName</key><string>...</string>
//       <key>size</key>              <string>{w,h}</string>
//       <key>smartupdate</key>       <string>...</string>
//       <key>textureFileName</key>   <string>...</string>
//     </dict>
//   </dict></plist>

struct XmlParser {
    std::string_view src;
    std::size_t pos = 0;

    explicit XmlParser(std::string_view s) : src(s) {}

    void skip_ws() {
        while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) {
            ++pos;
        }
    }

    // Skip <?xml ... ?> and <!DOCTYPE ... > declarations
    void skip_declarations() {
        skip_ws();
        while (pos < src.size() && src[pos] == '<') {
            if (pos + 1 < src.size() && (src[pos+1] == '?' || src[pos+1] == '!')) {
                // Skip until '>'
                std::size_t end = src.find('>', pos);
                if (end == std::string_view::npos) return;
                pos = end + 1;
                skip_ws();
            } else {
                break;
            }
        }
    }

    // Try to match a specific opening tag like "<dict>". Returns true if matched.
    bool match_open_tag(std::string_view tag) {
        skip_ws();
        if (pos + tag.size() + 2 > src.size()) return false;
        if (src[pos] != '<') return false;
        std::string_view actual = src.substr(pos + 1, tag.size());
        if (actual != tag) return false;
        // Must be followed by '>' or whitespace
        char after = src[pos + 1 + tag.size()];
        if (after == '>') {
            pos += 1 + tag.size() + 1;
            return true;
        }
        if (std::isspace(static_cast<unsigned char>(after))) {
            // Skip attributes until '>'
            std::size_t end = src.find('>', pos);
            if (end == std::string_view::npos) return false;
            pos = end + 1;
            return true;
        }
        return false;
    }

    // Try to match a self-closing tag like "<true/>" or "<false/>"
    bool match_self_closing(std::string_view tag) {
        skip_ws();
        if (pos + tag.size() + 3 > src.size()) return false;
        if (src[pos] != '<') return false;
        std::string_view actual = src.substr(pos + 1, tag.size());
        if (actual != tag) return false;
        // Followed by "/>"
        if (src[pos + 1 + tag.size()] == '/' && src[pos + 1 + tag.size() + 1] == '>') {
            pos += 1 + tag.size() + 2;
            return true;
        }
        return false;
    }

    // Try to match a closing tag like "</dict>"
    bool match_close_tag(std::string_view tag) {
        skip_ws();
        if (pos + tag.size() + 3 > src.size()) return false;
        if (src[pos] != '<' || src[pos+1] != '/') return false;
        std::string_view actual = src.substr(pos + 2, tag.size());
        if (actual != tag) return false;
        if (src[pos + 2 + tag.size()] != '>') return false;
        pos += 2 + tag.size() + 1;
        return true;
    }

    // Try to match <key>NAME</key> and capture NAME.
    bool match_key(std::string& out) {
        skip_ws();
        if (!match_open_tag("key")) return false;
        std::size_t end = src.find("</key>", pos);
        if (end == std::string_view::npos) return false;
        out = std::string(src.substr(pos, end - pos));
        pos = end + 6; // skip "</key>"
        return true;
    }

    // Try to match <string>VALUE</string> and capture VALUE.
    bool match_string(std::string& out) {
        skip_ws();
        if (!match_open_tag("string")) return false;
        std::size_t end = src.find("</string>", pos);
        if (end == std::string_view::npos) return false;
        out = std::string(src.substr(pos, end - pos));
        pos = end + 9; // skip "</string>"
        return true;
    }

    // Try to match <integer>VALUE</integer> and capture the integer.
    bool match_integer(long& out) {
        skip_ws();
        if (!match_open_tag("integer")) return false;
        std::size_t end = src.find("</integer>", pos);
        if (end == std::string_view::npos) return false;
        std::string num_str(src.substr(pos, end - pos));
        pos = end + 10;
        try {
            out = std::stol(num_str);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool at_eof() const { return pos >= src.size(); }
};

// ---------- Geometry string parser ----------
// Parses "{x,y}"         -> sets x, y
// Parses "{{x,y},{w,h}}" -> sets all four
//
// Returns false if the string is malformed.
[[nodiscard]] bool parse_point(std::string_view s, std::int32_t& x, std::int32_t& y) {
    // Expect "{x,y}"
    if (s.size() < 5 || s.front() != '{' || s.back() != '}') return false;
    std::string_view inner = s.substr(1, s.size() - 2);
    std::size_t comma = inner.find(',');
    if (comma == std::string_view::npos) return false;
    try {
        x = static_cast<std::int32_t>(std::stol(std::string(inner.substr(0, comma))));
        y = static_cast<std::int32_t>(std::stol(std::string(inner.substr(comma + 1))));
        return true;
    } catch (...) {
        return false;
    }
}

[[nodiscard]] bool parse_rect(std::string_view s,
                              std::int32_t& x, std::int32_t& y,
                              std::int32_t& w, std::int32_t& h) {
    // Expect "{{x,y},{w,h}}"
    if (s.size() < 11 || s.front() != '{' || s.back() != '}') return false;
    std::string_view inner = s.substr(1, s.size() - 2);
    // inner = "{x,y},{w,h}"
    if (inner.front() != '{') return false;
    std::size_t close1 = inner.find('}');
    if (close1 == std::string_view::npos) return false;
    std::string_view part1 = inner.substr(0, close1 + 1);  // "{x,y}"
    if (close1 + 2 >= inner.size() || inner[close1 + 1] != ',') return false;
    std::string_view part2 = inner.substr(close1 + 2);     // "{w,h}"
    if (!parse_point(part1, x, y)) return false;
    if (!parse_point(part2, w, h)) return false;
    return true;
}

// Parse a single frame dict.
// The parser position must be just after "<dict>".
// Stops after consuming "</dict>".
[[nodiscard]] ParseError parse_frame_dict(XmlParser& p, Frame& out) {
    while (true) {
        p.skip_ws();
        if (p.match_close_tag("dict")) return ParseError::kOk;
        std::string key;
        if (!p.match_key(key)) return ParseError::kXmlParseFailed;
        if (key == "frame") {
            std::string v;
            if (!p.match_string(v)) return ParseError::kXmlParseFailed;
            if (!parse_rect(v, out.atlas_x, out.atlas_y, out.atlas_w, out.atlas_h)) {
                return ParseError::kMalformedGeometry;
            }
        } else if (key == "offset") {
            std::string v;
            if (!p.match_string(v)) return ParseError::kXmlParseFailed;
            if (!parse_point(v, out.offset_x, out.offset_y)) {
                return ParseError::kMalformedGeometry;
            }
        } else if (key == "rotated") {
            if (p.match_self_closing("true")) {
                out.rotated = true;
            } else if (p.match_self_closing("false")) {
                out.rotated = false;
            } else {
                return ParseError::kXmlParseFailed;
            }
        } else if (key == "sourceColorRect") {
            std::string v;
            if (!p.match_string(v)) return ParseError::kXmlParseFailed;
            if (!parse_rect(v, out.source_x, out.source_y, out.source_w, out.source_h)) {
                return ParseError::kMalformedGeometry;
            }
        } else if (key == "sourceSize") {
            std::string v;
            if (!p.match_string(v)) return ParseError::kXmlParseFailed;
            if (!parse_point(v, out.source_size_w, out.source_size_h)) {
                return ParseError::kMalformedGeometry;
            }
        } else {
            // Unknown key — skip its value
            std::string tmp;
            long tmp_i;
            if (p.match_string(tmp)) continue;
            if (p.match_integer(tmp_i)) continue;
            if (p.match_self_closing("true") || p.match_self_closing("false")) continue;
            if (p.match_open_tag("dict")) {
                // Skip nested dict by counting opens/closes
                int depth = 1;
                while (depth > 0 && !p.at_eof()) {
                    p.skip_ws();
                    if (p.match_open_tag("dict")) { ++depth; continue; }
                    if (p.match_close_tag("dict")) { --depth; continue; }
                    // Skip anything else
                    if (p.pos < p.src.size()) ++p.pos;
                }
                continue;
            }
            // Skip unknown tag
            if (p.pos < p.src.size()) ++p.pos;
        }
    }
}

}  // namespace

const char* to_string(ParseError e) noexcept {
    switch (e) {
        case ParseError::kOk:                 return "ok";
        case ParseError::kInputEmpty:         return "input is empty";
        case ParseError::kXmlParseFailed:     return "XML parse failed";
        case ParseError::kNotPlistFormat:     return "not a plist format";
        case ParseError::kUnsupportedFormat:  return "unsupported plist format (only v2 supported)";
        case ParseError::kMalformedGeometry:  return "malformed geometry string";
        case ParseError::kMissingMetadata:    return "missing metadata block";
    }
    return "unknown error";
}

auto parse(std::string_view xml) -> std::expected<ParsedAtlas, ParseError> {
    if (xml.empty()) {
        return std::unexpected(ParseError::kInputEmpty);
    }

    XmlParser p(xml);
    p.skip_declarations();

    if (!p.match_open_tag("plist")) return std::unexpected(ParseError::kNotPlistFormat);
    if (!p.match_open_tag("dict"))  return std::unexpected(ParseError::kNotPlistFormat);

    ParsedAtlas result;

    while (true) {
        p.skip_ws();
        if (p.match_close_tag("dict")) break;  // end of outer dict

        std::string key;
        if (!p.match_key(key)) return std::unexpected(ParseError::kXmlParseFailed);

        if (key == "frames") {
            if (!p.match_open_tag("dict")) return std::unexpected(ParseError::kXmlParseFailed);
            while (true) {
                p.skip_ws();
                if (p.match_close_tag("dict")) break;
                std::string frame_name;
                if (!p.match_key(frame_name)) return std::unexpected(ParseError::kXmlParseFailed);
                if (!p.match_open_tag("dict")) return std::unexpected(ParseError::kXmlParseFailed);
                Frame f;
                f.name = frame_name;
                ParseError e = parse_frame_dict(p, f);
                if (e != ParseError::kOk) return std::unexpected(e);
                result.name_index[f.name] = result.frames.size();
                result.frames.push_back(std::move(f));
            }
        } else if (key == "metadata") {
            if (!p.match_open_tag("dict")) return std::unexpected(ParseError::kXmlParseFailed);
            while (true) {
                p.skip_ws();
                if (p.match_close_tag("dict")) break;
                std::string mkey;
                if (!p.match_key(mkey)) return std::unexpected(ParseError::kXmlParseFailed);
                if (mkey == "format") {
                    long v;
                    if (!p.match_integer(v)) return std::unexpected(ParseError::kXmlParseFailed);
                    result.metadata.format = static_cast<std::int32_t>(v);
                } else if (mkey == "realTextureFileName") {
                    std::string v;
                    if (!p.match_string(v)) return std::unexpected(ParseError::kXmlParseFailed);
                    result.metadata.real_texture_filename = v;
                } else if (mkey == "size") {
                    std::string v;
                    if (!p.match_string(v)) return std::unexpected(ParseError::kXmlParseFailed);
                    if (!parse_point(v, result.metadata.texture_w, result.metadata.texture_h)) {
                        return std::unexpected(ParseError::kMalformedGeometry);
                    }
                } else if (mkey == "smartupdate") {
                    std::string v;
                    if (!p.match_string(v)) return std::unexpected(ParseError::kXmlParseFailed);
                    result.metadata.smart_update = v;
                } else if (mkey == "textureFileName") {
                    std::string v;
                    if (!p.match_string(v)) return std::unexpected(ParseError::kXmlParseFailed);
                    result.metadata.texture_filename = v;
                } else {
                    // Skip unknown metadata key
                    std::string tmp;
                    long tmp_i;
                    if (p.match_string(tmp)) continue;
                    if (p.match_integer(tmp_i)) continue;
                    if (p.pos < p.src.size()) ++p.pos;
                }
            }
        } else {
            // Skip unknown top-level key
            std::string tmp;
            if (p.match_string(tmp)) continue;
            if (p.pos < p.src.size()) ++p.pos;
        }
    }

    if (result.metadata.format != 2) {
        return std::unexpected(ParseError::kUnsupportedFormat);
    }
    return result;
}

auto parse_file(const std::string& path)
    -> std::expected<std::pair<std::shared_ptr<std::string>, ParsedAtlas>, ParseError> {
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

}  // namespace resf2::reverse::plist
