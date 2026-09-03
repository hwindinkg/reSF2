#pragma once

// The shell screens — Dojo, MainMenu, Map, BattleResult.
//
// JS study (Task 1 documentation lives in core/scene/README.md):
//   - Dojo (screen 3, `Tf`): the HOME base — the screen the ORIGINAL boots
//     into (the JS trace: Preloader -> Loader -> Dojo). It shows the dojo
//     backdrop with the Map/Shop/Profile entry buttons, the gem chest and
//     the training fight vs the Punchbag dummy (the stages.xml Punchbag
//     zone Start=1). The native DojoScreen is that home hub: its FIGHT
//     button starts the Training battle of the Punchbag zone (which the
//     MapScreen's Training node also runs).
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

// The dojo — the home screen (native Dojo screen 3, JS `Tf`). The screen
// the game boots into (the original starts here, not in the GeneralMenu):
// the dojo backdrop + the Map/Shop/Profile entry buttons + the FIGHT
// button that starts the training fight vs the Punchbag dummy (the
// stages.xml Punchbag zone Training battle; the fight itself runs the
// shared FightScreen/FightController).
class DojoScreen : public Screen {
public:
    explicit DojoScreen(ScreenManager& mgr);

    ScreenId id() const override { return kScreenDojo; }

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

    // The between-rounds HUD "Next" button (JS `vhb` L410 case 1): the
    // fight holds in EndStance until the player confirms the next round.
    // `round_wait()` mirrors FightController::round_wait(); the button is
    // drawn + clickable only while it is true. `next_button_center` returns
    // the button's screen center (the position render_impl draws it at) —
    // the headless-loop driver injects its click there.
    bool round_wait() const;
    void next_button_center(float& cx, float& cy) const;

    // [trace, Phase 0] Arms the FightController's per-frame pose dump
    // (the first `frames` fight frames -> `path` JSONL). Defined in
    // screens.cpp (needs the full FightController type).
    void enable_pose_dump(const std::string& path, int frames);

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

    // --- on-screen gamepad (JS `Za` virtual controls, JS_GAMEPLAY §2) ----
    // The original's touch gamepad: the joystick `ze` (base + knob, the
    // pointer drags the knob -> a movement sector 1-8) bottom-left and the
    // attack buttons `fu` (punch/kick circles) bottom-right. The native
    // renders them from the ui/controller atlas (JoystickContainer_norm/
    // action, Joystick_norm/action, btn_punch_normal/action, btn_kick_
    // normal/action) and feeds the same fight_->player_input() path the
    // keyboard uses. Hidden while round_wait() (the Next button shows).
    bool pad_visible() const;          // false while round_wait()
    void update_gamepad_input();       // pointer -> joystick/button events
    void draw_gamepad(App& app) const; // the atlas-frame render
    // Joystick state: the knob drag (JS `ze.nia/Qgb/oia`).
    bool joy_grabbed_ = false;   // a pointer owns the joystick
    float joy_knob_x_ = 0.0f;     // knob offset from center (view px)
    float joy_knob_y_ = 0.0f;
    int joy_sector_ = 0;          // the active movement key 1-8 (0 = neutral)
    // Attack buttons: pressed state (JS `ig.nia/oia` -> frame swap).
    bool btn_punch_down_ = false;
    bool btn_kick_down_ = false;
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
