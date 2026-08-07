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

    // [Wave 11C P2] The battle-music ID list of `name` (params.xml
    // <Root Music="id|id">), read directly from disk so the fight screen can
    // random-pick the battle track without loading the whole location
    // (SPEC_PRESENTATION Q2: the play site passes the Location object).
    // Empty when the location or the Music attr is absent.
    std::vector<std::string> music_list_for(const std::string& name,
                                            const std::string& asset_root) const;

private:
    std::vector<std::string> location_names_;
    std::unique_ptr<GameLocation> location_;
    std::string current_location_name_ = "dojo";
};

} // namespace resf2::game
