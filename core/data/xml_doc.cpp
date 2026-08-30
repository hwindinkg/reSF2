#include "xml_doc.hpp"

#include <cstring>
#include <stdexcept>

namespace sf2::data {

void xml_doc::parse(const std::string& text) {
    parse(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

void xml_doc::parse(const std::uint8_t* data, std::size_t size) {
    const pugi::xml_parse_result result =
        doc_.load_buffer(data, size, pugi::parse_default, pugi::encoding_auto);
    if (!result) {
        throw std::runtime_error(std::string("xml_doc::parse failed: ") +
                                 result.description());
    }
}

bool xml_attr_bool(pugi::xml_node node, const char* name, bool def) {
    const pugi::xml_attribute attr = node.attribute(name);
    if (!attr) {
        return def;
    }
    const char* value = attr.value();
    return std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0;
}

int xml_attr_int(pugi::xml_node node, const char* name, int def) {
    const pugi::xml_attribute attr = node.attribute(name);
    return attr ? attr.as_int(def) : def;
}

float xml_attr_float(pugi::xml_node node, const char* name, float def) {
    const pugi::xml_attribute attr = node.attribute(name);
    return attr ? attr.as_float(def) : def;
}

} // namespace sf2::data