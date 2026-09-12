#pragma once

// The shell screens — Dojo, Map, Fight, Results, Shop, Profile (tabbed),
// Settings (minimal overlay).
//
// JS study (the per-screen wire-spec is reference/PORT_AUDIT_UI.md):
//   - Dojo (screen 3, `Tf` L1969-1972): the HOME base — the screen the
//     ORIGINAL boots into (Preloader -> Loader -> Dojo). It runs the
//     `FightNone` ModelViewer (the idle stance figure at the location's
//     ModelsViewer spawn) over the dojo location layer stack, with the
//     shared `za` top chrome. There is NO FIGHT button / punchbag / gear on
//     the JS hub (those were native inventions — PORT_AUDIT_UI §2.2).
//   - Top chrome (`za` L1972-1984): the persistent shell chrome mounted on
//     every shell screen via `ma.D1()` (L1831): a full-width `topPanel`
//     (misc id 260) at min(H*0.13,100), three widgets `wr`/`xr`/`yr`
//     (level/energy/money) centred in a row, and a VERTICAL column of five
//     `Le` nav buttons (menu id 262): Dojo/Map/Shop/Profile/Settings.
//     There is NO JS GeneralMenu screen — screen 8 does not exist in this
//     build (`dJ()` returns 0/3/4/5/6/7 only); the shell home is the Dojo.
//   - Map (screen 5, Ya L2124-2132): the battle-node screen. The backdrop is
//     `map<N>` where N = parseInt(FileName.split(".")[1]) - 1 (JS `qe.W0a`
//     L2143; res/map/partN json, the mapN frame is 2046x854). The battle
//     nodes come from stages.xml <Zone>/<Battle> with X/Y positions
//     (qe.X0a L2144: x = battle.x*uM + bg.w/2, y = -battle.y*uM + bg.h/2,
//     then -50; uM = 1.5003663003663004, JS L2488). Clicking a node starts a
//     fight.
//   - The battle-node button art is per node (JS `Qr` L2092-2095):
//     BattleBtn{Base|Active|Lock|LockActive|Pressed}/<prefix><Icon>, prefix
//     base_/active_/locked_/locked_active_/pressed_ (`Lc.*` L2482), Icon the
//     Battle Icon attr (default "training", L205).
//     There is NO zone tab strip and no BRACKET button in the JS map
//     (PORT_AUDIT_UI §2.3/§2.4): zone nav is the `Vr` scroller + the `Rr`
//     info panel / `Xr` status list (OPEN — not ported).
//
// The misc/menu/controller/fight-ui atlases are KTX ASTC — the data layer
// CPU-decodes them (core/data/ktx.cpp) and App::init registers their frames,
// so the `za` chrome art resolves. A flat fallback still covers a real
// per-frame miss (never a silent blank).

#include <memory>
#include <string>
#include <vector>

#include "app/fight_assets.hpp"
#include "app/act_player.hpp"
#include "app/item_catalog.hpp"
#include "app/save_system.hpp"
#include "app/screen_manager.hpp"

namespace sf2::data {
struct anim_clip;
} // namespace sf2::data
namespace sf2::scene {
class FightController;
class Fighter;
class LocationScene;
} // namespace sf2::scene

namespace sf2::app {

// The dojo — the home screen (native Dojo screen 3, JS `Tf`). The screen
// the game boots into: the dojo location layer stack + the `FightNone`
// ModelViewer (player idle + Punchbag enemy) + the shared `za` top chrome
// (nav column). The FIGHT button, the hand-drawn bag and the gear were
// native inventions (PORT_AUDIT_UI §2.2, §4); the Punchbag dummy is instead
// the viewer's real enemy model (`merged_bag`, DOJO_BG_STATIC §1/§6).
class DojoScreen : public Screen {
public:
    explicit DojoScreen(ScreenManager& mgr);

