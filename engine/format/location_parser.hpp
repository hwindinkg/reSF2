#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace resf2::format {

struct LayerImage {
    std::string atlas_name;
    std::string class_name;
    float x = 0, y = 0;
    float w = 0, h = 0;
    std::string color;  // hex RRGGBB
};

struct LocationLayer {
    int type = 0;
    float factor = 1.0f;
    std::string atlas_name;
    std::vector<LayerImage> images;
};

struct LocationData {
    std::string name;
    std::string color;    // hex RRGGBB
    float width = 0;
    float height = 0;
    float wall = 0;
    float floor = 0;
    float player_x = 0, player_y = 0;
    float enemy_x = 0, enemy_y = 0;
    std::vector<LocationLayer> layers;
};

class LocationParser {
public:
    bool parse(const std::string& xml, LocationData& out);
    bool load_file(const std::string& path, LocationData& out);
    std::string error() const { return error_; }

private:
    std::string error_;
    void parse_image(const class XmlNode& node, LayerImage& img);
};

} // namespace resf2::format
