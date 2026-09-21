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

// `wa.V8a` (L478476): the DESTINATION screen's tab index for the ChangeTab
// journal. Shop(4) = the pushed payload's `Gj.T5` (the `vj.E0` tab index,
// default 1); Map(5) = 16 (`StoryMapStage`); Profile(7) = 10 (`Perks`);
// else 0 (`Default`).
int wa_v8a(ScreenId id, int shop_tab_index) {
    switch (id) {
        case kScreenShop: return shop_tab_index;
        case kScreenMap: return 16;
        case kScreenProfile: return 10;
        default: return 0;
    }
}

// Fires ChangeTab + SceneLoaded for a navigation edge (JS `wa.mp` L933 +
// `v.qwa` L621757 + `wa.ghb` L934). Never throws, never navigates.
void quest_nav(App& app, const std::string& from, ScreenId to_id) {
    const std::string to = quest_scene_name(to_id);
    if (to.empty()) return;
    try {
        QuestJournal j;
        j.scene_from = from;
        j.scene_to = to;
        // `wa.mp` L933: `var e=ha.F().ta; e.lLa=e.Xo; e.nLa=xn.iOa(a);
        // e=vj.E0(e.DI); let f=wa.V8a(a,b); ... v.qwa(e,f)`. `v.qwa`
        // (L621757) writes `XNa=uh.getName(from)`, `YNa=uh.getName(to)` — tab
        // NAMES normalized through the index tables, never the scene names.
        // `DI` is the CURRENT screen's tracked tab (`Bj.DI`, ctor L1005); ""
        // normalizes to "Default". `V8a`'s shop payload is the pending
        // `OpenShop` tab (the `Gj.T5` the push carried).
        j.tab_from = QuestEngine::tab_name_for_index(
            QuestEngine::tab_index_for_name(app.quest_engine().tab_owner()));
        int shop_idx = 1;  // `b!=null ? b.T5 : 1`
        if (to_id == kScreenShop && app.has_pending_shop()) {
            shop_idx = QuestEngine::tab_index_for_name(app.pending_shop_tab());
            if (shop_idx == 0) shop_idx = 1;
        }
        j.tab_to = QuestEngine::tab_name_for_index(wa_v8a(to_id, shop_idx));
        try {
            j.player_level = app.save().load().level;
        } catch (const std::exception&) {
        }
        app.quest_engine().fire(app, "ChangeTab", j);
        app.quest_engine().fire(app, "SceneLoaded", j);
        // The destination ctor's own `DI` write (JS): the Map sets
        // `StoryMapStage` (L1094741/L1096479); Shop/Profile leave it.
        if (to_id == kScreenMap) app.quest_engine().set_tab_owner("StoryMapStage");
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
    quest_nav(app_, nav_from, pushed_id);
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
        quest_nav(app_, nav_from, stack_.back()->id());
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
