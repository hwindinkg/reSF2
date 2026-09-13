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
#include "xml_archive.hpp"

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

    // The Punchbag dummy parts (DOJO_BG_STATIC: the Punchbag Warrior wears
    // the PunchingBag/SkeletonPunchingBag items = `mdl_punching_bag` +
    // `mdl_skeleton_punching_bag` in models.dat; MODEL_FORMAT.md Dojo row:
    // bag = edges+capsules only, bag skeleton = bones+edges, no mesh).
    // Display-only: the Dojo hub hangs this dummy under the beam (Tf
    // Training setup); the fight sim keeps using `merged`.
    // `Fighter::sample` anchors each fighter on its PivotNode bone
    // (`internal_settings.xml` <PivotNode Name="NPivot"/>, `Dl.oL` L577), so
    // the bag is placed by NPivot (bind Y=+109) directly -- no COM re-point
    // is applied (see app.cpp).
    sf2::scene::Model bag_skeleton;
    sf2::scene::Model bag_body;
    sf2::scene::Model merged_bag;

    // Retained models.dat entries + the parsed part cache (filled by app.cpp).
    // A fight builds each warrior's model from its OWN equipment (JS `xc.cM`
    // L809-810 -> `wd.Erb` L496 -> `Yc.load` L568): the list.xml `<Item
    // Model>` attribute is the archive entry name (e.g. WEAPON_KUNAI ->
    // `mdl_weapon_kunai`, BODY_SHIN -> `mdl_body_shin`). Parts are parsed once
    // and shared across fighters.
    std::vector<sf2::data::archive_entry> model_archive;
    std::map<std::string, sf2::scene::Model> model_cache;

    // Parses (once) + returns the model for an archive entry name, or
    // nullptr when the archive has no such entry (JS `Yc.parse` on a name
    // that is not in models.dat) — the caller then keeps the base body.
    const sf2::scene::Model* load_part(const std::string& model_name) {
        const auto it = model_cache.find(model_name);
        if (it != model_cache.end()) return &it->second;
        for (const sf2::data::archive_entry& e : model_archive) {
            if (e.name != model_name) continue;
            return &model_cache
                        .emplace(model_name,
                                 sf2::scene::model_parse(e.data.data(), e.data.size()))
                        .first->second;
        }
        return nullptr;
    }

    // Merges an ordered model-name list into one fighter body (JS `Yc.load`
    // L568 loop over `xc.cM`'s list: skeleton first, then weapon/armor/helm;
    // first-definition-wins on shared bone names so clip indices stay valid
    // for the skeleton). Empty/unknown names are skipped (that part is simply
    // not worn).
    sf2::scene::Model merge_names(const std::vector<std::string>& names) {
        std::vector<sf2::scene::Model> parts;
        for (const std::string& n : names) {
            if (n.empty()) continue;
            const sf2::scene::Model* p = load_part(n);
            if (p != nullptr) parts.push_back(*p);
        }
        return sf2::scene::build_fighter_model(parts);
    }

    // The shared data (JS `G.data`).
    std::map<std::string, sf2::data::anim_clip> clips;
    std::map<std::string, sf2::scene::MoveDef> moves;
    std::vector<sf2::scene::TacticsFile> tactics_sets;
    std::map<std::string, sf2::scene::TacticDef> tactic_defs;  // by name
    // Perk catalog (res/perks.xml `Be` defs) for the fight trigger bus
    // (`ZOa` equip mapping needs def lookup by name at fight setup).
    std::map<std::string, sf2::scene::PerkDef> perk_catalog;

    // The hub's dojo location (the home-screen backdrop, JS `Tf`
    // L1969-1972). Loaded once by `ensure_dojo_location`. The hub renders
    // this every frame, including while a Fight is on top (ScreenManager
    // renders back-to-front, screen_manager.cpp), so it must stay the dojo
    // and must NOT be shared with the fight.
    sf2::scene::LocationScene dojo;

    // The battle's location scene. JS `Bf.init` L474 builds one scene per
    // battle location; kept separate from `dojo` because the hub keeps
    // rendering its backdrop beneath the fight (a reload of the shared scene
    // by the hub would clobber the fight's location). Reloaded when the
    // battle's location differs from `fight_location_name`.
    sf2::scene::LocationScene fight_location;
    std::string fight_location_name;

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
