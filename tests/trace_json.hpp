// tests/trace_json.hpp
//
// Minimal, dependency-free JSON reader for trace files.
//
// Supports exactly the subset of JSON that reverse/traces/*.json uses:
//   - objects, arrays, strings, numbers (int + float), bool, null
//   - nested to arbitrary depth
//   - UTF-8 strings (escape sequences: \", \\, \/, \b, \f, \n, \r, \t, \uXXXX)
//   - line comments (// ... ) — non-standard but convenient for trace files
//
// Does NOT support:
//   - writing / serialising (not needed — we only READ traces)
//   - schema validation (the trace format is documented; misuse is a bug)
//
// This header exists because pulling nlohmann/json into the test tree would
// be overkill for a 200-line consumer. If the project ever adopts a proper
// JSON library, this header can be replaced wholesale.

#pragma once

#include <cctype>
#include <cmath>
#include <cstdint>
#include <expected>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace resf2::test::json {

// ---------------------------------------------------------------------------
// Value type
// ---------------------------------------------------------------------------

class Value;
using Object = std::map<std::string, Value>;
using Array  = std::vector<Value>;
using Null   = std::monostate;

class Value {
public:
    using Storage = std::variant<Null, bool, std::int64_t, double,
                                 std::string, Array, Object>;

    Value() = default;
    Value(Null)            : v_{} {}
    Value(bool b)          : v_{b} {}
    Value(std::int64_t i)  : v_{i} {}
    Value(double d)        : v_{d} {}
    Value(std::string s)   : v_{std::move(s)} {}
    Value(Array a)         : v_{std::move(a)} {}
    Value(Object o)        : v_{std::move(o)} {}

    // Type queries
    [[nodiscard]] bool is_null()   const noexcept { return std::holds_alternative<Null>(v_); }
    [[nodiscard]] bool is_bool()   const noexcept { return std::holds_alternative<bool>(v_); }
    [[nodiscard]] bool is_int()    const noexcept { return std::holds_alternative<std::int64_t>(v_); }
    [[nodiscard]] bool is_double() const noexcept { return std::holds_alternative<double>(v_); }
    [[nodiscard]] bool is_number() const noexcept { return is_int() || is_double(); }
    [[nodiscard]] bool is_string() const noexcept { return std::holds_alternative<std::string>(v_); }
    [[nodiscard]] bool is_array()  const noexcept { return std::holds_alternative<Array>(v_); }
    [[nodiscard]] bool is_object() const noexcept { return std::holds_alternative<Object>(v_); }

    // Typed accessors — throw std::bad_variant_access on type mismatch.
    [[nodiscard]] bool                 as_bool()   const { return std::get<bool>(v_); }
    [[nodiscard]] std::int64_t         as_int()    const { return std::get<std::int64_t>(v_); }
    [[nodiscard]] double               as_double() const {
        if (is_int())    return static_cast<double>(std::get<std::int64_t>(v_));
        if (is_double()) return std::get<double>(v_);
        throw std::bad_variant_access{};
    }
    [[nodiscard]] const std::string&   as_string() const { return std::get<std::string>(v_); }
    [[nodiscard]] const Array&         as_array()  const { return std::get<Array>(v_); }
    [[nodiscard]] const Object&        as_object() const { return std::get<Object>(v_); }

    // Object helpers — return null Value if missing (does not throw).
    [[nodiscard]] const Value& at(const std::string& key) const {
        static const Value null_v;
        if (!is_object()) return null_v;
        auto it = std::get<Object>(v_).find(key);
        return it == std::get<Object>(v_).end() ? null_v : it->second;
    }
    [[nodiscard]] bool contains(const std::string& key) const {
        if (!is_object()) return false;
        return std::get<Object>(v_).contains(key);
    }

    // Array helpers
    [[nodiscard]] const Value& at(std::size_t i) const {
        return std::get<Array>(v_).at(i);
    }
    [[nodiscard]] std::size_t size() const {
        if (is_array())  return std::get<Array>(v_).size();
        if (is_object()) return std::get<Object>(v_).size();
        return 0;
    }

private:
    Storage v_;
};

// ---------------------------------------------------------------------------
// Parse error
// ---------------------------------------------------------------------------

enum class ParseError {
    Ok,
    UnexpectedEnd,
    InvalidChar,
    InvalidEscape,
    InvalidNumber,
    InvalidUtf8,
    NestingTooDeep
};

