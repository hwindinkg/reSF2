// engine/scene/scenes.hpp
//
// Concrete Scene implementations for reSF2's game flow.
//
// Most scenes are lightweight stubs that render a placeholder overlay and
// handle input for scene transitions. The heavy lifting (character rendering,
// physics, combat) is done by the Game class (SceneHost) — the MainMenu and
// Battle scenes delegate to it.
//
// Scene flow:
//   Boot -> Loading -> MainMenu -> Map -> Dialogue -> Battle -> Results -> MainMenu
//
// From MainMenu, the scroll menu can also go to Shop, Settings (stubs).

#pragma once

#include <array>
#include <vector>

#include "scene_system.hpp"
#include "../format/stage_parser.hpp"

namespace resf2::scene {

// ---------- Boot scene ----------
// Minimal splash — just clears the screen for a few frames before transitioning
// to Loading. In the original game this shows the studio logo; we don't have
// that asset yet, so it's a brief black screen.

class BootScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Boot; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
private:
    uint32_t elapsed_ms_ = 0;
    static constexpr uint32_t kBootDurationMs = 500;  // 0.5s splash
};

// ---------- Loading scene ----------
// Shows a progress bar while assets warm up. Currently the asset loading is
// synchronous (happens in host_load_location), so this scene just displays
// a loading message and transitions to MainMenu after a short delay.

class LoadingScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Loading; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
private:
    uint32_t elapsed_ms_ = 0;
    bool loading_started_ = false;
    static constexpr uint32_t kMinDisplayMs = 800;  // min 0.8s display
};

// ---------- Main menu scene ----------
// This is the dojo with the punching bag — the game's main hub. The player
// can move, hit the bag, and open the scroll menu. From the menu, the player
// can go to Map (Story), Shop, Settings, or Dialogue (test).
//
// Rendering and input for the dojo itself is delegated to the SceneHost
// (the Game class). This scene adds the menu UI and handles menu-item clicks
// to transition to other scenes.

class MainMenuScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::MainMenu; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
    void on_exit(SceneContext& ctx) override;
    bool on_quit_request(SceneContext& ctx) override;
};

// ---------- Map scene (level selection) ----------
// Shows zones and battle nodes parsed from stages.xml.

class MapScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Map; }
    void update_selected_battle();
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
private:
    struct ZoneEntry {
        resf2::format::StageZone zone;
        std::vector<size_t> battle_indices;
    };
    // One drawable node: a battle that has map coordinates.
    struct Node {
        const resf2::format::StageBattle* battle = nullptr;
        std::string icon;          // "tournament", "lynx", ...
        std::string icon_fallback; // by Type, for names the atlas has no frame for
        std::string label;         // localized, upper case
        float x = 0, y = 0;  // map coordinates from stages.xml
        bool completed = false;
    };
    std::vector<ZoneEntry> zone_battles_;
    std::vector<Node> nodes_;      // nodes of the zone currently on screen
    int selected_ = 0;             // zone index
    int selected_node_ = 0;        // index into nodes_
    float scroll_x_ = 0;           // horizontal pan of the map sheet, in pixels
    float scroll_target_x_ = 0;
    float max_scroll_ = 0;
    const resf2::format::StageBattle* selected_battle_ = nullptr;
    std::string selected_zone_name_;
    int reward_money_ = 0;
    int reward_exp_ = 0;
    int fight_power_ = 0;

    bool want_centre_ = false;   // pan the sheet onto the selection next render
    // [L1] Last "[MAP] round_progress" line printed, so the per-frame
    // progress log only emits when the state actually changed.
    std::string last_round_progress_log_;
    // Hit boxes {x, y, w, h} of the nodes as last drawn. Computed in
    // on_render, where the map transform is known, and read by on_update.
    std::vector<std::array<float, 4>> node_hit_;
    // Hit boxes of the page dots along the bottom, same idea.
    std::vector<std::array<float, 4>> dot_hit_;
    // Dragging the sheet. drag_moved_ separates a pan from a tap on a node.
    bool drag_active_ = false;
    float drag_last_x_ = 0.0f;
    float drag_moved_ = 0.0f;

    void rebuild_nodes(SceneContext& ctx);
    void select_node(size_t i);
    int zone_sheet_index() const;
};

// ---------- Shop scene ----------
// [ORIGINAL] Matches ShopScreen @ 0x1021f170. Layout from the reference
// screenshot: a MENU scroll roll across the top; on the body, from left to
// right: a fighter silhouette panel with a "TRY ON" button and navigation
// arrows, a central parchment scroll showing the currently equipped items
// with star ratings, and a right-hand detail panel with the selected item's
// name, stat bars, and a green BUY button. A bottom bar carries the currency
// readouts on the left and the category icons on the right.

class ShopScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Shop; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;

    // [Wave 9B] Test probes (re-soak-5): what the centre column is rendering
    // and which item is selected. Mirrors the render, so the tests measure
    // exactly what the screen draws.
    std::vector<std::string> visible_row_names(SceneContext& ctx) const;
    std::string selected_item_name(SceneContext& ctx) const;

private:
    // Category tab state
    int selected_category_ = 0;        // index into categories_
    std::vector<std::string> categories_ = {"Weapon", "Armor", "Helm", "Ranged", "Magic"};

    // Item list state
    int selected_item_idx_ = 0;        // index within the current category's items
    float scroll_offset_ = 0.0f;       // scroll offset for item list

    // [ORIGINAL] Reference-derived proportions (768-pt design space).
    // Top panel: 192 pt (kTopPanelAtlasH, same as dojo/menu).
    // MENU scroll roll: 56 pt.
    // Bottom bar (currency + category icons): 80 pt.
    // Body is split horizontally into three columns:
    //   left fighter column  ~ 28 % of logical width
    //   centre scroll column ~ 40 %
    //   right detail column  ~ 32 %
    static constexpr float kTopPanelH    = 192.0f;
    static constexpr float kMenuRollH    =  56.0f;
    static constexpr float kBottomBarH   =  80.0f;
    static constexpr float kDesignH      = 768.0f;
    static constexpr float kFighterFrac  =   0.28f;
    static constexpr float kScrollFrac   =   0.40f;
    static constexpr float kDetailFrac   =   0.32f;
    static constexpr int   kVisibleRows  =   3;   // item rows in the central scroll window
    static constexpr float kCatIconSize  =  52.0f;
};

// ---------- Settings scene (stub) ----------

class SettingsScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Settings; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
};

// ---------- Dialogue scene ----------
// Shows a dialogue box with speaker name and text. Click or press Space to
// advance. When all lines are exhausted, transitions to Battle (or back to
// MainMenu if no battle follows).

class DialogueScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Dialogue; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
private:
    size_t current_line_ = 0;
    uint32_t text_reveal_ms_ = 0;  // for typewriter effect
    // [HEURISTIC-TODO] 30 ms/code-point is a perceptual guess.  The binary's
    // QuestActionDialog render path (FUN_101dcc40, vtable[4]) delegates text
    // display to the engine's label animation system; no per-character delay
    // constant was found near FUN_101c7d20 or in sf2_beautified.js (searched
    // for "typewriter", "charDelay", "textSpeed" — none).  30 ms ≈ 33 chars/s
    // reads comfortably at 60 fps.  Adjust after video-capturing the original.
    static constexpr uint32_t kCharRevealMs = 30;
};

// ---------- Battle scene ----------
// The combat scene: an enemy fighter driven by the AI, round tracking per
// stages.xml (<Fight Rounds= RoundTime=>), and the fight HUD drawn by the
// host. A round ends when a fighter dies or the round timer runs out; the
// match ends when one side has the majority of rounds.

class BattleScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Battle; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
    void on_exit(SceneContext& ctx) override;
    bool on_quit_request(SceneContext& ctx) override;
private:
    void finish_round(SceneContext& ctx, bool player_won);
    int round_time_ms_ = 99000;     // per-round budget from BattleInfo
    int round_left_ms_ = 99000;     // counts down within the round
    int wins_player_ = 0;
    int wins_enemy_ = 0;
    int rounds_total_ = 1;
    uint32_t between_rounds_ms_ = 0;  // pause before the next round starts
    uint32_t guard_timer_ms_ = 0;  // prevent immediate transitions after entering
    static constexpr uint32_t kGuardMs = 500;  // 500ms guard
    static constexpr uint32_t kBetweenRoundsMs = 1500;
};

// ---------- Results scene ----------
// Post-battle screen. Shows victory/defeat overlay with rewards and
// a "Continue" button that transitions to Map or MainMenu.

class ResultsScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Results; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
private:
    uint32_t guard_ms_ = 0;
    static constexpr uint32_t kGuardMs = 400;
    int reward_gold_ = 0;
    int reward_xp_ = 0;
    bool is_victory_ = false;
    bool saved_ = false;
};

// ---------- Profile scene ----------
// Shows player stats: level, wins/losses, inventory summary.

class ProfileScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Profile; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
};

}  // namespace resf2::scene

