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
        const std::vector<std::string> fired =
            app.quest_engine().fire(app, "ChangeTab", j);
        // `wa.mp` (L933): `if(d && v.qwa(e,f)) return !1;`. `v.qwa` (L621757)
        // is `return ha.F().Sf("QUEST_EVENT_CHANGE_TAB")`, and `Sf` (L522497)
        // is `return this.RA(a)?(this.qT(),!0):!1` — TRUE when at least one
        // quest MATCHED. `wa.mp` then ABORTS the navigation: it never runs
        // `this.Td.Tf=a` / `this.fLa()`, so `wa.ghb` (L934) never fires
        // `QUEST_EVENT_SCENE_LOADED` for this edge. The matched quest owns the
        // transition (e.g. `StoryTutorialBuyItem`'s `OpenShop` -> `go.Thb` ->
        // `mp(4,...)`, or `FixShopOpen`'s `ChangeScene Shop`), and on THAT
        // later edge `StoryTutorialRetryGoToMap`'s `step==MAP` condition is not
        // yet satisfiable (the buy lesson holds the chain). Without the abort
        // the port fired SceneLoaded on the same edge and `RetryGoToMap` re-
        // queued the same `tutorial_buy_knives` dialog (the double-dialog bug).
        if (!fired.empty()) {
            // The outer nav is aborted, but the matched quest's own
            // `ChangeScene`/`OpenShop` mounts the SAME target (the port already
            // pushed it, so `do_navigate` sees it current) and its `wa.ghb`
            // SceneLoaded clears the scene-scoped guidance. Mirror that reset
            // here so a nav highlight aimed at the target scene is consumed.
            app.quest_engine().set_current_scene(to);
            app.quest_engine().enter_scene_guidance(to);
            std::fprintf(stdout,
                         "[quest] ChangeTab matched -> wa.mp abort (no SceneLoaded) "
                         "scene=%s->%s\n",
                         from.c_str(), to.c_str());
            std::fflush(stdout);
            return;
        }
        // `wa.ghb` L934: `ha.F().ta.Xo = xn.iOa(this.Td.Tf)` immediately before
        // `Sf("QUEST_EVENT_SCENE_LOADED")`. Set AFTER the ChangeTab fire (which
        // still sees the OLD `Xo`, as `wa.mp` L933 captured `lLa` first) and
        // before SceneLoaded so `_$CurrentScene` (`Bj` L961) reads the new scene.
        app.quest_engine().set_current_scene(to);
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
    const std::string nav_from =
        stack_.empty() ? std::string() : quest_scene_name(stack_.back()->id());
    push_impl(std::move(screen), nav_from);
}

void ScreenManager::push(std::unique_ptr<Screen> screen, const std::string& nav_from) {
    if (screen == nullptr) {
        return;
    }
    push_impl(std::move(screen), nav_from);
}

void ScreenManager::push_impl(std::unique_ptr<Screen> screen, const std::string& nav_from) {
    // The JS transition (ae) deactivates the covered screen: when a new
    // screen is pushed, the previous top goes to the inactive "leaving"
    // state (Te(5)) and only the new top updates. The shell mirrors that
    // with an immediate switch.
    if (!stack_.empty()) {
        stack_.back()->set_state(kStateLeaving);
    }
    screen->set_state(kStateActive);
    const ScreenId pushed_id = screen->id();
    std::fprintf(stdout, "[screen] push %s (id=%d) — stack now %zu\n", screen->name().c_str(),
                 static_cast<int>(screen->id()), stack_.size() + 1);
    std::fflush(stdout);
    stack_.push_back(std::move(screen));
    // JS `mc.Taa` (L63240) mounts the `ad` Loader (scene 2) only when the
    // target scene's `Pea()` (L60160) is non-empty — its `Yv()` minus every id
    // already in the asset cache `G.data.v`: `a.Pea().length>0?(c=a.lBa()...,
    // new Xg(this,a),...):(...)`. The port preloads all assets at boot, so
    // `scene_data_cached_` stands in for `G.data.v`:
    //   - Dojo (3): `Rg.load` (L1967) preloads `Tf.Yv()`, so the FIRST mount is
    //     clean; `Tf.init` (L1015725) then frees asset 1355 (`G.Qr(1355)`), so
    //     every LATER mount has `Pea()={1355}` and loads.
    //   - Shop(4)/Map(5)/Profile(7): their `Yv()` ids are never `G.Qr`-freed,
    //     so they load only on the first mount.
    //   - Fight(6)/Results(10) mount NO loader: the Fight scene enters with its
    //     `ik` VS intro already playing (JS loads the scene BEFORE `ik`; the
    //     port loads synchronously, so arming here would cover the intro), and
    //     Results is a native shell panel (JS shows the `kk` panel as a child
    //     of the Fight screen, `v.kD` L622187 — not a `Taa` scene change).
    switch (pushed_id) {
        case kScreenDojo:
            if (scene_data_cached_.count(kScreenDojo) == 0) app_.begin_scene_loader();
            scene_data_cached_.erase(kScreenDojo);  // `G.Qr(1355)` on every init
            break;
        case kScreenShop:
        case kScreenMap:
        case kScreenProfile:
            if (scene_data_cached_.count(pushed_id) == 0) {
                scene_data_cached_.insert(pushed_id);
                app_.begin_scene_loader();
            }
            break;
        default:
            break;
    }
    // JS: the Results screen is NOT a scene change — `v.kD` (L622187) shows the
    // `kk` results panel as a CHILD of the fight screen `i` (`i.lca` L2008
    // `this.Ws=Qo(jk)`); no `Zd.load`/`wa.mp` runs. So pushing/poping Results
    // must NOT fire `ChangeTab`/`SceneLoaded`. `quest_scene_name` maps Results
    // to "Fight" (it has no JS scene id), which made the push fire a spurious
    // `SceneLoaded` (scene=Fight). `fire("SceneLoaded")` -> `drain_deferred_fight`
    // (quest_engine.cpp) then ran the FightEnd-deferred quests (FirstGuardBeaten
    // -> the `ActScreen` Act_1) DURING the results, starting the act overlay
    // before the map. JS `ha.add` (L522089) suppresses auto-run while scene==Fight
    // and pumps the queue on the NEXT real scene load (the Map), so the act
    // belongs to the Map, not the results handoff.
    if (pushed_id != kScreenResults) {
        quest_nav(app_, nav_from, pushed_id);
    }
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
    const ScreenId popped_id = popped->id();
    const std::string nav_from = quest_scene_name(popped_id);
    stack_.pop_back();
    // The screen beneath (the JS "caller") reactivates.
    if (!stack_.empty()) {
        stack_.back()->set_state(kStateActive);
        // Popping Results is not a JS scene change (see `push_impl`): skip the
        // spurious "Fight"->"Fight" ChangeTab/SceneLoaded. The real edge
        // (Fight->Map) still fires when the Fight itself is popped next.
        if (popped_id != kScreenResults) {
            quest_nav(app_, nav_from, stack_.back()->id());
        }
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
