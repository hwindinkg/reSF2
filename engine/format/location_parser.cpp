#include "location_parser.hpp"
#include "xml_doc.hpp"
#include <cstdio>
#include <fstream>
#include <charconv>

namespace resf2::format {

static float to_float(const std::string& s, float def = 0.0f) {
    if (s.empty()) return def;
    float v;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    return r.ec == std::errc{} ? v : def;
}

void LocationParser::parse_image(const XmlNode& node, LayerImage& img) {
    img.class_name = node.attr("ClassName");
    img.x = to_float(node.attr("X"));
    img.y = to_float(node.attr("Y"));
    img.w = to_float(node.attr("Width"));
    img.h = to_float(node.attr("Height"));
    img.color = node.attr("Color");
}

bool LocationParser::parse(const std::string& xml, LocationData& out) {
    XmlDocument doc;
    if (!doc.parse(xml)) {
        error_ = doc.error();
        return false;
    }

    auto* root = doc.root()->first_child("Root");
    if (!root) {
        error_ = "No <Root> element";
        return false;
    }

    out.color = root->attr("Color");
    out.width = to_float(root->attr("Width"));
    out.height = to_float(root->attr("Height"));
    out.wall = to_float(root->attr("Wall"));
    out.floor = to_float(root->attr("Floor"));

    // Parse <Layer> elements
    for (auto& layer_node : root->children) {
        if (layer_node.name != "Layer") continue;

        LocationLayer layer;
        layer.type = (int)to_float(layer_node.attr("Type"));
        layer.factor = to_float(layer_node.attr("Factor"), 1.0f);
        layer.atlas_name = layer_node.attr("Atlas");
        // [ORIGINAL] Both read by the original's layer parser (game+0x3E40D0).
        // Path redirects the atlas lookup to another location's directory.
        layer.path = layer_node.attr("Path");
        layer.scaling = (layer_node.attr("Scaling") == "1");

        // Parse <Image> sub-elements
        for (auto& child : layer_node.children) {
            if (child.name == "Image") {
                LayerImage img;
                img.atlas_name = layer.atlas_name;
                parse_image(child, img);
                layer.images.push_back(img);
            } else if (child.name == "SimpleEffect") {
                LayerImage img;
                img.atlas_name = layer.atlas_name;
                parse_image(child, img);
                // Same shared helper as LocationManager's parser, so the two
                // copies of this file's logic cannot disagree about effects.
                if (const auto* tr = child.first_child("Transparency"))
                    parse_transparency(*tr, img.transparency);
                layer.images.push_back(std::move(img));
            }
        }

        // Parse <ModelsViewer> for player/enemy positions
        auto* mv = layer_node.first_child("ModelsViewer");
        if (mv) {
            out.player_x = to_float(mv->attr("PlayerPositionX"));
            out.player_y = to_float(mv->attr("PlayerPositionY"));
            out.enemy_x = to_float(mv->attr("EnemyPositionX"));
            out.enemy_y = to_float(mv->attr("EnemyPositionY"));
        }

        out.layers.push_back(std::move(layer));
    }

    return true;
}

bool LocationParser::load_file(const std::string& path, LocationData& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        error_ = "Cannot open: " + path;
        return false;
    }
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::string data(sz, '\0');
    f.read(data.data(), static_cast<std::streamsize>(sz));
    return parse(data, out);
}

} // namespace resf2::format
