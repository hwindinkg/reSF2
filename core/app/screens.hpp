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

// The on-screen gamepad's per-screen interaction state (JS `ze` joystick +
// `fu` attack buttons). ONE instance per screen that shows the pad; the
// shared `update_pad_input` in screens.cpp turns pointer events into
// `FightController::player_input` edges for both the fight and the dojo.
struct PadInputState {
    bool joy_grabbed = false;   // a pointer owns the joystick
    float joy_knob_x = 0.0f;    // knob offset from center (view px)
    float joy_knob_y = 0.0f;
    int joy_sector = 0;         // the active movement key 1-8 (0 = neutral)
    bool btn_punch_down = false;
    bool btn_kick_down = false;
};

// The keyboard's directional state (JS `gu` keyboard + `Za.bbb` bindings).
// `phys` holds the four logical movement directions, each with TWO physical
// slots (primary WASD / desktop arrow alias): 0=up(W/Up), 1=forward(D/Right),
// 2=down(S/Down), 3=back(A/Left). The JS binds DIAGONAL key-pairs BEFORE the
// cardinals (`bbb()` `De(2,0,key(1),key(3))` ... before `De(1,0,key(1))`), so
// W+D selects up_forward(2), not up(1)+forward(3). ONE instance per screen
// that owns a fight controller (the fight and the Dojo `FightNone` viewer).
struct KeyInputState {
    bool phys[8] = {};  // [dir*2 + alias] held
    int sector = 0;     // last movement control emitted (1-8, 0 = neutral)
};

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

    // Keyboard -> the hub's own controller. The dojo is a real `FightNone`
    // fight (JS `Tf` L1971 `this.Ig=v.m1a(a)`), so a key edge must reach it
    // through the SAME `player_input` path the drawn pad uses. Without this
    // override the base `Screen::on_key` swallowed every key and only the
    // on-screen pad worked.
    void on_key(int glfw_key, bool down) override;

    // The desktop key-alias gate (arrows/Space) - the SAME flag every other
    // screen uses (`Af.oUa` L2472 binds only the ten JS keys; the arrows/Space
    // are a deliberate desktop affordance, ON by default).
    void set_desktop_key_aliases(bool on) { desktop_key_aliases_ = on; }
    bool desktop_key_aliases() const { return desktop_key_aliases_; }

    // --- test/replay hooks (`--input-tape` dojo pad evidence) --------------
    // The hub's `FightNone` controller state. Null-safe: false/""/0 until the
    // controller exists.
    bool dojo_fight_ready() const;
    std::string dojo_player_move() const;
    float dojo_player_x() const;
    float dojo_player_y() const;
    int dojo_fight_frame() const;
    // The hub player's clip frame (JS `Te.M0()`). The pad-punch probe uses it
    // to prove the punch lands OUTSIDE the running move's `Uninterrupt`
    // interval (FrontFlip 4..23, moves.xml) before it asserts the punch move.
    int dojo_player_move_frame() const;
    // The last control the hub's keyboard produced (0 = unbound/swallowed);
    // the dojo keyboard-parity evidence (JS `Za.bbb` diagonal pairs).
    int dojo_last_key_type() const;

private:
    bool money_logged_ = false;

    // --- Dojo aliveness (JS `Tf` L1969-1972: the hub runs the `FightNone`
    // battle through a REAL `ca` — `this.Ig=v.m1a(a)`, `aa(): this.YL(Ig,a)`
    // steps it every frame) -------------------------------------------------
    // The hub's fight is built like any battle (`ca.ggb` L383): the player
    // from the save's gear, the enemy = the zone's first `FightNone` warrior
    // — the Punchbag training bag (`NotAI=1 NotAnimation=1`, stages.xml
    // L12-16 -> `merged_bag` bind pose). It is put straight into phase 2
    // (`xF(2)`, JS `kg` L387) with NO round flow (no ROUND/FIGHT plate, no
    // timer, no KO). The drawn gamepad feeds it through the SAME
    // `player_input` path the fight uses. Built lazily; null-safe when assets
    // are missing (headless).
    std::unique_ptr<sf2::scene::FightController> dojo_fight_;
    bool dojo_fight_tried_ = false;
    bool dojo_fight_ok_ = false;
    PadInputState dojo_pad_;  // the shared on-screen gamepad interaction state
    KeyInputState dojo_keys_;  // the hub's keyboard directional state (JS `gu`)
    // The desktop key-alias gate (arrows/Space), the SAME flag every other
    // screen uses (ON by default; see `set_desktop_key_aliases` above). The
    // hub previously passed `aliases=true` unconditionally and ignored it.
    bool desktop_key_aliases_ = true;
    int dojo_last_key_type_ = 0;
    // Builds `dojo_fight_` at the current dojo location (JS `Tf.init` L1971).
    void build_dojo_fight(App& app);
    // Tutorial quest banner state (quest_panel.hpp; derived read-only from
    // the save's Tutorial field + the last Training result).
    std::string tutorial_ = "MOVE";
    std::string story_step_;  // _$StoryTutorialStep (landed save API, L105)
    std::string map_focus_;   // save MapFocus/ys (landed; feeds boss_focus)
    std::vector<std::string> battles_;  // save Battles/iF (landed; WDa/wins)
    int level_ = 1;
    int quest_logged_ = -1;
    bool training_won_ = false;
    std::int64_t seen_money_ = -1;  // last logged money (snapshot change detection)

    // --- Fresh-profile tutorial (JS `StoryTutorialWelcome` chain) ----------
    // The approved `fresh/tutorial-from-0` boot. The beats are NOT hand-coded:
    // the app-layer QuestEngine loads the shipped
    // `quest_extensions/tutorial_quests.xml` and queues the `He` dialogs
    // (`Notification` move/punchbag + the `Regular` characterSensei
    // training-fight modal); the shared modal layer (screens.cpp
    // `quest_modal_consume`) displays them, and the modal's FIGHT button runs
    // the dialog's deferred `Fight Name="Punchbag|Bosses|1"` action here.
    // `launch_quest_fight` resolves the triple through stages.xml (the same
    // `load_zone_map`/`battle_rewards` path the Map uses) and pushes the real
    // tutorial battle (bamboo_grove, 2 x 99 s rounds).
    void launch_quest_fight(const std::string& triple);
};

// One boss-intro roster entry (JS `jk.init` L2062 iterates the `lD` boss
// battle list: `g.Hf` = the warrior portrait, `g.$s` = its name).
struct BossRosterEntry {
    std::string name;
    std::string image;
};

// Fidelity-tour hook (main.cpp): while on, MapScreen draws the boss-intro
// roster (`jk`, JS L2061-2065) full-screen and freezes input, so
// `--fidelity-tour` can capture `act_boss` headlessly (the JS shows `jk` at
// the start of a multi-fight boss battle; `act_boss` is the oracle matrix's
// name for that roster capture). `state` selects the frozen pose: 3/4 = the
// resting selection (`act_boss`), 1 = mid horizontal scroll-in.
bool force_boss_roster();
void set_force_boss_roster(bool on);
// The frozen `jk` pose the forced roster holds (3/4 = `act_boss`, 1 = mid
// scroll-in for the extra capture step).
void set_force_boss_state(int state);
int force_boss_state();

