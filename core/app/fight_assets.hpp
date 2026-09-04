#pragma once

// Fight session assets — the loaded data a FightScreen needs: the merged
// fighter model (rebuilt when equipment changes), the animation clips, the
// move map, the tactics, and the location scene. Owned by the shell (the
// screens reference it), built once per app run (JS: the assets are loaded
// once and shared — `G.data` / the `wd.fya` cache).
//
// The fighter model is REBUILT on equipment change (JS `xc.cM` L412713 +
// `wd.Ulb` L496): the merged model = skeleton + weapon + armor + helm parts
// (the `Model` list order — `build_fighter_model` first-definition-wins).
// The Shop/Equipment flow calls `rebuild_player_model(weapon_model,
// armor_model, helm_model)` and the next FightScreen uses it.

#include <map>
#include <string>
#include <vector>

#include "anim_archive.hpp"
#include "scene/ai.hpp"
#include "scene/location_scene.hpp"
#include "scene/model.hpp"
#include "scene/move_def.hpp"
#include "scene/trigger.hpp"

namespace sf2::scene {
class Fighter;
}

namespace sf2::app {

// The loaded fight assets (shared across Fight/Shop/Equipment screens).
struct FightAssets {
    // The merged fighter model parts (JS `xc.cM` order: skeleton first,
    // then weapon, armor, helm — the game pushes Of(weapon) then Hd,
    // hg(helm), Lg(armor) with the skeleton base always first).
    sf2::scene::Model skeleton;
    sf2::scene::Model body;    // the default Body armor (mdl_body)
    sf2::scene::Model head;    // the default Head helm (mdl_head)
    sf2::scene::Model weapon;  // the CURRENT weapon part (empty for Fists)
    sf2::scene::Model armor;   // the CURRENT armor part (empty = Body)
    sf2::scene::Model helm;    // the CURRENT helm part (empty = Head)

    // The merged model for the fight (JS `xc.cM()` -> `wd.Ulb` rebuilds the
    // fighter from the equipment's `model` list).
    sf2::scene::Model merged;

    // The shared data (JS `G.data`).
    std::map<std::string, sf2::data::anim_clip> clips;
    std::map<std::string, sf2::scene::MoveDef> moves;
    std::vector<sf2::scene::TacticsFile> tactics_sets;
    std::map<std::string, sf2::scene::TacticDef> tactic_defs;  // by name
    // Perk catalog (res/perks.xml `Be` defs) for the fight trigger bus
    // (`ZOa` equip mapping needs def lookup by name at fight setup).
    std::map<std::string, sf2::scene::PerkDef> perk_catalog;

    // The dojo location (the tutorial-zone fight backdrop). The full
    // fight-screen location set is loaded per battle (JS `Bf` per
    // location); the dojo is the tutorial zone's location.
    sf2::scene::LocationScene dojo;

    // Rebuilds the merged model from the current part set (JS `xc.cM`).
    void rebuild_merged() {
        std::vector<sf2::scene::Model> parts;
        parts.push_back(skeleton);
        if (!weapon.bones.empty()) parts.push_back(weapon);
        if (!armor.bones.empty()) parts.push_back(armor);
        else if (!body.bones.empty()) parts.push_back(body);
        if (!helm.bones.empty()) parts.push_back(helm);
        else if (!head.bones.empty()) parts.push_back(head);
        merged = sf2::scene::build_fighter_model(parts);
    }
};

} // namespace sf2::app
