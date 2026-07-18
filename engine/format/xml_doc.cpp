#include "xml_doc.hpp"
#include <cstdio>
#include <fstream>
#include <algorithm>
#include <cstring>

namespace resf2::format {

// ---- XmlNode ----

const XmlAttribute* XmlNode::find_attr(const std::string& name) const {
    for (auto& a : attributes) {
        if (a.name == name) return &a;
    }
    return nullptr;
}

std::string XmlNode::attr(const std::string& name, const std::string& def) const {
    auto* a = find_attr(name);
    return a ? a->value : def;
}

XmlNode* XmlNode::first_child(const std::string& name) {
    for (auto& c : children) {
        if (name.empty() || c.name == name) return &c;
    }
    return nullptr;
}

const XmlNode* XmlNode::first_child(const std::string& name) const {
    return const_cast<XmlNode*>(this)->first_child(name);
}

std::vector<const XmlNode*> XmlNode::find_all(const std::string& name) const {
    std::vector<const XmlNode*> result;
    for (auto& c : children) {
        if (c.name == name) result.push_back(&c);
    }
    return result;
}

// ---- XmlDocument helpers ----

void XmlDocument::skip_whitespace(std::string_view& sv) {
    while (!sv.empty() && (sv[0] == ' ' || sv[0] == '\t' || sv[0] == '\n' || sv[0] == '\r')) {
        sv.remove_prefix(1);
    }
}

std::string_view XmlDocument::consume_until(std::string_view& sv, char delim) {
    size_t end = sv.find(delim);
    if (end == std::string_view::npos) {
        auto result = sv;
        sv = std::string_view{};
        return result;
    }
    auto result = sv.substr(0, end);
    sv.remove_prefix(end + 1);
    return result;
}

bool XmlDocument::parse(std::string_view xml) {
    buffer_ = std::string(xml);
    std::string_view sv = buffer_;
    root_ = std::make_unique<XmlNode>();
    root_->name = "#document";

    // Skip XML declaration / BOM
    skip_whitespace(sv);
    while (!sv.empty() && sv[0] == '<' && sv.size() > 1) {
        if (sv[1] == '?' || sv[1] == '!') {
            // Processing instruction or DOCTYPE — skip until >
            auto end = sv.find('>');
            if (end == std::string_view::npos) {
                error_ = "Unterminated processing instruction";
                return false;
            }
            sv.remove_prefix(end + 1);
            skip_whitespace(sv);
            continue;
        }
        break;
    }

    while (!sv.empty()) {
        skip_whitespace(sv);
        if (sv.empty()) break;
        if (sv[0] != '<') {
            error_ = "Expected '<' at root level";
            return false;
        }
        if (!parse_node(sv, root_.get())) return false;
    }

    return true;
}

bool XmlDocument::load_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        error_ = "Cannot open: " + path;
        return false;
    }
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string data(sz, '\0');
    f.read(data.data(), static_cast<std::streamsize>(sz));
    return parse(data);
}

