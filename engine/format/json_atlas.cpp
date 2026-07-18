#include "json_atlas.hpp"
#include <charconv>
#include <fstream>

namespace resf2::format {

namespace {

struct JsonParser {
    std::string_view src;
    size_t pos = 0;

    explicit JsonParser(std::string_view s) : src(s) {}

    void skip_ws() {
        while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos;
    }

    bool match(char c) {
        skip_ws();
        if (pos < src.size() && src[pos] == c) { ++pos; return true; }
        return false;
    }

    bool match_str(std::string_view s) {
        skip_ws();
        if (pos + s.size() <= src.size() && src.substr(pos, s.size()) == s) {
            pos += s.size(); return true;
        }
        return false;
    }

    std::string parse_string() {
        skip_ws();
        if (pos >= src.size() || src[pos] != '"') return "";
        ++pos;
        size_t start = pos;
        while (pos < src.size() && src[pos] != '"') {
            if (src[pos] == '\\') { pos += 2; continue; }
            ++pos;
        }
        std::string result(src.substr(start, pos - start));
        if (pos < src.size() && src[pos] == '"') ++pos;
        return result;
    }

    int parse_int() {
        skip_ws();
        bool neg = false;
        if (pos < src.size() && src[pos] == '-') { neg = true; ++pos; }
        int val = 0;
        while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) {
            val = val * 10 + (src[pos++] - '0');
        }
        return neg ? -val : val;
    }

    float parse_float() {
        skip_ws();
        bool neg = false;
        if (pos < src.size() && src[pos] == '-') { neg = true; ++pos; }
        float val = 0.0f;
        while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) {
            val = val * 10.0f + (src[pos++] - '0');
        }
        if (pos < src.size() && src[pos] == '.') {
            ++pos;
            float div = 10.0f;
            while (pos < src.size() && std::isdigit(static_cast<unsigned char>(src[pos]))) {
                val += (src[pos++] - '0') / div;
                div *= 10.0f;
            }
        }
        return neg ? -val : val;
    }

    bool parse_object_start() { return match('{'); }
    bool parse_object_end() { return match('}'); }
    bool parse_array_start() { return match('['); }
    bool parse_array_end() { return match(']'); }
    void skip_value() {
        skip_ws();
        if (pos >= src.size()) return;
        char c = src[pos];
        if (c == '"') { parse_string(); }
        else if (c == '{') { match('{'); int depth = 1; while (depth > 0 && pos < src.size()) { if (src[pos] == '{') ++depth; else if (src[pos] == '}') --depth; ++pos; } }
        else if (c == '[') { match('['); int depth = 1; while (depth > 0 && pos < src.size()) { if (src[pos] == '[') ++depth; else if (src[pos] == ']') --depth; ++pos; } }
        else if (match_str("true") || match_str("false") || match_str("null")) {}
        else { while (pos < src.size() && !std::isspace(static_cast<unsigned char>(src[pos])) && src[pos] != ',' && src[pos] != '}' && src[pos] != ']') ++pos; }
    }
    bool parse_comma() { 
        skip_ws(); 
        if (pos < src.size() && src[pos] == ',') { 
            ++pos; 
            return true; 
        } 
        return false; 
    }
    bool parse_colon() { return match(':'); }
};

} // namespace

const char* to_string(JsonAtlasError e) noexcept {
    switch (e) {
        case JsonAtlasError::Ok: return "Ok";
        case JsonAtlasError::EmptyInput: return "EmptyInput";
        case JsonAtlasError::JsonParseFailed: return "JsonParseFailed";
        case JsonAtlasError::MissingFrames: return "MissingFrames";
        case JsonAtlasError::MissingMeta: return "MissingMeta";
        case JsonAtlasError::InvalidFrameData: return "InvalidFrameData";
    }
    return "Unknown";
}

