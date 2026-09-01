#pragma once

// The shell screens — MainMenu, Map, BattleResult.
//
// JS study (Task 1 documentation lives in core/scene/README.md):
//   - GeneralMenu (screen 8, xn L1167): the game's main menu. The four
//     top-tab buttons are `cs` (JS L2188): Progress/Strikes/Achiev/Seal
//     from the profile atlas; the map/dojo/shop/profile entry buttons are
//     the menu atlas frames (menu.aaef83fb.json): Dojo_normal/Map_normal/
//     Shop_normal/Profile_normal (+ _active/_pushed). The menu shows the
//     dojo background + the za top bar (JS L1972: topPanel frame from
//     misc, money/energy display). Clicking "Fight" (the Dojo button)
//     enters the map.
//   - Map (screen 5, Ya L2124-2132): the battle-node screen. The
//     backgrounds are `map/part0..6` (asset ids 336..324, the `map0` frame
//     is 2046x854); the battle nodes come from stages.xml <Zone>/<Battle>
//     with X/Y positions (qe.X0a L2144: node x = battle.x*uM + bg.w/2,
//     y = -battle.y*uM + bg.h/2, uM ~ 1). Clicking a node starts a fight.
//   - The battle-node button art is in map/buttons.json:
//     BattleBtnBase/base_<name> + BattleBtnActive/active_<name> +
//     BattleBtnPressed/pressed_<name> (e.g. base_training/active_training).
//
// The menu/map atlases ship as ASTC ktx / crunch dds (not CPU-decodable by
// the current pipeline — see core/data/README.md), so this phase renders a
// functional menu: the dojo webp background + flat labeled buttons at the
// JS-derived positions. The exact atlas-art layout is flagged as a gap.

#include <memory>
#include <string>
#include <vector>

#include "app/fight_assets.hpp"
#include "app/item_catalog.hpp"
#include "app/screen_manager.hpp"

namespace sf2::scene {
class FightController;
class LocationScene;
} // namespace sf2::scene

namespace sf2::app {

// The main menu — native GeneralMenu (screen 8).
class MainMenuScreen : public Screen {
public:
    explicit MainMenuScreen(ScreenManager& mgr);

    ScreenId id() const override { return kScreenGeneralMenu; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

private:
    struct Button {
        std::string label;
        float x = 0.0f;  // center
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        int target = -1;  // ScreenId to push when clicked
    };
    std::vector<Button> buttons_;
    int hover_ = -1;
    int last_hover_ = -1;
    bool money_logged_ = false;
};

// The map — native Map (screen 5).
class MapScreen : public Screen {
public:
    explicit MapScreen(ScreenManager& mgr);

    ScreenId id() const override { return kScreenMap; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

    struct Node {
        std::string name;
        std::string type;
        float x = 0.0f;  // screen pos (center)
        float y = 0.0f;
        bool active = true;
    };

private:
    std::vector<Node> nodes_;
    int hover_ = -1;
};

// The fight — native Fight screen (screen 6, JS `ai`/`ma` L2004-2010).
// Drives a real FightController (rounds/phases/timer/win-lose) with the
// player's keyboard input (Punch/Block/Forward/Back per the key_type enum)
// and the enemy AI. When the battle ends it pushes the Results screen with
// the winner + the reward.
class FightScreen : public Screen {
public:
    // `battle_name`/`location` = the stages.xml battle; `reward_money`/
    // `reward_exp` = the fight's reward (from the pending battle). `owned`
    // = the player's (type, subtype) items for the Locks move list.
    FightScreen(ScreenManager& mgr, const std::string& battle_name,
                const std::string& location, int reward_money, int reward_exp,
                const std::vector<std::pair<std::string, std::string>>& owned);

    ScreenId id() const override { return kScreenFight; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

    // Keyboard -> game key types (JS `Ik` key events -> the fight input).
    void on_key(int glfw_key, bool down);

    // The player's move-list size (the equipment-change evidence: the
    // headless-loop driver logs it before/after equipping a weapon).
    // Defined in screens.cpp (needs the full FightController type).
    std::size_t move_list_size() const;

    // [FIX Phase 4a verification] Prints the sampled bone positions of the
    // player/enemy (a clip-frame bone-sample check) + their triangle bbox
    // (the stretched/on-screen check). Defined in screens.cpp.
    void verify_fight() const;

private:
    std::string battle_name_;
    std::string location_;
    int reward_money_ = 0;
    int reward_exp_ = 0;
    std::unique_ptr<sf2::scene::FightController> fight_;
    bool results_pushed_ = false;
    bool key_state_[16] = {};
    int last_log_frame_ = 0;
    bool auto_attack_wired_ = false;
};

// The battle results — native results flow (JS `v.kD` L622187 -> the
// `Fh` results screen + `qxa` L1213 back to the map). Shows win/lose,
// applies the money/XP reward (JS `Pa.Fwa`/`Pa.Iab` -> `p.o.Fr`/`Jab`),
// updates + saves the Warrior, and returns to the map on click.
class ResultsScreen : public Screen {
public:
    ResultsScreen(ScreenManager& mgr, bool player_won, int money_reward,
                  int exp_reward);

    ScreenId id() const override { return kScreenResults; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

private:
    bool player_won_ = false;
    int money_reward_ = 0;
    int exp_reward_ = 0;
    bool applied_ = false;
};

// The shop — native Shop screen (screen 4, JS `Oa` g="468").
// Lists the priced Weapon/Armor/Helm items from list.xml as flat cards
// (the shop atlas is ASTC — not CPU-decodable, so the cards are flat with
// labels + the item's price). Click an item to buy: check money (JS
// `Pa.iwa` L629626: `p.o.Tb >= a.jp()`), deduct, add to the inventory
// (JS `Pa.gI` L628934 -> `p.o.xa.Oo`), save.
class ShopScreen : public Screen {
public:
    explicit ShopScreen(ScreenManager& mgr);

    ScreenId id() const override { return kScreenShop; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

private:
    std::vector<CatalogItem> items_;
    int hover_ = -1;
    int money_logged_ = 0;
};

// The equipment — native Profile/equipment screen (JS `Oa.f5` case 5 +
// `$g.$o` L152184 the equip flow). Shows the slots (Weapon/Armor/Helm) +
// the owned items; click an owned item to equip it: set the Warrior's
// slot + the item's Equipped flag (JS `xc.hk` L412433 + `setItem`),
// rebuild the fighter (merged model + move list), save.
class EquipmentScreen : public Screen {
public:
    explicit EquipmentScreen(ScreenManager& mgr);

    ScreenId id() const override { return kScreenProfile; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

private:
    std::vector<CatalogItem> catalog_;
    int hover_ = -1;
};

// The shared item catalog (the shop list + the equipment item lookup).
// Loaded once from list.xml and cached.
std::vector<CatalogItem> load_catalog(App& app);

// The FULL item catalog (all items incl. the ShopHide/Hidden base items
// Body/Head/Fists/NoRanged/NoMagic). The EquipmentScreen needs the type/
// subtype of every owned item, not just the shop-visible ones. Loaded once
// and cached.
std::vector<CatalogItem> load_full_catalog(App& app);

// Factory: creates a screen by id (used by Screen::push).
std::unique_ptr<Screen> make_screen(ScreenManager& mgr, ScreenId id);

} // namespace sf2::app