// JS `jk` (L2061-2065) — the boss-intro OPPONENT SCROLL. `ai.aa` case 0
// (L2007) runs `lca(this.TF.lD, this.TF.uP, this.TF.Y1)` when the boss battle
// has more than one `<Fight>`: `this.Ws = this.Qo(jk); this.Ws.init(a,b,c,…)`
// (L2009), and the scroll's `qd` signal (state 4) -> `ngb()` -> `tx()` (the
// `ik` VS intro). Five states, all driven by the `ed(t)` ease:
//   0 `scrollX=Pp; bQ.node.wa(ed(.1))`                     fade-in  (0.1 s)
//   1 `scrollX = Pp + (512 - Jq[index].node.ya - Pp)*ed(e)` scroll-in
//     (`e` = 1 s when `index` is the LAST entry, else 2 s)
//   2 `time>.5`                                            hold     (0.5 s)
//   3 `ed(.5)`: the selected `la(from -> to)` (1->1.4, or 1.4->2 at the LAST
//     index) and every other entry's `Hf.node.wa(1 -> .5)`  (0.5 s)
//   4 `ed(1.5)==1` -> `this.qd.Z()` (start the fight)       (1.5 s)
// per-frame `bQ.C(this.scrollX); bQ.D(512)`; `this.yY = 4.6` (L2063) is the
// sum of the five stages (0.1+2+0.5+0.5+1.5).
struct BossRosterScroll {
    // Screen-space (1280x720) adaptation of the JS 1024-design geometry:
    // `g = h.Hf.size` = the entry art at scale 1 = 353 (the pinned 1280-space
    // canvas), so the row pitch `g*.8` = 282.4 and the selected `la(1.4)` =
    // 353*1.4 = 494 (`kSelD` 504). The JS `512` centre is the screen centre
    // 640 = `kViewW*0.5f`.
    static constexpr float kBaseD = 353.0f;
    static constexpr float kStep = 282.4f;   // `d += g*.8` pitch (L2062)
    static constexpr float kCentre = 640.0f; // the JS (512,512) canvas centre
    static constexpr float kTotal = 4.6f;    // `this.yY=4.6` (L2063)

    std::vector<BossRosterEntry> entries;
    int index = 0;
    int state = 0;
    float time = 0.0f;
    float scroll_x = 0.0f;
    float row_alpha = 0.0f;
    float Pp = 0.0f;
    bool started = false;
    bool done = false;

    bool active() const { return started && !done; }

    // `jk.init(a,b,c,d)` L2062-2063. `idx` = `this.index = b`.
    void start(std::vector<BossRosterEntry> e, int idx) {
        entries = std::move(e);
        const int n = static_cast<int>(entries.size());
        index = n == 0 ? 0 : (idx < 0 ? 0 : (idx >= n ? n - 1 : idx));
        state = 0;
        time = 0.0f;
        row_alpha = 0.0f;
        done = false;
        started = true;
        // L2063: `Pp = -(Jq[last].node.ya - Jq[0].node.ya)` then
        // `-= Jq[0].Hf.size/2` and `-= Jq[last].Hf.size/2`.
        const float span = static_cast<float>(n > 0 ? n - 1 : 0) * kStep;
        Pp = -span - kBaseD;
        scroll_x = Pp;  // case 0: `this.scrollX = this.Pp`
    }

    // `Jq[i].node.ya` — the entry's row x (L2062 `h.node.C(d); d += g*.8`).
    float entry_x(int i) const { return static_cast<float>(i) * kStep; }

    // `ed(t)` (L2064): the shared 0->1 ease over `t` seconds.
    static float ed(float t, float elapsed) {
        if (t <= 0.0f) return 1.0f;
        float x = elapsed / t;
        if (x < 0.0f) x = 0.0f;
        if (x > 1.0f) x = 1.0f;
        return 1.0f - (1.0f - x) * (1.0f - x);
    }
    bool is_last() const { return index >= static_cast<int>(entries.size()) - 1; }
    float stage_a() const {
        if (state < 3) return 0.0f;
        if (state > 3) return 1.0f;
        return ed(0.5f, time);
    }
    float selected_scale() const {
        const float from = is_last() ? 1.4f : 1.0f;
        const float to = is_last() ? 2.0f : 1.4f;
        return from + (to - from) * stage_a();
    }
    float other_alpha() const { return 1.0f - 0.5f * stage_a(); }

    // Advances one fixed step. Returns true on the single frame the state-4
    // ease completes (`this.qd.Z()` L2064 — start the fight).
    bool tick(float dt) {
        if (!active()) return false;
        if (dt < 0.0f) dt = 0.0f;
        time += dt;
        switch (state) {
            case 0:
                scroll_x = Pp;
                row_alpha = ed(0.1f, time);
                if (ed(0.1f, time) >= 1.0f) {
                    time = 0.0f;
                    ++state;
                }
                break;
            case 1: {
                const float e = ed(is_last() ? 1.0f : 2.0f, time);
                scroll_x = Pp + (kCentre - entry_x(index) - Pp) * e;
                if (e >= 1.0f) {
                    time = 0.0f;
                    ++state;
                }
                break;
            }
            case 2:
                if (time > 0.5f) {
                    time = 0.0f;
                    ++state;
                }
                break;
            case 3:
                if (ed(0.5f, time) >= 1.0f) {
                    time = 0.0f;
                    ++state;
                }
                break;
            case 4:
                if (ed(1.5f, time) >= 1.0f) {
                    row_alpha = 1.0f;
                    done = true;
                    return true;
                }
                break;
            default:
                break;
        }
        return false;
    }
};

