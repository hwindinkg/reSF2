#pragma once

// Screen manager — the native port of the game's `mc.K` (JS_MAP §1.6,
// sf2.502f0946.js L122-127) and the screen-state base `$d` (L119-122).
//
// JS semantics ported here:
//   - `mc.K.stack[]` — a stack of screen states; the top is the current
//     screen. `mc.aa` (L124) runs the transition state machine (`pu` 1..3)
//     then updates every active state (`d.aa(a)` = `$d.aa` -> `elements.oja`).
//   - `$d` — the screen-state base: `state` (the `Te(n)` state machine,
//     L121), `active`, `time`, `elements` (a `Db` scene node), `Mr` (the
//     manager), `caller`, `info`. `$d.jI(cls, info)` pushes a new screen
//     (`this.Mr.Taa(cls, this, info)`, L120).
//   - `Taa(cls, caller, info)` (L126) pushes a screen onto the stack and
//     starts a transition; `B()` (L124) pops.
//
// The native port keeps the stack + update/render passes; the fade
// transitions are simplified (a fixed-duration cross-fade via the screen
// state, flagged below) — the JS `ae` transition classes are not ported
// verbatim. The screen `state` values follow `Te(n)` (L121): 0 = hidden,
// 2 = entering (active), 3 = active, 5 = leaving, 7 = destroyed.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sf2::app {

class Screen;
class App;
class ScreenManager;

// Screen ids — mirror of `xn` (JS L1167-1168). Only the ids the shell
// uses are exposed; the JS enum is 0=Preloader, 2=Loader, 3=Dojo, 4=Shop,
// 5=Map, 6=Fight, 7=Profile, 8=GeneralMenu, 9=Pvp.
enum ScreenId : int {
    kScreenPreloader = 0,
    kScreenDojo = 3,
    kScreenShop = 4,
    kScreenMap = 5,
    kScreenFight = 6,
    kScreenProfile = 7,
    kScreenGeneralMenu = 8,
};

// Screen-state values ($d.Te, JS L121: 2/3/5 active, else inactive).
enum ScreenState : int {
    kStateHidden = 0,
    kStateEntering = 2,
    kStateActive = 3,
    kStateLeaving = 5,
    kStateDestroyed = 7,
};

// The screen-state base — native `$d`.
class Screen {
public:
    explicit Screen(ScreenManager& mgr, std::string name);
    virtual ~Screen() = default;

    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    virtual ScreenId id() const = 0;
    const std::string& name() const { return name_; }

    // Update pass — called once per fixed 60 Hz tick (the JS `$d.aa(a)`
    // -> `elements.oja(a)`; the port calls `update_impl`).
    void update(float dt);
    // Render pass (the JS `$d.Ea(a)` -> `elements.nja(a)`).
    void render(App& app);

    // Push a new screen on top of this one (the JS `$d.jI(cls, info)`).
    void push(ScreenId id);

    // Transition helpers — the simplified port of `Te(n)`/`Dw(a)`/`ym(a)`.
    // The shell drives these from the manager; the JS fade is replaced by
    // an instantaneous switch (the exact `ae` fade is out of scope — the
    // screen flow is what matters this phase). Only the entering/active
    // states update; leaving/hidden screens are frozen beneath the top.
    void set_state(int state) {
        state_ = state;
        active_ = (state == kStateEntering || state == kStateActive);
    }
    int state() const { return state_; }
    bool active() const { return active_; }

    float time() const { return time_; }

    // Hook for the screen's own per-frame logic (JS subclasses override
    // `aa`). Default does nothing.
    virtual void update_impl(float dt) { (void)dt; }
    // Hook for rendering (JS subclasses override `Ea`). Default draws
    // nothing.
    virtual void render_impl(App& app) { (void)app; }

    ScreenManager& manager() const { return mgr_; }
    App& app() const;

protected:
    ScreenManager& mgr_;
    std::string name_;
    float time_ = 0.0f;
    bool active_ = false;
    int state_ = kStateHidden;
};

// The screen manager — native `mc.K` (JS L122-127).
class ScreenManager {
public:
    explicit ScreenManager(App& app) : app_(app) {}

    // Push a screen onto the stack (the JS `Taa`). The screen becomes the
    // top and is activated.
    void push(std::unique_ptr<Screen> screen);

    // Pop the top screen (the JS `B` on the top state). When the stack
    // becomes empty the app stays on a blank screen.
    void pop();

    // Update all active screens (the JS `mc.aa`, L124). The top screen's
    // update runs last (drawn on top).
    void update(float dt);

    // Render all screens back-to-front (the JS `mc.Ea`, L125).
    void render(App& app);

    Screen* top() const {
        return stack_.empty() ? nullptr : stack_.back().get();
    }

    // The current screen id (the JS trace's `mc.K.stack[top].DQ()`).
    int current_id() const {
        return top() != nullptr ? static_cast<int>(top()->id()) : -1;
    }

    const std::vector<std::unique_ptr<Screen>>& stack() const { return stack_; }

    App& app() const { return app_; }

private:
    App& app_;
    std::vector<std::unique_ptr<Screen>> stack_;
};

} // namespace sf2::app
