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
    std::vector<ZoneEntry> zone_battles_;
    int selected_ = 0;
    float scroll_x_ = 0;       // horizontal scroll position
    float scroll_target_x_ = 0;
    const resf2::format::StageBattle* selected_battle_ = nullptr;
    std::string selected_zone_name_;
    int reward_money_ = 0;
    int reward_exp_ = 0;
    int fight_power_ = 0;
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
// The combat scene. Currently delegates to the same dojo rendering as
// MainMenu (the punching bag stands in for a real opponent). In the
// future this will load a proper enemy character and track win/loss.

class BattleScene final : public Scene {
public:
    SceneId id() const noexcept override { return SceneId::Battle; }
    void on_enter(SceneContext& ctx) override;
    void on_update(SceneContext& ctx) override;
    void on_render(SceneContext& ctx) override;
    void on_exit(SceneContext& ctx) override;
    bool on_quit_request(SceneContext& ctx) override;
private:
    uint32_t battle_timer_ms_ = 0;
    uint32_t guard_timer_ms_ = 0;  // prevent immediate transitions after entering
    static constexpr uint32_t kBattleMaxMs = 5 * 60 * 1000;  // 5 min timeout
    static constexpr uint32_t kGuardMs = 500;  // 500ms guard
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