// The map — native Map (screen 5).
class MapScreen : public Screen {
public:
    explicit MapScreen(ScreenManager& mgr);
    ScreenId id() const override { return kScreenMap; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

    // The `Rr` info-panel FIGHT button centre (`tj`, JS L2099/L2102) — the
    // ONLY fight trigger on the map (a node tap only re-targets the panel).
    // The headless-loop/tour's battle-start step clicks this.
    void fight_button_center(float& x, float& y) const;

    // `Ur` zone-dot strip geometry (JS L2112-2116, `qk.layout` L2137): the
    // centre of the dot drawn for zone `zi`, or false when that zone renders
    // none (`Vr.HXa` L2123-2124). The draw AND the click hit-test both read
    // this, so the rect a player taps is exactly the rect that was painted.
    bool zone_dot_center(std::size_t zi, float& cx, float& cy) const;
    // The zone the map is showing (`Vr` selection; seeded from the save's
    // CurrentZone and written back on a dot tap).
    int zone_selected() const { return zone_sel_; }

    struct Node {
        std::string name;
        std::string alias;     // stages.xml Alias — the JS `Qr.Bka(a.Cg)`
                               // label key (L2144; `Lc.Cg` = Alias, L1403).
        std::string type;
        std::string icon;      // Battle Icon (JS `Lc.icon`; default "training",
                               // L205) — the per-node `BattleBtn*` art suffix
        std::string title;     // stages.xml Title lang key (JS `Lc.k6` — the
                               // `Rr` info-panel title, L2103)
        std::string preview;   // stages.xml Preview, stem after the last '.'
                               // (JS `Lc.olb` -> `Me.CT` `res/map/images/
                               // <stem>.img`, L2105/L2111)
        std::string zone;      // the stages.xml Zone Name (JS `st`)
        std::string location;  // the Battle Location (JS fight backdrop)
        int fight_count = 0;   // <Fight> count (JS `Lc.Kz().length`, L2134):
                               // the `Xr` status pip count is this - 1 for
                               // boss types (`Xr` ctor L2134)
        int reward_money = 0;  // first <Reward Money> (the `ci`/`bi` gold icon
                               // value shown under the difficulty bar, L2133)
        std::vector<std::string> warriors;  // Fight Warriors FirstNames (Xs)
        // The `<Fight Name>` per ladder slot (JS `dl.name`), in order. The
        // `Xr` pip row (L2133-2136) draws one pip per rendered fight (boss
        // types drop the last) and lights it (`indicatorOn`, `Ox.wMa(0)`)
        // when that fight's `dl.status==1` (`YL` L111266: the `il` record's
        // `CompletedCount >= <Fight Replays>`). `pip_beaten[k]` is that bit.
        std::vector<std::string> fight_names;
        // Per-`<Fight>` `Locked` attribute (JS `dl.locked`, the `il` parse
        // `a.locked=u.ka(b.attributes.get("Locked"),!1)`; `Xr` L2134
        // `c[k].locked?l.wMa(2)`), parallel to `fight_names`.
        std::vector<bool> fight_locked;
        std::vector<bool> pip_beaten;
        float x = 0.0f;  // screen pos (center; JS `qe.X0a` L2144)
        float y = 0.0f;
        bool active = true;
        // JS `Lc.tt()` (L1406) / `hl.tt` (L278): the `<Battle>` record's
        // `Locked` attribute. `Qr` (L2092) picks the `BattleBtnLock/locked_`
        // frame from THIS, not from `active` (`Qr.lla` L2094 hides the
        // record-less node instead).
        bool locked = false;
        // JS `Qr.lla` (L2094): the button draws only while
        // `hs.isActive && !a.li()` (a save `<Battles>` record exists and is
        // not Hidden/expired). `alt_state` marks the `*_INTERMISSION` /
        // boss hard-mode twins — hidden until they have a record, which is
        // what keeps their labels off the base node's.
        bool visible = true;
        bool alt_state = false;
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
    // Boss-intro act (JS `ai` L2007 `lca(TF.lD, TF.uP, TF.Y1)` -> `jk`): the
    // roster scroll runs on the FIGHT press for a multi-`<Fight>` boss battle,
    // then launches the battle when state 4 fires `qd` (L2064/L2009).
    BossRosterScroll roster_;
    Node act_node_;
    // True while the fidelity-tour `force_boss_roster` hook owns the machine;
    // when it turns off the frozen roster is dropped so the REAL flow (or the
    // live map) re-arms cleanly instead of ticking the stale frozen pose.
    bool roster_forced_ = false;
    // The `qo` focus-refresh marker (JS `Ya.Uw` L2129): the last
    // `SetMapFocus Battle=` the quest engine applied. A focus landing AFTER
    // this screen was constructed (the StoryTutorialBossFight SceneLoaded
    // fire, tutorial_quests.xml L155) re-targets `Rr` on the live map.
    std::string applied_focus_;
    // Re-targets `Rr` to the node named in a MapFocus string (the `Ya.Uw` +
    // `ue.tea` focus rule, incl. the BOSSES/first-visible fallbacks).
    void apply_map_focus(const std::string& battle);
    // The plate rect for live map-button registry index `i` (JS `Wr.qFa`
    // L2179). Shared by the draw and the hit test so the rect a player taps is
    // exactly the rect that was painted.
    void map_button_rect(std::size_t i, float& cx, float& cy, float& w,
                         float& h) const;
    // Shared battle-start body (JS `Ya` mp(6)): fills pending_battle and
    // pushes the fight. Used by node clicks and act completion alike.
    void launch_battle(const Node& n);
    // The shared `jk` gate for EVERY battle start (JS `ai.aa` case 0, L2007):
    // a boss battle with more than one `<Fight>` (`this.TF.lD.length>1 &&
    // this.TF.eE`) arms the opponent scroll and only launches when its state 4
    // fires `qd` (`ngb()` -> `tx()`, L2009); every other battle launches
    // directly. Both real entries run through here — the `Rr` FIGHT button and
    // the quest modal's deferred `Fight` action — so the real Map->FIGHT entry
    // cannot skip the scroll.
    void start_battle(const Node& n);
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
    // = the player's items (Type / SubType / Name) for the Locks move list;
    // EMPTY means "resolve from the save" (`owned_items`) so the direct boot
    // and the Map/Dojo launch build the identical list.
    FightScreen(ScreenManager& mgr, const std::string& battle_name,
                const std::string& location, int reward_money, int reward_exp,
                const std::vector<sf2::scene::OwnedItem>& owned);

    ScreenId id() const override { return kScreenFight; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

    // Keyboard -> game key types (JS `Ik` key events -> the fight input).
    void on_key(int glfw_key, bool down);

    // The GLFW -> game key_type id map (0 = unbound), from the JS `sc.OD`
    // table (`Af.oUa` L2472) — EXACTLY the ten keys the JS binds. Exposed so
    // the key-map verification can assert the binding without running a fight.
    static int key_type_for_glfw(int glfw_key);

    // The desktop-only aliases (Left/Right/Up/Down movement, Space=Punch) —
    // NOT part of `Af.oUa`. The browser JS binds only the ten `Af.oUa` keys
    // (W/A/S/D + K/L/O/P/J/Q, L2472); a desktop player reaches for the arrow
    // keys and Space first, and `App::poll_input` (app.cpp) already POLLS
    // GLFW_KEY_LEFT/RIGHT/UP/DOWN/SPACE/ESCAPE/ENTER and routes them here.
    // Dropping them silently made the player's own keys dead in the windowed
    // build ("controls barely respond"), so the desktop map is ON by default.
    static int desktop_alias_for_glfw(int glfw_key);

    // The desktop key map toggle. DEFAULT ON: the windowed desktop port
    // accepts the JS ten keys PLUS the arrows/Space/Esc. Turn it OFF to get
    // the byte-exact `Af.oUa` table only (the `--input-tape js` fidelity run,
    // the `--verify-input` key-map assertion).
    void set_desktop_key_aliases(bool on) { desktop_key_aliases_ = on; }
    bool desktop_key_aliases() const { return desktop_key_aliases_; }

    // The key_type id the LAST accepted key produced (0 = the key was not
    // bound / was swallowed). The input-tape harness reads it to report which
    // physical keys actually reach the fight.
    int last_input_key_type() const { return last_input_key_type_; }

    // The player's current move name ("" when idle-less). The input-tape
    // harness logs it per frame to show the move -> idle flip.
    std::string player_current_move() const;

    // The ENEMY's current move name and each fighter's reported facing
    // (`Fighter::facing()`, the pose dump's `fx`). The `--verify-place` victim
    // probe reads them to log what the throw's victim actually does.
    std::string enemy_current_move() const;
    float player_facing() const;
    float enemy_facing() const;

    // [probe, authorised] The ENEMY's live hit-reaction state for the
    // `--boss-hit-probe` (no OS input): the ragdoll latch (JS `Al.nk`, set by
    // `ragdoll_start` on a landed hit and cleared by `Al.stop` at the next
    // move start), its frame count / reaction name, and the enemy's
    // started-move counter (an AI replacement would bump it). Read-only.
    bool enemy_ragdoll_active() const;
    int enemy_ragdoll_frame() const;
    std::string enemy_ragdoll_name() const;
    int enemy_moves_started() const;

    // Test/replay hook: inject a game key edge by key_type id (1..14) into
    // the same `player_input` path the keyboard uses, bypassing the GLFW key
    // map. The `atframe <n> press <control>` replay stream uses these ids.
    void inject_game_key(int key_type_index, bool down);

    // Test hooks (`--verify-place`): move both fighters to explicit world x so
    // a probe can force either facing (`Ae.Wl`), and read the resulting
    // positions / clip frame back. No behaviour change.
    void place_fighters(float me_x, float enemy_x);
    float player_world_x() const;
    float enemy_world_x() const;
    int player_move_frame() const;
    // [probe] Round-transition frame log hooks (JS `XF` L370): the whole 3-D
    // view's visibility (`camera_.visible`, the M2 round-transition hide) and
    // the current camera centre x. Read-only — no behaviour change.
    bool scene_visible() const;
    float camera_center_x() const;
    // Clears the player's current move (`Fighter::clear_move`) so a probe
    // starts from a neutral state.
    void reset_player_move();

    // The player's move-list size (the equipment-change evidence: the
    // headless-loop driver logs it before/after equipping a weapon).
    // Defined in screens.cpp (needs the full FightController type).
    std::size_t move_list_size() const;

    // Test hooks for the input replay/verification harness (no behavior
    // change): the player's last decision and started-move count.
    std::string player_last_decision() const;
    int player_moves_started() const;

    // The player's last move decision (JS `Gc.DK` `c == false`, L673-674),
    // rendered as `cands=<name>@<prio>,... f=<name>,... draw=<v>|- idx=<i>
    // <picked> ukb=<name>`. "" when no decision has run. The
    // `--verify-input` probes assert this whole record.
    std::string player_decision() const;

    // The owned rows this screen actually used (see `player_owned_`), and the
    // ordered player move-list names joined with "," (the boot-vs-Map
    // comparison). Both are test/replay hooks — no behavior change.
    const std::vector<sf2::scene::OwnedItem>& resolved_owned() const {
        return player_owned_;
    }
    std::string move_list_digest() const;

    // [fidelity] The fight controller's frame counter (JS `ca.frame`). The
    // fidelity tour uses it to capture a fight state at a deterministic frame
    // (the oracle fight captures are pinned to `fight.frame`). -1 before the
    // controller exists.
    int fight_frame() const;

    // [fidelity] The `ik` VS-intro overlay state (JS `ik`, L2069-2071): the
    // overlay covers the scene for `ik.yY` = kVsTotal seconds while the native
    // sim runs underneath (JS creates the fight only AFTER `ik.kg`, L2071).
    // A fight capture must therefore wait for the overlay before framing, and
    // the `fight_intro` capture must land INSIDE the composed hold. `vs_time`
    // is the overlay clock in seconds (JS `ik.time`, reset per `kd` stage).
    bool vs_active() const;
    float vs_time() const;

    // [FIX Phase 4a verification] Prints the sampled bone positions of the
    // player/enemy (a clip-frame bone-sample check) + their triangle bbox
    // (the stretched/on-screen check). Defined in screens.cpp.
    void verify_fight() const;

    // The between-rounds gate (there is NO Next button — the JS round
    // auto-advances): true from a round's end until the break plate
    // (`Cr.tca` L2023) expires into `FNa`. The on-screen gamepad is hidden
    // while it is true (JS `Ta.XF(!1)`), and the headless drivers' tap
    // target (`next_button_center`) is the inert (0,0).
    bool round_wait() const;
    void next_button_center(float& cx, float& cy) const;

    // [--flow-verify] The fight pause `Dr` dialog is open (`paused_`). The
    // row geometry is fixed by the screens.cpp `kPauseDlg*` constants (HUD
    // pause icon 640,117 68px; row y=396; Music x=561.25, Sound x=718.75,
    // tile 135) — the driver clicks those constants directly, exactly as the
    // headless-loop shop step clicks `shop_price_rect`'s centre.
    bool pause_dialog_open() const { return paused_; }

    // [trace, Phase 0] Arms the FightController's per-frame pose dump
    // (the first `frames` fight frames -> `path` JSONL). Defined in
    // screens.cpp (needs the full FightController type).
    void enable_pose_dump(const std::string& path, int frames);

private:
    std::string battle_name_;
    std::string location_;
    int reward_money_ = 0;
    int reward_exp_ = 0;
    // The owned items this screen actually built the player's move list from
    // (the ctor param, or `owned_items(app())` when it was empty). The
    // boot-vs-Map comparison reads it back.
    std::vector<sf2::scene::OwnedItem> player_owned_;
    std::unique_ptr<sf2::scene::FightController> fight_;
    bool results_pushed_ = false;
    bool key_state_[16] = {};
    // The keyboard directional state (JS `gu` + the `Za.bbb` key-pair table).
    KeyInputState keys_;
    int last_log_frame_ = 0;
    bool auto_attack_wired_ = false;
    // Pause dialog (JS `Jn` -> `Ar.Qg(0)` -> `Aia()` `Dr`, L2018/L425 —
    // UI-layer only): the HUD pause icon freezes the sim (update skipped) and
    // shows the `Dr` dialog (`res/fight/pause.*`: Pause title,
    // PauseMusic/PauseSound toggles, play=resume, home=quit). The Esc key
    // toggle is a desktop-only alias (NOT in `Af.oUa`), live whenever the
    // desktop key map is on (the default).
    bool paused_ = false;
    bool music_off_ = false;  // `Dr.PauseMusic_on/off` toggle state
    // The desktop key aliases (arrows/Space/Esc). DEFAULT ON: the default
    // desktop key map is the JS `Af.oUa` ten keys PLUS the arrows/Space/Esc
    // the windowed player will press (see `desktop_alias_for_glfw`).
    bool desktop_key_aliases_ = true;
    // The last accepted key's key_type id (0 = unbound/swallowed); the
    // input-tape harness's per-key evidence.
    int last_input_key_type_ = 0;

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
    // The joystick/button interaction state (JS `ze.nia/Qgb/oia`, `fu.nia/oia`),
    // read by the shared `update_pad_input` and by `draw_gamepad`.
    PadInputState pad_;

    // --- VS intro (JS `ik`, g="419", L2069-2074) --------------------------
    // The pre-fight VS screen the oracle `fight_intro` shows: the full-screen
    // `vs/bg` backdrop, the two warrior portraits (`oe(a.Hf)`/`oe(b.Hf)`)
    // sliding in from the sides, the `vs/sprites` "left"/"right" strokes and
    // the "vs" glyph, and the two localized names (`Yeb/Xeb`, gold). JS
    // creates it in `ai.tx()` (L2008) and only starts the fight after `ik.kg`
    // fires; the port keeps the sim ticking underneath and covers it fully.
    // Presentation-only: render + one timer, no sim hooks.
    //
    // NOTE (capture alignment): `ik.yY` = kVsTotal = 3.4 s (JS L2071) and the
    // composition completes at the `kd7` name stage (~1.7 s), so the composed
    // hold runs ~1.7..2.9 s (`kVsNameT`..`kVsFadeT`). The fidelity driver gates
    // `fight_intro` on this clock and every other fight capture on `!vs_active`
    // (the JS fight `ca.frame` starts only after the overlay) — see
    // `kVsTotal`/`kVsFadeT`/`kVsNameT` in screens.cpp.
    float vs_t_ = 0.0f;              // seconds since the fight screen opened
    bool vs_active_ = true;
    std::string vs_player_name_;
    std::string vs_enemy_name_;
    std::string vs_player_image_;    // users/images stem (JS `Hf`)
    std::string vs_enemy_image_;
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
    // Prize breakdown snapshot (copied from PendingBattle in update — the
    // `v.kD`/`bzb` factor lines; render reads these, never the sim).
    int prize_base_ = 0;
    int prize_bonus_ = 0;
    int prize_combo_ = 0;
    int prize_shocks_ = 0;
    bool prize_perfect_ = false;
    bool prize_first_ = false;
    // Per-category bonus COINS (`Fh.lXa` `oc.P3/ep/Ui/DZ/Ub`): the Results
    // rows show these; `prize_combo_`/`prize_shocks_` are the counts (`jU`/
    // `e6`) that label them (`PERFECT ×{0}`).
    int prize_perfect_coins_ = 0;
    int prize_first_coins_ = 0;
    int prize_combo_coins_ = 0;
    int prize_style_coins_ = 0;
    int prize_shock_coins_ = 0;
    // `oc.OY` ruby (`Fh.lXa` arg `c`, L2054-2055; `oc.mOa = oc.OY`). The
    // goldPrize row's `Or.x_` (`Lr.ZMa` L2078) renders it as the `Qw` sub-row.
    int prize_ruby_ = 0;
    // JS `Lr`/`Or` reveal clock (L2057-2081): the `kk` results container
    // holds the list for 500 ms (`kk.rxa` `wh.delay(...,500)`), then row `i`
    // slides in (`Or.aa` case 0, `ed(.5)` = 500 ms) and counts up (case 1,
    // 500 ms), each row starting when the previous slide lands; the star row
    // (`Pr`) never slides and counts over `ed(1)` = 1 s. The list therefore
    // settles at 4.5 s and the OK plate appears only then (`Lr.XMa`/`bza`).
    float reveal_t_ = 0.0f;
    bool reveal_done_ = false;
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

    // `Oa.uLa(a,b)` (JS L1181866) = `f5(tab)` + `Za.SA(item)`: switch to the
    // tab named by the `vj.E0` category (Weapon/Armor/Helm/Ranged/Magic/…) and
    // select the item by list.xml Name (`` = first row). Returns false for an
    // unknown tab (the caller logs; no screen change).
    bool open_at(const std::string& tab, const std::string& item);

    // `Oa.Imb()` (L1181282) minus the pane/`refresh` plumbing the port folds
    // into `render_impl`: `jAa()` refills the item lists and `f5(tab)`
    // re-selects the current tab. Re-reads the catalog + the save snapshot.
    void refresh_items();

    // The live `Oa.Hg` shop tab index (probe/verify read).
    int tab() const { return tab_; }

private:
    std::vector<CatalogItem> items_;
    int hover_ = -1;      // grid cell hover (row index within the tab)
    int sel_ = 0;         // selected grid row (JS `Oa.xA`/`Za.Ac` L2296)
    int side_hover_ = 0;  // 0 = none, 1 = detail action button (JS `Up`)
    std::int64_t money_logged_ = 0;
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
    // (The old green `confirm_`/`confirm_until_` post-purchase toast was an
    // invention: JS `Pa.iwa` L1228 renders no caption on success. Removed.)
    // --- `Pi` purchase panel (`Oa.Fhb` L2300 unowned branch) --------------
    // `Oa.Fhb` L2300: unowned -> `this.Ad.qr.addListener(this.yS);
    // this.Ex(a,7); this.Ad.aa(L.K.sk.Bm); this.sab()`, with `this.Ad=new Pi`
    // (L2291). The panel's CONFIRM control is the `M8` GoldButton (`Ne.Wub`
    // L2254 `kL(this.M8, Aa.jp(), …)` = `EButtonGreen` + the `p.o.Vf` gold
    // icon): the quest buy (`Ao` L1119-1120) wires exactly `M8.pa` ->
    // `Ao.Qg` -> `Pa.iwa(b)` + `xa.$o` + `rb.U3()` (snd_buy). `Oa.yS` L2300
    // (fired from `Pi.qr`/`Jc.qr`) is the close: `Ad.$Ma(); fU(); Oya=!0`.
    // `buy_armed_` = the row index whose panel is open, -1 = plain detail.
    int buy_armed_ = -1;
    // JS `p.o.qC` (world ctor L247: `u.ka(a.attributes.get("ShowUpgrades"),false)`
    // on the WARRIOR node — a SAVE attribute; `users_default.xml` ships
    // `ShowUpgrades="0"`). The second half of the upgrade-button gate
    // (`this.k9 && p.o.qC`, `Ne.Wub` L2255). The port's `WarriorSave` does not
    // carry it, so it is read from the save file once per `refresh_items`
    // (the JS reads it once at world construction — this is no more dynamic).
    bool show_upgrades_ = false;
    // --- Backdrop = the persistent dojo scene ----------------------------
    // JS `Oa extends ma` (L2285) is an overlay on the running dojo location,
    // so the oracle `shop_tab1..5`/`shop_detail` captures show the dojo
    // interior + the `FightNone` idle figure behind the shop UI
    // (PORT_AUDIT_UI §2.4; the old `_0015_bg` sky sprite was the wrong art).
    // Rendered with the same `assets.dojo` layer stack + `ma.Sya` framing as
    // the DojoScreen hub (`Tf.init`/`Tf.Ea` L1971-1972).
    std::unique_ptr<sf2::scene::Fighter> backdrop_fighter_;
    bool backdrop_fig_tried_ = false;
    bool backdrop_fig_ok_ = false;
    const sf2::data::anim_clip* backdrop_idle_ = nullptr;  // owned by FightAssets
    // --- `Pi` try-on preview (`Oa.Fhb` L2300 unowned -> `Ex(a,7)` L2301) ----
    // The unowned press wears the item on the `Pi` model and plays its `TryOn`
    // clip (JS `iz.XBa("TryOn")=7` L444) BEFORE any purchase; the buy is the
    // `M8` plate (`Pa.iwa` L1228 / `ZYa` L2251) wired from the panel below.
    // The body is merged into `preview_model_` (this screen's storage), so the
    // shared FightAssets::merged — the dojo/fight body — is never rebuilt.
    std::unique_ptr<sf2::scene::Fighter> preview_fighter_;
    sf2::scene::Model preview_model_;
    const sf2::data::anim_clip* preview_clip_ = nullptr;  // owned by FightAssets
    int preview_frame_ = 0;
    bool preview_active_ = false;

    // `Oa.Fhb` L2300 unowned -> `this.Ex(a,7)` (L2301): build the preview body
    // wearing `it` and load the item's `TryOn` move clip. Preview-owned
    // storage; no shared asset is touched.
    void arm_preview(App& app, const CatalogItem& it);

    // `Ne.ZYa` L2251 (`Pa.iwa(this.Ch) && p.o.xa.$o(this.Ch,!0)`) = the shop's
    // BUY + EQUIP at the `M8` price plate, gated by `Pa.iwa` L1228
    // (`p.o.Tb >= a.jp()`, else `v.Bv(a,2)`). Returns true when accepted.
    bool purchase_price_plate(App& app, const CatalogItem& bit);
    // `Ne.Ehb` case 2 L2254 -> `bka(0, Aa.nn())` -> `Pa.EYa` L1228: the `pVa`
    // RubyButton buys with the Ruby/crystal balance (`p.o.fd`).
    bool purchase_gem_price_plate(App& app, const CatalogItem& bit);
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
// `oe(a.fileName)`). Tab 0 (PERK TREE) is ported: `ds.uZ` L2227 reads
// `id.ht().tH` tiers built by `bya`/`dPa`/`cPa` (L1353-1357) from the
// `character_progress.xml` `<PerkTree>` (asset 1315, `td.Vib` L1160
// `id.ht().parse(f)`) joined with `perks.xml` (`v.Rg`, asset 310) and the
// save's `<PerkHistory>` (`p.o.co.KS.Oa`); each tier is the `tk` compare
// cell (`Rx` arrows + two `uk` level badges, L2217-2222). Tab 2
// (ACHIEVEMENTS) is ported: `fs.uZ` L2214 lists `v.uv.tI` groups (`Iv`
// L1175, from `achievements.xml` asset 1356 via `td.Adb`/`Fib` L1160)
// joined with the save `<Counters>`/`<Achievements>` (`p.o.yi` = `yt.parse`
// L294, read at `this.yi.parse(a)` L250) through `cab` (L2216), rendered by
// the `hs`/`is` cell (L2209-2213). The `tk`/`is` atlas art (profile 258/270)
// is ASTC; a genuine frame miss falls back to the flat row (the same
// never-silent rule as the rest of the shell). The invented equipment slot
// list + owned grid were removed (PORT_AUDIT_UI §3 #19, §4 #6): JS moves
// equip into the shop detail panel (`$o`) — now implemented in ShopScreen.
class EquipmentScreen : public Screen {
public:
    explicit EquipmentScreen(ScreenManager& mgr);

    ScreenId id() const override { return kScreenProfile; }

    void update_impl(float dt) override;
    void render_impl(App& app) override;

    // `vb.rF(a,b)` (L1131579): `this.hla(a)` (select profile slot `a`) then
    // `this.jq.Dr(b)` (focus item `b`). `slot` is `To.hOa(vj index)`; returns
    // false when the slot has no shell tab (the shell renders 4: 0..3).
    bool select_tab(int slot, const std::string& focus);
    // The live profile tab index (probe/verify read).
    int tab() const { return tab_; }

    // --- [probe] `--settings-profile-shop-probe` (ii) --------------------
    // The folded Moves tab selection (`vb.uj` L2198 -> `umb`/`$r.refresh`
    // L2183/L2234). `select_move` performs the SAME `move_sel_ = index`
    // assignment `update_impl`'s captured-hit test performs (the `vb.hqb`
    // L2198 click); `shown_move` returns exactly the name the `$r` right
    // panel draws (the SELECTED row, not `move_rows_.front()`).
    void select_move(int index);
    std::string shown_move() const;
    int move_row_count() const { return static_cast<int>(move_rows_.size()); }

    // The folded Moves tab row (JS Profile sub-view `qv`; same rule as the
    // deleted standalone MovesScreen — build_move_list_locks over the save's
    // owned items, display only).
    struct MoveRow {
        std::string name;
        std::string type;
        int priority = 0;
        // JS `Ru` (L1253) fields added by `Fa.Ueb` L712 from the move's
        // `<Profile>` child: `image` = `Ye.qI(Icon)` (the `skills` atlas
        // frame, drawn by `ls`/`Ed.ZL` L2203), `keys` = `KeysDescription`
        // (`ls.ymb` L2238 label), `rank` = `v4` (the `es.uZ` L2239 order).
        std::string image;
        std::string keys;
        int rank = 0;
        // `ra.Ul` (L712) document order - the STABLE tie-break of the
        // `es.uZ` (L2239) `pb(a.v4,b.v4)` sort when `Rank` values are equal.
        int order = 0;
    };

    // One `gs` seal row (JS `gs.TA` entries, L2231; cell `js` L2232). The
    // cell (`js.j5` L2233) draws ONLY the `oe(a.fileName)` image — there is
    // no name/count text in the JS. `a.fileName` is the list.xml `Image`
    // attribute (`pL` L322 `this.fileName = a.attributes.get("Image")`).
    struct SealRow {
        std::string name;   // list.xml Item Name (the `gs.TA` identity)
        std::string image;  // list.xml `Image` == the JS `a.fileName`
    };

    // One `ds` perk-tier row item (JS `id.ht().tH` tiers of `Ih`, L1353/2227;
    // the `tk` cell L2217-2222 shows up to two per tier). `tier` = the
    // PerkTree `<Level Value>`; `kind` = the XML tag ("Perk"/"Upgrade",
    // `id.k7a` L1355); `learned_level` = the save `<PerkHistory>` level
    // (`p.o.co.KS.Oa`); `available` = `Mw.K1()` (L1358).
    struct PerkRow {
        int tier = 0;             // PerkTree <Level Value>
        std::string name;         // perk Name (perks.xml / character_progress.xml)
        std::string kind;         // "Perk" (type 1) or "Upgrade" (type 2)
        std::string image;        // perks.xml Image ("Icons01.IconAvenger")
        std::string description;  // perks.xml Description key
        int learned_level = 0;    // save progression level (0 = not learned)
        bool available = false;   // Mw.K1
        int upgrade_max = 0;      // `Lc.Tc` (`Be.Tc`): the def's max
                                  // UpgradeLevel value (character_progress.xml
                                  // `<UpgradeLevel Value>`; written as `Ji.Ce`)
        // `Ih.type` (`id.f8a` L1357): 1 = Perk, 2 = Upgrade (0 = neither).
        int type = 0;
        // `Ih.Be` (L1371) — the `uk` cell state machine (L2222/L2224). Built
        // by `id.bya`/`Txb`/`dzb` (L1353-1356): 0 = learn target, 1 = sibling
        // at a learned tier, 2 = the learned cell, 3 = owned/placeholder.
        int state = 3;
        // `Ih.PQ()` = `Lc.Tc` (L1371): the matched `UpgradeLevel` def
        // (`j0a` L1190). `e8a`/`lnb` (L1353-1357) leave `history_count+1`
        // while defs remain, else 0 (the base def `Tc=0`, L1328-1329).
        int pq = 0;
        // `p.o.co.KS.Oa` records for this name (`id.cPa` L1353).
        int history_count = 0;
    };

    // One `fs` achievement row (JS `fs.El` of `Ba(def, value)`, L2214-2216;
    // cell `is` L2212). Definition from achievements.xml, value from the
    // save `<Counters>`/`<Achievements>` join.
    struct AchievRow {
        std::string name;         // Achievement Name (lang key)
        std::string description;  // Description (lang key, `{n}` placeholders)
        std::string icon;         // Icon ("Achievements01.ach_x" -> '/' path)
        int target = 0;           // CounterValue (`xw.counter`)
        int value = 0;            // displayed progress (`is.QZ`)
        bool completed = false;   // `xw.completed` (`yt.Yua` L297) / value>=target
        int money_prize = 0;      // MoneyPrize (`xw.AE`)
        int bonus_prize = 0;      // BonusPrize (`xw.dP`)
        bool reward_available = false;  // `xw.yj` (`Ir` L1248)
    };

private:
    int tab_ = 0;        // `cs` tab index (0 = `ds` leveling .. 3)
    int tab_hover_ = -1;
    int hover_ = -1;
    // Folded Moves tab data (JS Profile sub-view `qv`, To.kOa=11 L2201).
    std::string weapon_ = "Fists";
    std::vector<MoveRow> move_rows_;
    int move_total_ = 0;
    // Moves-tab selection (JS `vb.uj`/`hqb` L2198 -> `$r.refresh` L2234): the
    // selected `ks`/`ls` cell index drives the `$r` right panel. The old port
    // pinned the panel to `move_rows_.front()`; `es.Upb` (L2239) defaults the
    // selection to row 0 / the last-used move, so 0 matches the initial state.
    int move_sel_ = 0;
    int move_hover_ = -1;
    // Cell hit rects captured during render (the `perk_cell_hits_` pattern) so
    // update_impl hit-tests the SAME layout the renderer produced.
    struct MoveCellHit {
        float cx = 0.0f;
        float cy = 0.0f;
        float half_w = 0.0f;
        float half_h = 0.0f;
        int index = -1;
    };
    std::vector<MoveCellHit> move_cell_hits_;
    // `cs` tab badges (JS `Eg.GU` L1853 -> `Le.badge.lk(getCounterValue)`).
    // Index = the `cs.Tw` tab id; `cs.getCounterValue` (L2189) defines each
    // one (0 `co.uCa` L305, 1 `sCa` L256, 2 `yi.rCa` L294, 3 `vCa` L256).
    // `Dg.lk` (L1850) hides a zero badge (`node.R(a > 0)`), so all-zero rows
    // render nothing - which is exactly the shipped fresh-save state.
    int tab_badges_[4] = {0, 0, 0, 0};
    // Ported `gs` SEALS tab data (owned `I.Vr` rows, L2231).
    std::vector<SealRow> seal_rows_;
    // Ported `ds` PERK TREE (tab 0) rows, L2227.
    std::vector<PerkRow> perk_rows_;
    // Ported `fs` ACHIEVEMENTS (tab 2) rows, L2213-2216.
    std::vector<AchievRow> achiev_rows_;

    // --- Backdrop = the persistent dojo scene (JS `vb extends ma`) ---------
    // Like `Oa` (L2285), `vb` (L2189) is an overlay on the running dojo
    // location: the oracle `profile_tab*`/`moves` captures show the dojo
    // interior + the `FightNone` idle figure behind the parchment UI
    // (PORT_AUDIT_UI §2.5). Same `assets.dojo` layer stack + `ma.Sya` hub
    // framing as the DojoScreen hub / ShopScreen (the old `dojo_sprite` +
    // flat dim was the wrong backdrop).
    std::unique_ptr<sf2::scene::Fighter> backdrop_fighter_;
    bool backdrop_fig_tried_ = false;
    bool backdrop_fig_ok_ = false;
    const sf2::data::anim_clip* backdrop_idle_ = nullptr;  // owned by FightAssets

    // `uk` cell hit rects captured during render (so update_impl hit-tests
    // the SAME wrapping layout the renderer produced). `index` = perk_rows_.
    struct PerkCellHit {
        float cx = 0.0f;
        float cy = 0.0f;
        float half = 0.0f;
        int index = -1;
    };
    std::vector<PerkCellHit> perk_cell_hits_;
    int perk_sel_ = -1;    // `vb.uj` (`hqb` L2198): the selected `uk` cell
    int perk_hover_ = -1;
    int player_level_ = 1;  // `p.o.bb()` (the `uk.zo` level gate, L2223)
    int achiev_hover_ = -1;
    // `Bt.L1a`/`Qua` (L306-307): is the selected row buyable (JS `Be==0`
    // learnable + `!zo` level gate + `uwa()`)? Then `perk_buy` performs the
    // save write (`<Perks>` + `<PerkHistory>`).
    bool perk_buyable(int index) const;
    void perk_buy(int index);
    // `vb.exb` L2199 / `yt.sca` L296: is the row's reward claimable?
    bool achiev_claimable(int index) const;
    void achiev_claim(int index);
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

    // JS `od.aa` (L1895: `this.V7||this.oEa||this.Aqa||!L.K.Tj().Db(156)`)
    // applied to `un extends od` (L1916): the dialog closes on the Escape
    // key-down edge. JS menus are pointer-only — NO arrow-key navigation is
    // added here (that would be invention).
    void on_key(int glfw_key, bool down) override;

private:
    int age_ = 0;  // frames since push (press debounce)
};

// --- D13/D15 the Settings `un` dialog (JS L1916-1930) -----------------------
// `Xc.Shb()` (L931) = `Wb.openDialog(310,null)`; `Wb.Xob` case 310 (L926)
// builds `new un`, a `Wb` dialog appended to the ACTIVE screen (L927). The `za`
// nav button #5 (`Vfb` L1981) opens it OVER the current screen — it does NOT
// navigate (`ma.Jg().jI(11)` was the port's invention). `SettingsScreen`
// (the native `make_screen(kScreenSettings)` path) hosts the same dialog.
bool settings_dialog_open();
void open_settings_dialog(App& app);
void close_settings_dialog();
// `un.rHa` case 4 (L1931): advance `$u` through `iv` (L2477
// `"en de it fr pt ru es tr ja ko"`); RESTART is revealed iff `$u != G.Rq()`.
void settings_dialog_cycle_language(App& app);
// D15: `Nm`/`Km` visibility (`R(t9)`/`X(t9)`) — hidden until a language change.
bool settings_dialog_restart_visible();

// [--flow-verify] The Settings `un` dialog Music/Sound row centre + hit size
// (`un` icon rect, screens.cpp `settings_layout()`). `music=false` selects the
// Sound row. Read-only geometry; only meaningful while the dialog is open.
bool settings_bus_row_center(bool music, float& cx, float& cy, float& w, float& h);
// [--flow-verify] The largest `DeliveryTime` (`Ec`) across the shop-visible
// catalog. 0 when no shipped row is a timed-delivery order, i.e. the
// `Pa.iwa` `Ec>0` (`Pa.y2a` -> `snd_upgrade`) branch is unreachable from the
// price plate and only the immediate `Pa.gI` -> `snd_buy` branch can be
// driven. Read-only catalog scan.
int catalog_max_delivery_sec(App& app);
// JS `lb.WT(a){ta.WT(a);p.TJ.save()}` / `lb.VT(a){ta.VT(a);p.TJ.save()}`
// (L1276): EVERY bus-mute toggle writes the save immediately. The Settings
// `un` rows and the fight pause `Dr` rows (`tp`/`Sla`, L2066-2067) both call
// this; the pause rows previously set the bus only and lost it on reload.
void persist_bus_mutes(App& app);

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

// --- quest live-action helpers (quest_engine.cpp) --------------------------
// `Xn`/`Pa` catalog type lookup for `OpenShop Tab="?Purchase[X].Type"`
// (tutorial_quests.xml L107): the item's list.xml Type, or "" when unknown.
std::string catalog_item_type(App& app, const std::string& item_name);

// `eo.N3a` (L1117 `za.instance.sxa()` -> `scroll.collapse(0)`, L2001): drives
// the shared `za` nav scroll flag (the collapsed header then carries the
// `MenuBtnFlashing` pulse until the player expands it).
void set_za_nav_open(bool open);
// The `za` column's expanded state for a shell screen (`gk.uJ`) — asserted by
// the `--flow-verify` probe for the Map/Shop header tap.
bool za_nav_expanded(ScreenId id);
// JS `lca(TF.lD, TF.uP, TF.Y1)` (L2009): the `<Fight>` the boss ladder is on —
// the wins recorded for the battle, clamped to its `<Fight>` count, so the
// quest journal's `_$Fight` is `zone|name|(index+1)`.
int map_fight_index(App& app, const std::string& name, int fight_count);
// `Wc.NAa(v.Gz(fight))` (JS L2163): the `Wc` difficulty LEVEL index (0..4) for
// a fight, resolved from the SAME rating the Map's `Wc` bar uses
// (`map_battle_rating_cached` -> `map_difficulty_level`). `fight_triple` is the
// `hb` triple `zone|battle|fight`; the battle's `<Fight>` index is the triple's
// 1-based fight ordinal minus 1 (the `map_fight_index` convention). Returns -1
// when the triple is malformed (the JS `p.Wv` miss -> `a=-1`).
int map_fight_difficulty_level(App& app, const std::string& fight_triple);
// The live Map screen's `Ur` strip, for probes/drivers — null-safe (returns
// false / -1 when the Map is not the top screen).
bool map_zone_dot_center(App& app, std::size_t zi, float& cx, float& cy);
int map_zone_selected(App& app);

// `go.Thb` (L1092) + `Oa.uLa` (L1181866): open the Shop at the `vj.E0` tab
// name and select the item by name. Pushes the Shop when it is not current;
// otherwise re-points the live screen. Returns false for an unknown tab.
bool shop_open_at(App& app, const std::string& tab, const std::string& item);

// `Po` `UpdateShopItems` (L570290) -> `Oa.get().Imb()`: refresh the LIVE shop
// (refill lists + re-select the tab). No-op (returns false) when the shop is
// not the current screen, matching `a!=null && a.Imb()`.
bool shop_refresh_items(App& app);

// --- quest dialog (He) display/dispatch contracts (screens.cpp) -------------
// The action-plate frame for a `<Button Color>` (`He.lea` L1063 ->
// `nz.hi` L1840): Red->"btnDark", Green->"btnGreen", White/Beige->"btnWhite",
// Gold->"btnGold". `primary` (the Right slot) defaults an empty/unknown colour
// to White; the secondary slots (Left/Middle/Close) default to Dark — the
// `od.jR` L1899 primary `EButtonWhite` / secondary `EButtonDark` convention.
const char* quest_button_frame(const std::string& color, bool primary);

// --- `He.jkb` L1056-1057 row buttons (`this.ima`, id from `this.eOa=5`) -----
// A row carrying `Item`/`Enchantment` becomes a `tv` pushed to `this.ima` with
// `id=this.eOa++`; `He.dhb` L1061 `a<this.eOa` finds it by id and runs its
// nested `Yb`. One clickable box per such row; `slot` is the `dhb` id (5+row).
struct EngineDialog;
struct QuestDialogRowButton {
    int slot = 0;  // `tv.id` = `this.eOa++` (the first row button is 5)
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;  // the row's body box (screen px)
};

// The modal's row-button boxes for `d` (empty when no row carries an
// Item/Enchantment). `Multiline`/`MultilineBig` stack every row (`uj.sqb`
// L1953); the paged `Od` shows the current page row alone (`Od.Xma` L1948).
std::vector<QuestDialogRowButton> quest_dialog_row_buttons(App& app,
                                                           const EngineDialog& d);

// The `He.dhb` L1061 row id (`>=5`) of the row box under (x, y), or -1.
// `Od.Jsb`/`Od.xx` L1948-1949 dispatch `this.Ge(row id)` on the delivery
// countdown's expiry; the port has no countdown model, so the row's own box
// is the tap target.
int quest_dialog_row_hit_index(App& app, const EngineDialog& d, double x, double y);

// `--dialog-verify` headless self-check (no OS input, no pixels): queues the
// crafted `He` dialogs and asserts the D1/D2/D7 display + dispatch contracts,
// printing `[dlgverify] PASS/FAIL <case>` per case. Returns true only when
// every case passes.
bool run_quest_dialog_selfcheck(App& app);

// `--settings-profile-shop-probe`: the three shell behaviours added in
// `e567dcb8` asserted against the real code paths (no OS input, hidden window
// + RULE 0 watchdog from the driver) — (i) the Settings Credits row opens the
// credits view; (ii) selecting a Moves cell changes the `$r` right panel's
// shown move; (iii) the shop cell draws the sale badge on the RUBY tab.
// Prints one `[sps]` line per assertion and returns the FAIL count.
int run_shell_probe(App& app);

} // namespace sf2::app
