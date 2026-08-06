// engine/game/location_manager.cpp
//
// LocationManager implementation — location discovery, loading, and management.

#include "location_manager.hpp"
#include "asset_manager.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "engine/format/xml_doc.hpp"
#include "game.hpp"       // for helper functions (read_text, tof, toi)

namespace resf2::game {

// ---------- helpers ----------

static GameLocation parse_location(const std::string& xml) {
    fmt::XmlDocument doc;
    if (!doc.parse(xml)) {
        std::fprintf(stderr, "[location] xml_doc parse error: %s\n", doc.error().c_str());
        return {};
    }

    GameLocation loc;
    auto* root = doc.root()->first_child("Root");
    if (!root) return loc;

    loc.color = root->attr("Color");
    loc.width = tof(root->attr("Width"));
    loc.height = tof(root->attr("Height"));
    loc.wall = tof(root->attr("Wall"));
    loc.floor = tof(root->attr("Floor"));

    for (const auto& child : root->children) {
        if (child.name == "Layer") {
            LocationLayer layer;
            layer.type = toi(child.attr("Type"));
            layer.factor = tof(child.attr("Factor"), 1.0f);
            layer.atlas_name = child.attr("Atlas");
            // [ORIGINAL] Both read by the original's layer parser
            // (game+0x3E40D0). Path redirects the atlas lookup to another
            // location's directory; without it the layer renders nothing.
            layer.path = child.attr("Path");
            layer.scaling = (child.attr("Scaling") == "1");

            for (const auto& ic : child.children) {
                if (ic.name == "Image" || ic.name == "SimpleEffect") {
                    LayerImage img;
                    img.atlas_name = layer.atlas_name;
                    img.class_name = ic.attr("ClassName");
                    img.x = tof(ic.attr("X"));
                    img.y = tof(ic.attr("Y"));
                    img.w = tof(ic.attr("Width"));
                    img.h = tof(ic.attr("Height"));
                    img.color = ic.attr("Color");
                    // [Wave 11B W2] The arena WALLS: the original creates
                    // the wall collision objects from the
                    // <Image ClassName="left"/"right"> anchors (their X is
                    // the wall's world position - dojo +-680); both
                    // fighters stop AT these walls (SPEC_WORLD_FEEL 3b,
                    // VERIFY_W11 3: wall collision objects, not width/2).
                    if (ic.name == "Image" &&
                        (img.class_name == "left" || img.class_name == "right")) {
                        if (img.class_name == "left") loc.wall_left_x = img.x;
                        else loc.wall_right_x = img.x;
                    }
                    // [ORIGINAL] A <SimpleEffect> animates itself. Only the
                    // Transparency channel is modelled so far; OscillationX/Y
                    // and Rotation use the same curve type (see
                    // engine/format/effect_curve.hpp).
                    if (const auto* tr = ic.first_child("Transparency"))
                        resf2::format::parse_transparency(*tr, img.transparency);
                    layer.images.push_back(std::move(img));
                }
            }

            auto* mv = child.first_child("ModelsViewer");
            if (mv) {
                loc.player_x = tof(mv->attr("PlayerPositionX"));
                loc.player_y = tof(mv->attr("PlayerPositionY"));
                loc.enemy_x = tof(mv->attr("EnemyPositionX"));
                loc.enemy_y = tof(mv->attr("EnemyPositionY"));
            }

            loc.layers.push_back(std::move(layer));
        }
    }

    return loc;
}

// ---------- discover_locations ----------

void LocationManager::discover_locations(const std::string& asset_root) {
    location_names_.clear();
    auto root = std::filesystem::path(asset_root);
    for (const auto& base : {root / "assets" / "locations",
                              root / "locations"}) {
        if (!std::filesystem::exists(base)) continue;
        for (auto& entry : std::filesystem::directory_iterator(base)) {
            if (entry.is_directory()) {
                std::string name = entry.path().filename().string();
                if (std::filesystem::exists(entry.path() / "params.xml")) {
                    location_names_.push_back(name);
                }
            }
        }
        if (!location_names_.empty()) break;
    }
    std::printf("[LOCATIONS] Discovered %zu locations\n", location_names_.size());
    if (!location_names_.empty()) {
        std::printf("  First 5: ");
        for (size_t i = 0; i < std::min(size_t{5}, location_names_.size()); ++i)
            std::printf("%s ", location_names_[i].c_str());
        std::printf("\n");
    }
}

// ---------- load_location ----------

void LocationManager::load_location(const std::string& name, const std::string& asset_root,
                                    AssetManager* assets) {
    auto root = std::filesystem::path(asset_root);
    std::string params_path;
    for (const auto& dir : {root/"assets"/"locations"/name,
                             root/"locations"/name,
                             root/"assets"/"1536"/"locations"/name}) {
        auto p = dir/"params.xml";
        if (std::filesystem::exists(p)) { params_path = p.string(); break; }
    }
    if (params_path.empty()) {
        std::printf("Location '%s' not found!\n", name.c_str()); return;
    }
    std::printf("Loading location: %s\n", params_path.c_str());
    auto xml = read_text(params_path);
    location_ = std::make_unique<GameLocation>(parse_location(xml));
    std::printf("  Player: (%.0f, %.0f)  Enemy: (%.0f, %.0f)\n",
                location_->player_x, location_->player_y,
                location_->enemy_x, location_->enemy_y);
    std::printf("  Walls: left=%.0f right=%.0f (params.xml <Image "
                "ClassName=left/right>)\n",
                location_->wall_left_x, location_->wall_right_x);

    // Load atlases for each layer (if AssetManager available)
    if (assets) {
        for (auto& layer : location_->layers) {
            if (layer.atlas_name.empty()) continue;
            if (assets->atlases().count(layer.atlas_name)) continue;
            // [ORIGINAL] <Layer Path="locations/other/"> borrows the atlas from
            // another location, so the search directory comes from Path rather
            // than from this location's name. Path is stored with a trailing
            // slash and a "locations/" prefix, e.g. "locations/spaceship/".
            std::string owner = name;
            if (!layer.path.empty()) {
                std::string p = layer.path;
                while (!p.empty() && (p.back() == '/' || p.back() == '\\'))
                    p.pop_back();
                const auto slash = p.find_last_of("/\\");
                owner = (slash == std::string::npos) ? p : p.substr(slash + 1);
                if (!owner.empty() && owner != name) {
                    std::printf("  [atlas] '%s' borrowed from location '%s'"
                                " (Path=\"%s\")\n",
                                layer.atlas_name.c_str(), owner.c_str(),
                                layer.path.c_str());
                }
            }
            assets->load_atlas(layer.atlas_name, owner, asset_root);
        }
    }
}

} // namespace resf2::game