[[nodiscard]] inline const char* to_string(ParseError e) noexcept {
    switch (e) {
    case ParseError::Ok:              return "ok";
    case ParseError::UnexpectedEnd:   return "unexpected end of input";
    case ParseError::InvalidChar:     return "invalid character";
    case ParseError::InvalidEscape:   return "invalid escape sequence";
    case ParseError::InvalidNumber:   return "invalid number";
    case ParseError::InvalidUtf8:     return "invalid UTF-8";
    case ParseError::NestingTooDeep:  return "nesting too deep";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace detail {

inline constexpr int kMaxDepth = 64;

struct Parser {
    std::string_view src;
    std::size_t pos = 0;
    int depth = 0;

    [[nodiscard]] bool eof() const noexcept { return pos >= src.size(); }
    [[nodiscard]] char peek() const noexcept { return eof() ? '\0' : src[pos]; }
    char advance() noexcept { return eof() ? '\0' : src[pos++]; }

    void skip_ws() noexcept {
        while (!eof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos;
            } else if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/') {
                // line comment — non-standard but useful for trace files
                while (!eof() && peek() != '\n') ++pos;
            } else {
                break;
            }
        }
    }

    bool match(char c) {
        skip_ws();
        if (peek() == c) { ++pos; return true; }
        return false;
    }

    std::expected<Value, ParseError> parse_value() {
        if (++depth > kMaxDepth) return std::unexpected{ParseError::NestingTooDeep};

        skip_ws();
        if (eof()) return std::unexpected{ParseError::UnexpectedEnd};

        std::expected<Value, ParseError> result{Value{}};
        char c = peek();
        switch (c) {
        case '{': result = parse_object(); break;
        case '[': result = parse_array();  break;
        case '"': result = parse_string(); break;
        case 't': case 'f': result = parse_bool();   break;
        case 'n': result = parse_null();   break;
        default:
            if (c == '-' || (c >= '0' && c <= '9')) result = parse_number();
            else result = std::unexpected{ParseError::InvalidChar};
        }

        --depth;
        return result;
    }

    std::expected<Value, ParseError> parse_object() {
        if (!match('{')) return std::unexpected{ParseError::InvalidChar};
        Object obj;
        skip_ws();
        if (peek() == '}') { ++pos; return Value{std::move(obj)}; }
        while (true) {
            skip_ws();
            auto key = parse_string();
            if (!key) return key;
            if (!match(':')) return std::unexpected{ParseError::InvalidChar};
            auto val = parse_value();
            if (!val) return val;
            obj.emplace(std::move(key->as_string()), std::move(*val));
            skip_ws();
            if (match(',')) continue;
            if (match('}')) break;
            return std::unexpected{ParseError::InvalidChar};
        }
        return Value{std::move(obj)};
    }

    std::expected<Value, ParseError> parse_array() {
        if (!match('[')) return std::unexpected{ParseError::InvalidChar};
        Array arr;
        skip_ws();
        if (peek() == ']') { ++pos; return Value{std::move(arr)}; }
        while (true) {
            auto val = parse_value();
            if (!val) return val;
            arr.emplace_back(std::move(*val));
            skip_ws();
            if (match(',')) continue;
            if (match(']')) break;
            return std::unexpected{ParseError::InvalidChar};
        }
        return Value{std::move(arr)};
    }

    std::expected<Value, ParseError> parse_string() {
        if (!match('"')) return std::unexpected{ParseError::InvalidChar};
        std::string out;
        while (true) {
            if (eof()) return std::unexpected{ParseError::UnexpectedEnd};
            char c = advance();
            if (c == '"') break;
            if (c == '\\') {
                if (eof()) return std::unexpected{ParseError::UnexpectedEnd};
                char e = advance();
                switch (e) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    // \uXXXX — simplistic BMP-only
                    if (pos + 4 > src.size())
                        return std::unexpected{ParseError::InvalidEscape};
                    std::string h(src.substr(pos, 4));
                    pos += 4;
                    unsigned long cp = std::stoul(h, nullptr, 16);
                    if (cp < 0x80) {
                        out += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default:
                    return std::unexpected{ParseError::InvalidEscape};
                }
            } else {
                out += c;
            }
        }
        return Value{std::move(out)};
    }

    std::expected<Value, ParseError> parse_number() {
        std::size_t start = pos;
        bool is_float = false;
        if (peek() == '-') ++pos;
        if (eof() || !(std::isdigit(static_cast<unsigned char>(peek()))))
            return std::unexpected{ParseError::InvalidNumber};
        if (peek() == '0') {
            ++pos;
        } else {
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++pos;
        }
        if (!eof() && peek() == '.') {
            is_float = true;
            ++pos;
            if (eof() || !std::isdigit(static_cast<unsigned char>(peek())))
                return std::unexpected{ParseError::InvalidNumber};
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++pos;
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            is_float = true;
            ++pos;
            if (!eof() && (peek() == '+' || peek() == '-')) ++pos;
            if (eof() || !std::isdigit(static_cast<unsigned char>(peek())))
                return std::unexpected{ParseError::InvalidNumber};
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++pos;
        }
        std::string_view tok = src.substr(start, pos - start);
        try {
            if (is_float) return Value{std::stod(std::string(tok))};
            return Value{static_cast<std::int64_t>(std::stoll(std::string(tok)))};
        } catch (...) {
            return std::unexpected{ParseError::InvalidNumber};
        }
    }

    std::expected<Value, ParseError> parse_bool() {
        if (src.substr(pos, 4) == "true")  { pos += 4; return Value{true}; }
        if (src.substr(pos, 5) == "false") { pos += 5; return Value{false}; }
        return std::unexpected{ParseError::InvalidChar};
    }

    std::expected<Value, ParseError> parse_null() {
        if (src.substr(pos, 4) == "null") { pos += 4; return Value{Null{}}; }
        return std::unexpected{ParseError::InvalidChar};
    }
};

}  // namespace detail

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

[[nodiscard]] inline auto parse(std::string_view src)
    -> std::expected<Value, ParseError>
{
    detail::Parser p{src, 0, 0};
    auto v = p.parse_value();
    if (!v) return v;
    p.skip_ws();
    if (!p.eof()) return std::unexpected{ParseError::InvalidChar};
    return v;
}

[[nodiscard]] inline auto parse_file(const std::string& path)
    -> std::expected<Value, ParseError>
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::unexpected{ParseError::UnexpectedEnd};
    std::ostringstream ss;
    ss << f.rdbuf();
    return parse(ss.str());
}

}  // namespace resf2::test::json