    ScreenId id() const override { return kScreenDojo; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

private:
    bool money_logged_ = false;

    // --- Dojo aliveness (JS `Tf` L1969-1972: the hub runs the `FightNone`
    // ModelViewer — the idle stance figure at the location's ModelsViewer
    // spawn, not a hand-placed capsule) -----------------------------------
    // The idle player figure: a scene Fighter sampling the stance clip
    // (display only — never stepped through fight logic). Built lazily on
    // first render; null-safe when assets are missing (headless).
    std::unique_ptr<sf2::scene::Fighter> dojo_fighter_;
    bool dojo_fig_tried_ = false;
    bool dojo_fig_ok_ = false;
    const sf2::data::anim_clip* dojo_idle_ = nullptr;  // owned by FightAssets
    int idle_frame_ = 0;   // fixed-step counter driving the idle cycle
    // [OPEN] The hub's Punchbag dummy (`merged_bag`, enemy spawn 973,-110,
    // `Bf.zjb` L476) is not drawn: the 876a3a97 bind-pose attempt rendered
    // nothing (bag ON vs OFF = +0.03pp) and was removed to restore the
    // pre-regression hub. Needs the bag's real clip/COM verified vs oracle.
    // Tutorial quest banner state (quest_panel.hpp; derived read-only from
    // the save's Tutorial field + the last Training result).
    std::string tutorial_ = "MOVE";
    std::string story_step_;  // _$StoryTutorialStep (landed save API, L105)
    std::string map_focus_;   // save MapFocus/ys (landed; feeds boss_focus)
    std::vector<std::string> battles_;  // save Battles/iF (landed; WDa/wins)
    int level_ = 1;
    int quest_logged_ = -1;
    bool training_won_ = false;
    int seen_money_ = -1;  // last logged money (snapshot change detection)
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
        std::string icon;      // Battle Icon (JS `Lc.icon`; default "training",
                               // L205) — the per-node `BattleBtn*` art suffix
        std::string zone;      // the stages.xml Zone Name (JS `st`)
        std::string location;  // the Battle Location (JS fight backdrop)
        std::vector<std::string> warriors;  // Fight Warriors FirstNames (Xs)
        float x = 0.0f;  // screen pos (center; JS `qe.X0a` L2144)
        float y = 0.0f;
        bool active = true;
    };

    // One zone tab (JS `st`, parsed by `p.Dkb` L188): the zone strip the map
    // renders (`Ya.HXa` L2123). `locked` is the shell stub rule (see the
    // MapScreen ctor) until the save carries Battles/iF for `WDa`.
    struct ZoneTab {
        std::string name;   // "Punchbag" / "ZONE_1" .. "ZONE_7"
        std::string file;   // FileName ("Map0.1" ..) — the zone backdrop id
        bool is_start = false;  // the `Start="1"` zone (JS `st.yR`)
        bool locked = false;
        int part = -1;  // res/map backdrop index (ZONE_1→part0 …; -1 = none)
        std::vector<Node> nodes;
    };

private:
    std::vector<ZoneTab> zones_;
    int zone_sel_ = 0;
    int hover_ = -1;
    int tab_hover_ = -1;
    // Tournament-series progress (save Fights/yc win counts, cached at
    // construction; the Map remounts every visit so it stays fresh).
    std::vector<WarriorSave::FightWins> fight_wins_;
    // Boss-intro act (JS hCa lD + Rd player): armed node + player while the
    // intro runs; the fight launches when done (headless bypasses).
    ActPlayer act_;
    Node act_node_;
    bool act_pending_ = false;
    // Shared battle-start body (JS `Ya` mp(6)): fills pending_battle and
    // pushes the fight. Used by node clicks and act completion alike.
    void launch_battle(const Node& n);
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
    // Pause dialog (JS `Jn` -> `Ar.Qg(0)` -> `Aia()` `Dr`, L2018/L425 —
    // UI-layer only): Esc/P or the HUD pause icon freezes the sim (update
    // skipped) and shows the `Dr` dialog (`res/fight/pause.*`: Pause title,
    // PauseMusic/PauseSound toggles, play=resume, home=quit). Never engages
    // headless (key/pointer driven; the loop injects neither here).
    bool paused_ = false;
    bool music_off_ = false;  // `Dr.PauseMusic_on/off` toggle state

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

