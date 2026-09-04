// Screen manager implementation (native `mc`/`$d` port, JS L119-127).

#include "app/screen_manager.hpp"

#include <cstdio>

#include "app/app.hpp"
#include "app/quest_engine.hpp"
#include "app/screens.hpp"

namespace sf2::app {

namespace {

// JS scene names for the quest journal (`xn.iOa`, JS_MAP L432): Results has
// no JS screen (shown inside Fight) so it maps to Fight; unknown ids map to
// "" and skip firing (no misfire on empty comparisons).
std::string quest_scene_name(ScreenId id) {
    switch (id) {
        case kScreenDojo: return "Dojo";
        case kScreenShop: return "Shop";
        case kScreenMap: return "Map";
        case kScreenFight: return "Fight";
        case kScreenProfile: return "Profile";
        case kScreenGeneralMenu: return "GeneralMenu";
        case kScreenResults: return "Fight";
        default: return "";
    }
}

// Fires ChangeTab + SceneLoaded for a navigation edge (JS `v.qwa` L1212 +
// `wa.ghb` L934). Never throws, never navigates (engine records only).
void quest_nav(App& app, const std::string& from, const std::string& to) {
    if (to.empty()) return;
    try {
        QuestJournal j;
        j.scene_from = from;
        j.scene_to = to;
        try {
            j.player_level = app.save().load().level;
        } catch (const std::exception&) {
        }
        app.quest_engine().fire(app, "ChangeTab", j);
        app.quest_engine().fire(app, "SceneLoaded", j);
    } catch (const std::exception&) {
    }
}

} // namespace

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
    const ScreenId pushed_id = screen->id();
    const std::string nav_from =
        stack_.empty() ? "" : quest_scene_name(stack_.back()->id());
    std::fprintf(stdout, "[screen] push %s (id=%d) — stack now %zu\n", screen->name().c_str(),
                 static_cast<int>(screen->id()), stack_.size() + 1);
    std::fflush(stdout);
    stack_.push_back(std::move(screen));
    quest_nav(app_, nav_from, quest_scene_name(pushed_id));
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
    const std::string nav_from = quest_scene_name(popped->id());
    stack_.pop_back();
    // The screen beneath (the JS "caller") reactivates.
    if (!stack_.empty()) {
        stack_.back()->set_state(kStateActive);
        quest_nav(app_, nav_from, quest_scene_name(stack_.back()->id()));
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