bool XmlDocument::parse_node(std::string_view& sv, XmlNode* parent) {
    // sv starts at '<'
    if (sv.empty() || sv[0] != '<') {
        error_ = "Expected '<'";
        return false;
    }
    sv.remove_prefix(1);  // skip '<'

    // Check for comment
    if (sv.size() >= 3 && sv[0] == '!' && sv[1] == '-' && sv[2] == '-') {
        sv.remove_prefix(3);
        auto end = sv.find("-->");
        if (end == std::string_view::npos) {
            error_ = "Unterminated comment";
            return false;
        }
        sv.remove_prefix(end + 3);
        return true;
    }

    // Check for CDATA
    if (sv.size() >= 8 && sv[0] == '!' && sv.substr(0, 8) == "![CDATA[") {
        sv.remove_prefix(8);
        auto end = sv.find("]]>");
        if (end == std::string_view::npos) {
            error_ = "Unterminated CDATA";
            return false;
        }
        sv.remove_prefix(end + 3);
        return true;
    }

    // Read tag name
    skip_whitespace(sv);
    std::string tag_name;
    while (!sv.empty() && sv[0] != '>' && sv[0] != '/' && sv[0] != ' ' && sv[0] != '\t' && sv[0] != '\n' && sv[0] != '\r') {
        tag_name += sv[0];
        sv.remove_prefix(1);
    }
    if (tag_name.empty()) {
        // Check for closing tag </...>
        if (sv[0] == '/') {
            sv.remove_prefix(1);
            consume_until(sv, '>');
            return true; // ignore close tags at this level
        }
        error_ = "Empty tag name";
        return false;
    }

    bool self_closing = false;
    skip_whitespace(sv);

    XmlNode node;
    node.name = std::move(tag_name);
    node.parent = parent;

    // Parse attributes
    while (!sv.empty() && sv[0] != '>' && sv[0] != '/') {
        if (sv[0] == '?' || sv[0] == '!') {
            // Skip unexpected processing instructions / comments inside tag
            auto end = sv.find('>');
            if (end == std::string_view::npos) break;
            sv.remove_prefix(end + 1);
            return true;
        }
        // Read attribute name
        std::string attr_name;
        while (!sv.empty() && sv[0] != '=' && sv[0] != '>' && sv[0] != '/' && sv[0] != ' ' && sv[0] != '\t' && sv[0] != '\n' && sv[0] != '\r') {
            attr_name += sv[0];
            sv.remove_prefix(1);
        }
        if (attr_name.empty()) {
            skip_whitespace(sv);
            continue;
        }
        skip_whitespace(sv);
        if (!sv.empty() && sv[0] == '=') {
            sv.remove_prefix(1);
            skip_whitespace(sv);
            if (!sv.empty() && (sv[0] == '"' || sv[0] == '\'')) {
                char quote = sv[0];
                sv.remove_prefix(1);
                auto val_end = sv.find(quote);
                if (val_end == std::string_view::npos) {
                    error_ = "Unterminated attribute value in <" + node.name + ">";
                    return false;
                }
                std::string attr_value(sv.substr(0, val_end));
                sv.remove_prefix(val_end + 1);
                node.attributes.push_back({std::move(attr_name), std::move(attr_value)});
            }
        }
        skip_whitespace(sv);
        if (!sv.empty() && sv[0] == '/') {
            self_closing = true;
            sv.remove_prefix(1);
        }
        skip_whitespace(sv);
    }

    if (sv.empty()) {
        error_ = "Unexpected end in <" + node.name + ">";
        return false;
    }

    if (sv[0] == '/') {
        self_closing = true;
        sv.remove_prefix(1);
        skip_whitespace(sv);
    }

    if (sv[0] != '>') {
        error_ = "Expected '>' in <" + node.name + ">";
        return false;
    }
    sv.remove_prefix(1);  // skip '>'

    if (self_closing) {
        parent->children.push_back(std::move(node));
        return true;
    }

    // Parse children and text content
    std::string text_content;
    bool has_text = false;
    while (!sv.empty()) {
        skip_whitespace(sv);
        if (sv.empty()) break;

        if (sv[0] == '<') {
            // Check for closing tag
            if (sv.size() > 1 && sv[1] == '/') {
                // Find the tag name
                sv.remove_prefix(2);
                skip_whitespace(sv);
                std::string close_name;
                while (!sv.empty() && sv[0] != '>' && sv[0] != ' ' && sv[0] != '\t' && sv[0] != '\n' && sv[0] != '\r') {
                    close_name += sv[0];
                    sv.remove_prefix(1);
                }
                auto end = sv.find('>');
                if (end != std::string_view::npos) sv.remove_prefix(end + 1);
                // If we had text content before, store it
                if (has_text) {
                    node.content = std::move(text_content);
                }
                parent->children.push_back(std::move(node));
                return true;
            }

            // Push text content to current node before parsing child
            if (has_text) {
                // If there's text before a child, store it
                node.content = std::move(text_content);
                has_text = false;
            }

            // Parse child node
            if (!parse_node(sv, &node)) return false;
        } else {
            // Text content
            size_t end = sv.find('<');
            if (end == std::string_view::npos) {
                text_content += std::string(sv);
                sv = std::string_view{};
            } else {
                text_content += std::string(sv.substr(0, end));
                sv.remove_prefix(end);
            }
            has_text = true;
        }
    }

    // If we ran out of input, store what we have
    if (has_text) {
        node.content = std::move(text_content);
    }
    parent->children.push_back(std::move(node));
    return true;
}

} // namespace resf2::format