    // --- round banner (JS `Cr` L2021-2026 — presentation only) -----------
    // The current banner's kind + the fight frame it was raised at (the
    // screen-side age drives the hold-forever VICTORY/DEFEAT pop-in — the
    // controller's banner_progress() divides by banner_len_, which is 1e9
    // for those, so their controller progress stays ~0).
    int banner_kind_seen_ = 0;      // banner_kind as int (0 = none)
    int banner_start_frame_ = 0;    // fight_->frame() when the banner changed
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
    // JS `OLa`/`Oz` (L253-254): exp needed to go level -> level+1
    // (`v.FR` = character_progress.xml `<Threshold>`; 100 fallback).
    static int exp_for_level(int level);

private:
    bool player_won_ = false;
    int money_reward_ = 0;
    int exp_reward_ = 0;
    bool applied_ = false;
    // Tutorial quest toast (quest_panel.hpp; derived read-only in update -
    // e.g. the first Training win nudges the player back to Sensei).
    std::string quest_toast_;
    // Prize breakdown snapshot (copied from PendingBattle in update — the
    // `v.kD`/`bzb` factor lines; render reads these, never the sim).
    int prize_base_ = 0;
    int prize_bonus_ = 0;
    int prize_combo_ = 0;
    int prize_shocks_ = 0;
    bool prize_perfect_ = false;
    bool prize_first_ = false;
};

// The shop — native Shop screen (screen 4, JS `Oa` g="468").
// The responsive `Oa.layout` `gb` split (L2293-2295) yields three JS rects:
//   viewer `c = b.fn(.75)`  -> the `Oe` card viewer (`Za.Pn(c)`, L2295),
//   right slot `d` (bc)     -> the item detail side panel,
//   left slot  `b` (b2)     -> the `MJ`/`op` side panels (params/enchant).
// Items are laid out by the `Oe`/`Gg` cell list (L2261-2262, L1883-1893): a
// single column of `ns` cells (L2303-2308) with per-category anchor `uw` and
// spacing `LT` (Oa.f5 L2286-2288: 50/20/100/50/50), drawing the real item
// image (JS `ns.j5` L2307 `Rf(Ye.qI(fileName))`). A grid click selects the
// item (`Oa.xA` L2296); the detail panel's action button is the `Fhb` L2300
// equip/buy path (`re.rga` L2285): owned+equipped -> UNEQUIP (`xa.Qxb`),
// owned -> EQUIP (`xa.$o`), else TRY (buy gate `Pa.iwa` L1228).
// The `Eg` bottom tab strip (`ss` L2283-2284) is kept.
class ShopScreen : public Screen {
public:
    explicit ShopScreen(ScreenManager& mgr);

    ScreenId id() const override { return kScreenShop; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

private:
    std::vector<CatalogItem> items_;
    int hover_ = -1;      // grid cell hover (row index within the tab)
    int sel_ = 0;         // selected grid row (JS `Oa.xA`/`Za.Ac` L2296)
    int side_hover_ = 0;  // 0 = none, 1 = detail action button (JS `Up`)
    int money_logged_ = 0;
    // --- `Gg` list scroll (JS L1883-1893): drag + momentum + snap --------
    // The `Oe` viewer's cell list is the JS `Gg` scroller: `scroll_y_` =
    // `ei.node.ra` (list-container y relative to the viewer top, shifted by
    // the drag), `scroll_vel_` = `ub`, `scroll_target_` = `targetY`,
    // `scroll_state_` = `state` (0 idle / 1 drag / 2 target lerp). The
    // top/bottom clamps mirror states 4/5 (`gj=uz` first-centred /
    // `gj=-last.ra+(size.y-last.h)/2` last-centred). Init re-runs when the
    // tab or row count changes (`Gg.VK` L1891).
    float scroll_y_ = 0.0f;
    float scroll_vel_ = 0.0f;
    float scroll_target_ = 0.0f;
    int scroll_state_ = 0;
    int scroll_tab_ = -1;
    int scroll_count_ = -1;
    float drag_start_ = 0.0f;  // `Fq` (pointer y at grab)
    float drag_base_ = 0.0f;   // `gj` (list y at grab)
    float drag_delta_ = 0.0f;  // `p_` (drag displacement, L1888)
    float drag_prev_ = 0.0f;   // previous local y (velocity source)
    float drag_vel_ = 0.0f;    // `ub = jM[0].y*.01` (L1888) -> fling
    // Shop tabs (JS `vj.E0` category ids 1..5 → `vj.ifa` tab lists, Oa L1168).
    // tab_ indexes kShopTabs (0 = Weapon); tab_hover_ is the tab hover.
    // seen_ is the last save snapshot (owned/equipped markers, refreshed in
    // update — render never touches disk).
    int tab_ = 0;
    int tab_hover_ = -1;
    WarriorSave seen_;
    // Buy confirmation (display-only): last bought item + the screen time
    // until which the confirmation line shows.
    std::string confirm_;
    float confirm_until_ = 0.0f;
};

// The Profile — native `vb` (JS L2189-2201, `dJ()==7`): a tabbed screen with
// the `cs` bottom tab strip (L2188: 4 `Le` on the profile atlas id 258,
// `Tw=[0,1,2,3]`). The active sub-view docks into the real `vb.layout`
// `a = b.fn(.75)` viewer rect (L2195). Tab routing mirrors `vb.hla`
// (L2190-2193): 0 -> `Rl=ds` (POWERLEVELING_SLIDER L2227) + the `XB=ei`
// header (`ivb()`); 1 -> `qv=es` (SKILLS_SLIDER L2239, `To.kOa`=11 L2201);
// 2 -> `Zr=fs` (ACHIEVEMENT_SLIDER L2213); 3 -> `lv=gs` (SEALS_SLIDER L2231).
// Tab 1 folds the Moves/skills list. Tab 3 (SEALS) is ported from the
// derivable `gs.uZ` filter (`p.o.xa.hJ(I.Vr)`, L2231): the save's owned
// `Type="Seal"` catalog rows, drawn with the `js` cell (`js.ba` L2232
// `oe(a.fileName)`). Tabs 0 and 2 stay OPEN with cites (the audit note they
// are not derivable 1:1, not that the data is missing): `ds` shows
// `id.ht().tH` tiers of `Ih` rows (`uZ` L2227) which `bya`/`dPa`/`cPa`
// (L1353-1357) merge from the `character_progress.xml` `<PerkTree>` (asset
// 1315, `td.Vib` L1160 `id.ht().parse(f)`) with `perks.xml` (`v.Rg`, asset
// 310) and the save's perk progression (`p.o.co.KS.Oa`); each tier renders
// the `tk` compare cell (`Rx` arrows + two `uk` level badges, L2217-2222) —
// neither pipeline nor cell art is modelled. `fs` shows `v.uv.tI`
// achievements (`Iv` L1175, parsed from `achievements.xml` asset 1356 via
// `td.Adb`/`Fib` L1160) joined with the save's `<Counters>`/`<Achievements>`
// (`p.o.yi` = `yt.parse`, L294, read at `this.yi.parse(a)` L250) through
// `cab` (L2216), rendered by the `hs`/`is` achievement cell (L2209-2213) —
// the counters/save join and cell art are OPEN. The invented equipment slot
// list + owned grid were removed (PORT_AUDIT_UI §3 #19, §4 #6): JS moves
// equip into the shop detail panel (`$o`) — now implemented in ShopScreen.
class EquipmentScreen : public Screen {
public:
    explicit EquipmentScreen(ScreenManager& mgr);

