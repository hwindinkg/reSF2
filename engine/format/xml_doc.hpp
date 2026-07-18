#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <memory>

namespace resf2::format {

struct XmlAttribute {
    std::string name;
    std::string value;
};

struct XmlNode {
    std::string name;                     // tag name
    std::string content;                  // text content
    std::vector<XmlAttribute> attributes; // tag attributes
    std::vector<XmlNode> children;        // child elements
    XmlNode* parent = nullptr;

    const XmlAttribute* find_attr(const std::string& name) const;
    std::string attr(const std::string& name, const std::string& def = "") const;

    XmlNode* first_child(const std::string& name = "");
    const XmlNode* first_child(const std::string& name = "") const;

    // Find all children with matching tag name
    std::vector<const XmlNode*> find_all(const std::string& name) const;
};

class XmlDocument {
public:
    bool parse(std::string_view xml);
    bool load_file(const std::string& path);

    const XmlNode* root() const { return root_.get(); }
    XmlNode* root() { return root_.get(); }

    std::string error() const { return error_; }

private:
    std::unique_ptr<XmlNode> root_;
    std::string error_;
    std::string buffer_;  // owns the string data

    bool parse_node(std::string_view& sv, XmlNode* parent);
    bool parse_attributes(std::string_view& sv, XmlNode* node);
    void skip_whitespace(std::string_view& sv);
    std::string_view consume_until(std::string_view& sv, char delim);
};

} // namespace resf2::format
