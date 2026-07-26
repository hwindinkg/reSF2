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

class ShopScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Shop; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
private:
    float scroll_y_ = 0.0f;  // vertical scroll offset for the item list
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
    static constexpr uint32_t kCharRevealMs = 30;  // 30ms per char
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

