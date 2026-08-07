#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "effect_curve.hpp"

namespace resf2::format {

struct LayerImage {
    std::string atlas_name;
    std::string class_name;
    float x = 0, y = 0;
    float w = 0, h = 0;
    std::string color;  // hex RRGGBB
    // [ORIGINAL] <SimpleEffect><Transparency> — empty for a plain <Image>.
    EffectCurve transparency;
};

struct LocationLayer {
    int type = 0;
    float factor = 1.0f;
    std::string atlas_name;

    // [ORIGINAL] <Layer Path="locations/spaceship/"> — the layer's atlas lives
    // in ANOTHER location's directory, not this one's. 26 layers across the
    // shipped params.xml files use it (flying_rocks, waterfall, ruins_village,
    // spaceship). Ignoring it means those atlases are looked up in the wrong
    // directory and the layer silently renders nothing.
    // Read by the original's layer parser at game+0x3E40D0.
    std::string path;

    // [ORIGINAL] <Layer Scaling="1"> — set on foreground layers (62 of them).
    // The original stores it as a flag on the layer object (+0x154/+0x155 in
    // game+0x3E40D0).
    bool scaling = false;

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
    // [Wave 11C P2] <Root Music="id|id"> - the battle-music ID list
    // (Location::parse FUN_8F43C6F8, attr read @ 0x8F43CB54 -> vector<string>
    // at Location+0x18). The fight screen random-picks one ID from it
    // (SPEC_PRESENTATION Q2; VERIFY_W11 Q2: the battle track = this list,
    // not stages.xml <Battle Music>).
    std::vector<std::string> music;
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
