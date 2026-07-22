#pragma once

#include <memory>
#include <string>
#include <vector>

#include "types.hpp"

namespace resf2::game {

class AssetManager;

// ---------- Location manager ----------
//
// Encapsulates game location loading, discovery, and management.
// Loads params.xml, manages GameLocation struct, and orchestrates
// asset loading for each location.

class LocationManager {
public:
    LocationManager() = default;

    void discover_locations(const std::string& asset_root);
    const std::vector<std::string>& location_names() const { return location_names_; }
    void load_location(const std::string& name, const std::string& asset_root,
                       AssetManager* assets = nullptr);
    const GameLocation* location() const { return location_.get(); }
    GameLocation* location() { return location_.get(); }
    const std::string& current_location_name() const { return current_location_name_; }
    void set_current_location_name(const std::string& n) { current_location_name_ = n; }

private:
    std::vector<std::string> location_names_;
    std::unique_ptr<GameLocation> location_;
    std::string current_location_name_ = "dojo";
};

} // namespace resf2::game
