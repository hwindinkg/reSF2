#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <pugixml.hpp>

namespace sf2::data {

// Thin wrapper around pugixml: parses XML text (UTF-8, BOM-tolerant) into a
// document. Mirrors the game's `Wg.parse` (JS_MAP §7.2).
class xml_doc {
public:
    xml_doc() = default;

    // Parses XML from a string. Throws std::runtime_error on parse failure.
    void parse(const std::string& text);

    // Parses XML from raw bytes (treated as UTF-8 text).
    void parse(const std::uint8_t* data, std::size_t size);

    pugi::xml_node root() const { return doc_.root(); }

private:
    pugi::xml_document doc_;
};

// Attribute helpers mirroring the game's `u` helpers (JS_MAP §7.2: ka/I/H).
bool xml_attr_bool(pugi::xml_node node, const char* name, bool def = false);
int xml_attr_int(pugi::xml_node node, const char* name, int def = 0);
float xml_attr_float(pugi::xml_node node, const char* name, float def = 0.0f);

} // namespace sf2::data