    ScreenId id() const override { return kScreenProfile; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

    // The folded Moves tab row (JS Profile sub-view `qv`; same rule as the
    // deleted standalone MovesScreen — build_move_list_locks over the save's
    // owned items, display only).
    struct MoveRow {
        std::string name;
        std::string type;
        int priority = 0;
    };

    // One `gs` seal row (JS `gs.TA` entries, L2231; cell `js` L2232).
    struct SealRow {
        std::string name;
        int count = 0;
        std::string image;  // list.xml Image ("drop_blue_seal") -> `oe` name
    };

private:
    int tab_ = 0;        // `cs` tab index (0 = `ds` leveling .. 3)
    int tab_hover_ = -1;
    int hover_ = -1;
    // Folded Moves tab data (JS Profile sub-view `qv`, To.kOa=11 L2201).
    std::string weapon_ = "Fists";
    std::vector<MoveRow> move_rows_;
    int move_total_ = 0;
    // Ported `gs` SEALS tab data (owned `I.Vr` rows, L2231).
    std::vector<SealRow> seal_rows_;
};

// The settings — the real JS `un extends od` dialog (L1916-1930), reached
// from the `za` nav button #5 (`za.Vfb` L1979 -> `Xc.Shb()` = `Wb.openDialog
// (310,null)` -> `new un`). 9-slice `od` base (`AV=fc(2340,1530)`, `Md=750`
// L1894/L1930) + title + Sound/Music/Credits/Language rows (gated by
// `Ca.hasFeature`) + BACK (`EButtonDark`) / RESTART (`EButtonBeige`). Music
// toggles via play/stop_music; BACK returns to the caller; Sound is state
// display (no runtime SFX mute API). OPEN: the per-language BMF atlas build
// (`G.Oq(253)`/`un.C8`, L1927) and the exact per-row offsets (need the
// `E.get(250)` frame sizes, L1917) are not modelled.
class SettingsScreen : public Screen {
public:
    explicit SettingsScreen(ScreenManager& mgr);

    ScreenId id() const override { return kScreenSettings; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

private:
    bool music_off_ = false;
    int hover_ = -1;
    int age_ = 0;  // frames since push (BACK press debounce)
};

// The shared item catalog (the shop list + the equipment item lookup).
// Loaded once from list.xml and cached.
std::vector<CatalogItem> load_catalog(App& app);
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