std::expected<JsonAtlas, JsonAtlasError> parse_json_atlas(std::string_view json) {
    if (json.empty()) return std::unexpected(JsonAtlasError::EmptyInput);

    JsonParser p(json);
    p.skip_ws();
    if (!p.match('{')) return std::unexpected(JsonAtlasError::JsonParseFailed);

    JsonAtlas atlas;

    while (true) {
        p.skip_ws();
        if (p.match('}')) break;

        std::string key = p.parse_string();
        if (key.empty()) return std::unexpected(JsonAtlasError::JsonParseFailed);
        if (!p.parse_colon()) return std::unexpected(JsonAtlasError::JsonParseFailed);

if (key == "frames") {
            p.skip_ws();
            // Check if it's an array [ or object {
            bool is_array = false;
            if (p.match('[')) {
                is_array = true;
            } else if (p.match('{')) {
                is_array = false;
            } else {
                return std::unexpected(JsonAtlasError::JsonParseFailed);
            }
            if (is_array) {
                // Array format (sf2_pc)
                while (true) {
                    p.skip_ws();
                    if (p.match(']')) break;

                    if (!p.match('{')) return std::unexpected(JsonAtlasError::JsonParseFailed);

                    JsonAtlasFrame frame;

                    while (true) {
                        p.skip_ws();
                        if (p.match('}')) break;

                        std::string fkey = p.parse_string();
                        if (!p.parse_colon()) return std::unexpected(JsonAtlasError::InvalidFrameData);

                        if (fkey == "filename") {
                            frame.filename = p.parse_string();
                        }
                        else if (fkey == "rotated") {
                            p.skip_ws();
                            if (p.match_str("true")) frame.rotated = true;
                            else if (p.match_str("false")) frame.rotated = false;
                        }
                        else if (fkey == "frame") {
                            if (!p.parse_object_start()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                            while (true) {
                                p.skip_ws();
                                if (p.match('}')) break;
                                std::string sfk = p.parse_string();
                                if (!p.parse_colon()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                                if (sfk == "x") frame.x = p.parse_int();
                                else if (sfk == "y") frame.y = p.parse_int();
                                else if (sfk == "w") frame.w = p.parse_int();
                                else if (sfk == "h") frame.h = p.parse_int();
                                else p.parse_int();
                                p.parse_comma();
                            }
                        }
                        else if (fkey == "spriteSourceSize") {
                            if (!p.parse_object_start()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                            while (true) {
                                p.skip_ws();
                                if (p.match('}')) break;
                                std::string sfk = p.parse_string();
                                if (!p.parse_colon()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                                if (sfk == "x") frame.ox = p.parse_int();
                                else if (sfk == "y") frame.oy = p.parse_int();
                                else if (sfk == "w") frame.sw = p.parse_int();
                                else if (sfk == "h") frame.sh = p.parse_int();
                                else p.parse_int();
                                p.parse_comma();
                            }
                        }
                        else if (fkey == "sourceSize") {
                            if (!p.parse_object_start()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                            while (true) {
                                p.skip_ws();
                                if (p.match('}')) break;
                                std::string sfk = p.parse_string();
                                if (!p.parse_colon()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                                if (sfk == "w") frame.sw0 = p.parse_int();
                                else if (sfk == "h") frame.sh0 = p.parse_int();
                                else p.parse_int();
                                p.parse_comma();
                            }
                        }
                        else {
                            p.skip_value();
                        }
                        p.parse_comma();
                    }

                    if (frame.w > 0 && frame.h > 0) {
                        atlas.name_index[frame.filename] = atlas.frames.size();
                        atlas.frames.push_back(frame);
                    }
                    p.parse_comma();
                }
            }
            else {
                // Object format (TexturePacker dict)
                while (true) {
                    p.skip_ws();
                    if (p.match('}')) break;

                    std::string frame_name = p.parse_string();
                    if (!p.parse_colon()) return std::unexpected(JsonAtlasError::JsonParseFailed);
                    if (!p.parse_object_start()) return std::unexpected(JsonAtlasError::JsonParseFailed);

                    JsonAtlasFrame frame;
                    frame.filename = frame_name;

                    while (true) {
                        p.skip_ws();
                        if (p.match('}')) break;

                        std::string fkey = p.parse_string();
                        if (!p.parse_colon()) return std::unexpected(JsonAtlasError::JsonParseFailed);

                        if (fkey == "rotated") {
                            p.skip_ws();
                            if (p.match_str("true")) frame.rotated = true;
                            else if (p.match_str("false")) frame.rotated = false;
                        }
                        else if (fkey == "frame") {
                            if (!p.parse_object_start()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                            while (true) {
                                p.skip_ws();
                                if (p.match('}')) break;
                                std::string sfk = p.parse_string();
                                if (!p.parse_colon()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                                if (sfk == "x") frame.x = p.parse_int();
                                else if (sfk == "y") frame.y = p.parse_int();
                                else if (sfk == "w") frame.w = p.parse_int();
                                else if (sfk == "h") frame.h = p.parse_int();
                                else p.parse_int();
                                p.parse_comma();
                            }
                        }
                        else if (fkey == "spriteSourceSize") {
                            if (!p.parse_object_start()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                            while (true) {
                                p.skip_ws();
                                if (p.match('}')) break;
                                std::string sfk = p.parse_string();
                                if (!p.parse_colon()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                                if (sfk == "x") frame.ox = p.parse_int();
                                else if (sfk == "y") frame.oy = p.parse_int();
                                else if (sfk == "w") frame.sw = p.parse_int();
                                else if (sfk == "h") frame.sh = p.parse_int();
                                else p.parse_int();
                                p.parse_comma();
                            }
                        }
                        else if (fkey == "sourceSize") {
                            if (!p.parse_object_start()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                            while (true) {
                                p.skip_ws();
                                if (p.match('}')) break;
                                std::string sfk = p.parse_string();
                                if (!p.parse_colon()) return std::unexpected(JsonAtlasError::InvalidFrameData);
                                if (sfk == "w") frame.sw0 = p.parse_int();
                                else if (sfk == "h") frame.sh0 = p.parse_int();
                                else p.parse_int();
                                p.parse_comma();
                            }
                        }
                        else {
                            p.skip_value();
                        }
                        p.parse_comma();
                    }

                    if (frame.w > 0 && frame.h > 0) {
                        atlas.name_index[frame.filename] = atlas.frames.size();
                        atlas.frames.push_back(frame);
                    }
                    p.parse_comma();
                }
            }
        }
        else if (key == "meta") {
            if (!p.parse_object_start()) return std::unexpected(JsonAtlasError::JsonParseFailed);
            while (true) {
                p.skip_ws();
                if (p.match('}')) break;
                std::string mkey = p.parse_string();
                if (!p.parse_colon()) return std::unexpected(JsonAtlasError::JsonParseFailed);
                if (mkey == "image") atlas.meta.image = p.parse_string();
                else if (mkey == "size") {
                    if (!p.parse_object_start()) return std::unexpected(JsonAtlasError::JsonParseFailed);
                    while (true) {
                        p.skip_ws();
                        if (p.match('}')) break;
                        std::string smk = p.parse_string();
                        if (!p.parse_colon()) return std::unexpected(JsonAtlasError::JsonParseFailed);
                        if (smk == "w") atlas.meta.w = p.parse_int();
                        else if (smk == "h") atlas.meta.h = p.parse_int();
                        else p.parse_int();
                        p.parse_comma();
                    }
                }
                else if (mkey == "scale") {
                    auto s = p.parse_string();
                    if (!s.empty()) atlas.meta.scale = std::stof(s);
                }
                else {
                    p.skip_value();
                }
                p.parse_comma();
            }
        }
        else {
            p.skip_value();
        }
        p.parse_comma();
    }

    if (atlas.frames.empty()) return std::unexpected(JsonAtlasError::MissingFrames);
    if (atlas.meta.w == 0 || atlas.meta.h == 0) return std::unexpected(JsonAtlasError::MissingMeta);

    return atlas;
}

std::expected<JsonAtlas, JsonAtlasError> parse_json_atlas_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return std::unexpected(JsonAtlasError::JsonParseFailed);
    size_t sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string data(sz, '\0');
    f.read(data.data(), static_cast<std::streamsize>(sz));
    return parse_json_atlas(data);
}

} // namespace resf2::format