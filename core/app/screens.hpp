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

#include "app/screen_manager.hpp"

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

// The battle-result placeholder — native placeholder for the results flow
// (full version in 3.6b).
class BattleResultScreen : public Screen {
public:
    explicit BattleResultScreen(ScreenManager& mgr, std::string winner_text);

    ScreenId id() const override { return kScreenFight; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

private:
    std::string winner_text_;
};

// Factory: creates a screen by id (used by Screen::push).
std::unique_ptr<Screen> make_screen(ScreenManager& mgr, ScreenId id);

} // namespace sf2::app
