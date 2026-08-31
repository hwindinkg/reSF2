// Screen manager implementation (native `mc`/`$d` port, JS L119-127).

#include "app/screen_manager.hpp"

#include <cstdio>

#include "app/app.hpp"
#include "app/screens.hpp"

namespace sf2::app {

Screen::Screen(ScreenManager& mgr, std::string name) : mgr_(mgr), name_(std::move(name)) {}

App& Screen::app() const { return mgr_.app(); }

void Screen::update(float dt) {
    if (!active_) {
        return;
    }
    time_ += dt;
    update_impl(dt);
}

void Screen::render(App& app) { render_impl(app); }

void Screen::push(ScreenId id) { mgr_.push(make_screen(mgr_, id)); }

void ScreenManager::push(std::unique_ptr<Screen> screen) {
    if (screen == nullptr) {
        return;
    }
    // The JS transition (ae) deactivates the covered screen: when a new
    // screen is pushed, the previous top goes to the inactive "leaving"
    // state (Te(5)) and only the new top updates. The shell mirrors that
    // with an immediate switch.
    if (!stack_.empty()) {
        stack_.back()->set_state(kStateLeaving);
    }
    screen->set_state(kStateActive);
    std::fprintf(stdout, "[screen] push %s (id=%d) — stack now %zu\n", screen->name().c_str(),
                 static_cast<int>(screen->id()), stack_.size() + 1);
    std::fflush(stdout);
    stack_.push_back(std::move(screen));
}

void ScreenManager::pop() {
    if (stack_.empty()) {
        return;
    }
    Screen* popped = stack_.back().get();
    std::fprintf(stdout, "[screen] pop %s (id=%d) — stack now %zu\n", popped->name().c_str(),
                 static_cast<int>(popped->id()), stack_.size() - 1);
    std::fflush(stdout);
    popped->set_state(kStateDestroyed);
    stack_.pop_back();
    // The screen beneath (the JS "caller") reactivates.
    if (!stack_.empty()) {
        stack_.back()->set_state(kStateActive);
    }
}

void ScreenManager::update(float dt) {
    // The JS mc.aa iterates the stack; only the active (top) screen gets
    // its update pass (`$d.active` gates `d.aa(a)` in the JS L125). The
    // covered screens stay frozen beneath the transition.
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        if (stack_[i] != nullptr && stack_[i]->active()) {
            stack_[i]->update(dt);
        }
    }
}

void ScreenManager::render(App& app) {
    for (std::size_t i = 0; i < stack_.size(); ++i) {
        if (stack_[i] != nullptr) {
            stack_[i]->render(app);
        }
    }
}

} // namespace sf2::app
