#pragma once

// Quest engine core (app scope) — data-driven quest machine.
//
// Spec: the JS quest loader/engine (`Fe.Ij`/`ha`/`Bj`/`Yb`, event/condition/
// action tables) over the real shipped tree. The ROOT is
// `reference/extracted/xml/res/quests.xml` (the same extracted-res source
// `stages.xml`/`list.xml` resolve from): its `<Quest>` children register
// inline, its `<Include File="a.xml|b.xml">` children load further files
// (each `|` alternative that exists, in order, after evaluating the
// Include's own `<Conditions>`), and a running quest's
// `<AttachQuestFile File="x.xml">` action loads a file at that point
// (JS `Bn.S` -> `p.F().L3`).
//
// What this IS: QuestDef parse (Quest/Events/Conditions/Actions incl.
// nested If/Dialog/Button children), session latch for Unresumable="1",
// condition eval (Equal/Greater/GreaterEqual/Less/LessEqual + Not and
// And/Or nesting, `_$` journal vars + the `?`-queries the shell can
// answer; an operand the shell cannot answer makes the condition
// UNKNOWN — the quest does not fire and the query is logged, never
// silently true/false), and runners for the APP-SCOPED actions only —
// save/UI writes (so/to/qo/oo/SetVariable, queue clears) plus RECORDS for
// everything else (scene/fight/click/dialog/minigame requests are logged,
// never executed: no auto-navigation, no auto-fights, no auto-clicks —
// headless-safe by construction).
// Async semantics (Wait Frames, Dialog modal gating, Activate delays,
// `Dh[]` ordering, `be.Mbb` gates) collapse to synchronous runs — noted.
//
// Journal (Bj analog): story_step comes from the LIVE save at fire time;
// scene_to/from from ScreenManager push/pop; fight triple from FightEnd.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "app/save_system.hpp"
#include "xml_doc.hpp"

namespace sf2::app {

class App;

// Journal for one event firing (JS `ha.ta`/`Bj` readable subset).
struct QuestJournal {
    std::string scene_to;     // JS scene name (Dojo/Map/Fight/Shop/Profile/…)
    std::string scene_from;   // JS scene name
    std::string fight;        // last fight name ("Punchbag|Bosses|1" or bare)
    std::string fight_result;  // "Win" / "Loss" / ""
    std::string fight_zone;   // resolved zone ("" when unknown)
    int player_level = 1;
    std::string action_id;  // Activate ActionID
};

// Condition node (leaf comparison or And/Or operator). Leaf kinds mirror
// the JS `yb.tD` tag table: Equal/Greater/GreaterEqual/Less/LessEqual.
struct QuestCond {
    std::string kind = "Equal";  // leaf kind, or "And" / "Or" operator
    bool invert = false;         // Not="1"
    std::string value1;
    std::string value2;
    std::vector<QuestCond> children;  // Operator branches
};

// Action node (tag + attrs; If/Dialog keep structured children).
struct QuestAction {
    std::string tag;
    std::map<std::string, std::string> attrs;
    QuestCond if_cond;                     // If/Conditions
    std::vector<QuestAction> if_then;      // If/Then
    std::vector<QuestAction> if_else;      // If/Else
    std::vector<QuestAction> children;     // Dialog lines/buttons/nested acts
};

// One quest definition (JS `be`).
struct QuestDef {
    std::string name;
    int priority = 0;
    bool unresumable = false;
    std::vector<std::string> events;  // ChangeTab/SceneLoaded/Activate/…
    QuestCond root;                   // AND of top-level Conditions
    std::vector<QuestAction> actions;
};

// JS `vh` (L1063): one `<Button>` slot of a `He` dialog. `He.Rib` (L1057-1058)
// files each nested `<Button Type="...">` into its own slot — Left→`Ng`,
// Middle→`Nh`, Right→`rh`, Close→`Hj` — each carrying its own caption, colour
// and deferred actions. The port folded every `<Button>` into the single
// Right slot (`button_actions`/`button_text`), stranding the 159 Left / 16
// Middle / 1 Close shipped slots.
struct EngineDialogButton {
    std::string text;                 // `vh.text` (`Button Text`)
    std::string color;                // `vh.color` (`Button Color`)
    std::vector<QuestAction> actions; // `vh.actions` (deferred to the press)
    bool hint = false;                // `vh.aA` (`Button Flashing`)
};

// Structured dialog record for the Sensei modal (He display lives in
// screens.cpp; the engine only queues).
struct EngineDialog {
    std::string type;   // Notification / Regular
    std::string title;  // characterSensei / boss_lynx / ... (lang key)
    std::string image;
    // Resolved Line texts. Each entry is a runtime lang key (`tutorial_move`,
    // `tutorial_training_fight`, ...) — NOT pre-resolved, so the display
    // layer resolves it after the lang table is loaded (JS `ba.cg`/`ba.Fz`
    // resolve at He.S time; the port queues before boot renders).
    std::vector<std::string> lines;
    // Per-row advance captions (`ButtonText`), parallel to `lines`. JS
    // `He.jkb` (L1042) stores the caption on the ROW: a `Regular` dialog with
    // several `<Line>` rows pages through them, and each page's caption
    // labels the advance plate. In the shipped tutorial the two-row Lynx
    // dialog is `dlgStoryBtnMore` (row 1) / `dlgStoryBtnFight` (row 2,
    // tutorial_quests.xml L159-160).
    std::vector<std::string> line_buttons;
    // Current page (JS `He` line cursor). 0 for a single-row dialog.
    std::size_t page = 0;
    // The dialog button's caption (the `Right` `<Button Text>` when authored,
    // e.g. sensei_arc.xml L59, else the LAST row's `ButtonText`: the page
    // that carries the nested actions, `hab()` L1060).
    std::string button_text;
    // The Right button's `<Button Color>` (JS `vh.color` via `He.Rib` L1057).
    // The frame is `nz.hi(color)` (`He.lea` L1063): Red→Dark, Green→Green,
    // White/Beige→White, Gold→Gold.
    std::string button_color;
    // The Right button's deferred nested actions (JS `vh.actions`, run on
    // press via `He.dhb(1)` L1061). Empty = no button (Notification OK with
    // no actions, `hab()` false).
    std::vector<QuestAction> button_actions;
    // `He.Rib` L1057-1058: an authored `<Button Type="Right">` (or a bare
    // `<Button>` with no `Type`) creates the `rh` slot even with NO nested
    // actions. `Xc.Xhb` L1047 always passes that slot to `Od` (`e`/`k`); `hab()`
    // L1060 gates ONLY the Notification OK plate (L1050). tutorial_quests.xml
    // L112 (`<Button Type="Right" Color="White" />`) is exactly this: a
    // `Regular` whose plate the port dropped because `button_actions` was empty.
    bool has_right_button = false;
    // The other `He` slots (`He.Rib` L1057-1058): Left→`Ng`, Middle→`Nh`,
    // Close→`Hj`. `dhb` L1061 dispatches them by index (0/2/100); the Right
    // slot above is index 1.
    EngineDialogButton left_, middle_, close_;
    // `He.L` L1044 `MinContentHeight` -> `Od.cv` L1944: the floor `od.layout`
    // L1898 applies to the measured content height (`Od.lj` L1950
    // `Math.max(kb.ew(), this.cv)`).
    float min_content_height = 0.0f;
    // --- D10 dialog attrs (`He` L1043-1045 parse, `He.S` L1051 apply) --------
    // `Mirrored` -> `n4a` L1043, applied L1047 (`this.n4a&&(r+="|Flip")`, so the
    //   parse appends "|Flip" to `image`; `v.RIa` L1222 detects it).
    // `ImageScale` -> `iy` L1044, applied L1947 (`Zg.la(1.8*this.iy)`).
    // `ContentOffsetX` -> `TM` L1044, applied to the content x (`Jva` L1951).
    // `ImageOffsetX/Y` -> `OB`/`YV` L1044, applied L1051 `VLa`/`WLa`.
    // `TextOffset` -> `ov` L1044 (`Xy`), applied L1051 `mMa` + `eba` L1950.
    // `TextPosXByImage` -> `LH` L1045, applied L1051 `nMa` (`Jva` L1951).
    // `BlockRaycast` -> `$Ta` L1044, the `Ib.Qhb(...,h)` L1050 dim gate.
    // `DisableNotificationsButtons` -> `qUa` L1044 -> `Ib.RP` (L1045/L1050),
    //   gating the notification OK plate (`Ib.Sr` L1910 `Ib.RP?b=!1:...`).
    bool mirrored = false;
    float image_scale = 1.0f;
    float content_offset_x = 0.0f;
    float image_offset_x = 0.0f;
    float image_offset_y = 0.0f;
    float text_offset_x = 0.0f;
    float text_offset_y = 0.0f;
    bool text_pos_x_by_image = true;
    bool block_raycast = true;
    bool disable_notifications_buttons = false;
    // D8 `He.SK` L1043 (`u.H(ReadTime, ge.ZGa)`): the `Ib` bar's auto-dismiss
    // budget in seconds (`Ib.aa` L1905 `this.SK-=a; this.SK<=0&&(this.qma=!0)`
    // then `OZa` L1908 -> `y4(!1)`). Zero disables the countdown.
    float read_time = 0.0f;
    // `He.ah` L1045 `Item` -> L1046 `x=ba.Pc(a,this.ah); x=p.items.$b(x)`, whose
    // `Ev` composite the portrait prefers (`Od.$A` L1945 `new or(this.sV)`).
    std::string item;
    // `He.L` L1044 `Loot` -> the `ShowLoot` type's item list (`Xc.Uhb` L929:
    // `ba.Pc(a,this.IN).split("|")`, offers.xml is the only shipped one).
    std::vector<std::string> loot;
    std::string quest;               // firing quest name
    QuestJournal journal;            // `Qt` (He.S stores the firing journal)
};
// One `<Battles>` write a quest action asks for. JS mapping:
//   ShowBattle            -> `Aj(true)`  L1108 -> `Iaa(hb,true,true,..)` +
//                            `hl.yla` — ensure the record, set flags;
//   HideBattle            -> `Aj(false)` L1108 -> `Iaa` `c=false` -> `Eja`
//                            L261 — drop the record;
//   SetBattleVisibility   -> `no` L1096 -> `Iaa(hb,false,true,false,hidden)`
//                            — ensure + set Hidden (`IsVisible<=0` => hidden);
//   ToggleBattle          -> `Ho` L1106 -> `hl.gx(!li)` — flip Hidden only
//                            when the record already exists.
struct QuestBattleWrite {
    std::string zone;            // resolved `hb.Me`
    std::string name;            // `hb.Re`
    bool remove = false;         // HideBattle -> `Eja`
    bool toggle_hidden = false;  // ToggleBattle -> flip `hl.gx`
    bool locked = false;         // `Kra` (Iaa `d`)
    bool hidden = false;         // Iaa `e`
    int replay_count = 0;        // `yla` (Iaa `f`)
};

// `Gn` `ChangeScene` (L1032 `S` -> `qIa` -> `wa.F().mp`): the destination the
// engine navigates the ScreenManager to. `reopen` mirrors the `ReopenScene`
// attr (`this.bta`, L1031: navigate even when the target IS the current
// scene). The engine skips the push when the target already is on top.
struct QuestSceneRequest {
    std::string destination;  // resolved `xn.jOa` name (Dojo/Map/Shop/Profile)
    bool reopen = false;      // `ReopenScene="1"`
};

// `go` `OpenShop` (L1092): `mp(4, new Gj(tab, item))` then `Oa.uLa(tab,item)`
// (`f5(tab)` + `Za.SA(item)`, L1181866) — open the Shop, set the `vj.E0` tab
// and select the item by name.
struct QuestShopOpen {
    std::string tab;   // `vj.E0` category name (Weapon/Armor/Helm/…)
    std::string item;  // items.xml Name, or "" (tab only)
};

// Side effects of one run: save writes (applied) + records (logged only).
struct QuestSideEffects {
    bool has_story_step = false;
    std::string story_step;
    bool has_map_focus = false;
    std::string map_focus;
    bool has_current_zone = false;
    std::string current_zone;
    std::map<std::string, std::string> set_vars;  // Global SetVariable
    std::vector<QuestBattleWrite> battle_writes;  // Show/Hide/SetVisibility/Toggle
    std::vector<std::string> scene_requests;      // Gn (record only)
    std::vector<std::string> fight_requests;      // Sn (record only)
    std::vector<std::string> dialogs;             // He summaries (record only)
    std::vector<std::string> clicks;              // Nn targets (record only)
    // `Nn` (`ClickButton`) with `UseFlashing="1"`: flash the target plate
    // WITHOUT firing its callback (`IgnoreCallback="1"`). The tutorial asks
    // for `InfoBattle.FightButton` (tutorial_quests.xml L156) — the map
    // FIGHT button is highlighted, never auto-pressed.
    std::vector<std::string> flash_targets;
    // `MenuBtnFlashing BtnName` (L361): highlight a `za` nav button by scene.
    // The desktop guidance for `_NextScene` (FLOW_STATIC L140-142: the web/
    // else branch shows the notification + the flash and does NOT navigate).
    std::vector<std::string> menu_flashes;
    // `ClickHint Target` (L338, the Switch/Steam branch): arrow hint. Recorded
    // (the desktop shell drives navigation through the nav flash).
    std::vector<std::string> click_hints;
    std::vector<std::string> clears;              // Mn queue names
    std::vector<std::string> minigames;           // Do/Eo/Ao/Bo/Co/Fo (record)
    // `Bn.S` (L1025): `AttachQuestFile File` loads a quest file at this
    // point in the run (deferred to the end of the fire pass — the loaded
    // quests register for FUTURE events, exactly like the JS `RA` loop
    // which captures `a.length` before iterating).
    std::vector<std::string> attach_files;
    // `Ge.S` (L1023-1024): `Activate ActionID` (re-fires the Activate
    // event with `_$ActionID` bound; the JS `Ge.MZ` handshake).
    std::vector<std::string> activate_requests;
    // `SceneMenuScroll Action` (the Switch/Steam branch): recorded.
    std::vector<std::string> scene_menu_scroll;
    // --- live action execution (interactive; headless keeps records) -------
    // The JS action classes that ACT (not merely record), collected during the
    // run and performed by `tick` after the firing pass (so a `mp` push never
    // re-enters the ScreenManager's update iteration). See each cite below.
    //
    // `Gn` L1032: `wa.F().mp(xn.jOa(dest), …)` — push the scene.
    std::vector<QuestSceneRequest> navigate;
    // `go` L1092: `wa.F().mp(4, new Gj(tab,item))` + `Oa.uLa(tab,item)`.
    std::vector<QuestShopOpen> shop_opens;
    // `Nn` L1114 WITHOUT `IgnoreCallback`: the target's own click listeners
    // stay live (`xk.pa` is NOT cleared), so the player's press dispatches the
    // target's callback. The engine only ARMS + logs it (the JS never
    // auto-presses: `Nn.S` hooks `xk.pa.addListener(Qg)` and waits).
    std::vector<std::string> click_arm;
    // `eo` L1117 (`Nn`… `sxa()`): `MenuBtnFlashing` collapses the `za` scroll
    // (`za.instance.sxa()` -> `scroll.collapse(0)`, L2001) before it flashes.
    bool collapse_nav = false;
    std::vector<std::string> unknown;             // unhandled tags
};

class QuestEngine {
public:
    QuestEngine() = default;

    // Fires an event (ChangeTab/SceneLoaded/Activate/FightEnd/SessionStart):
    // loads the chain on first use, evaluates quests in file order, runs
    // matches, applies save writes dirty-checked, recurses Activate (cap).
    // Returns fired quest names. Never throws, never blocks, never
    // navigates. `journal` carries the event context.
    std::vector<std::string> fire(App& app, const std::string& event,
                                  const QuestJournal& journal);

    // Records the last fight triple (Bj Nb/Qv analog; set on FightEnd).
    // ChangeTab/SceneLoaded journals leave fight empty and inherit this.
    void note_fight(const std::string& name, const std::string& result);

    // Sensei-modal queue (He records): display + advance live in screens.
    bool has_dialog() const { return !dialogs_.empty(); }
    const EngineDialog& dialog() const { return dialogs_.front(); }
    void pop_dialog() {
        // `Wb`'s top is the first NON-Notification (`modal_index`) — the same
        // entry `press_dialog` erases. Erasing `begin()` dropped a leading bar
        // Notification instead: the `tutorial_shop` bar entry queued by
        // `StoryTutorialOpenScene` (tutorial_quests.xml L350) was still in the
        // queue when the follow-up `Regular` (L327/L110) arrived, so dismissing
        // the `Regular` left IT queued and the modal re-blocked the Shop forever.
        const std::size_t i = modal_index();
        if (i < dialogs_.size()) {
            dialogs_.erase(dialogs_.begin() + static_cast<std::ptrdiff_t>(i));
        }
    }
    // Headless drain pops the entry it LOGGED (`dialog()` = the FRONT), not
    // `modal_index()`. `pop_dialog()` is modal-aware and is a NO-OP when the
    // queue holds only bar Notifications, so a Notification at the front made
    // the headless `while (has_dialog())` loop spin forever (the `Regular`
    // behind it never drained) — a soft lock. This erase always progresses.
    void pop_head_dialog() {
        if (!dialogs_.empty()) dialogs_.erase(dialogs_.begin());
    }

    // --- `Ib` bar vs `Wb` modal (JS `He.S` L1050 / `Wb.Xob` L927) ----------
    // JS `He.S` L1050 routes a `Notification` to `Ib.F().Qhb(r,z,c,g,k,SK,x,
    // $Ta)` — the BAR — and NOT to `Wb.openDialog`; `Wb.Xob` (L927) is the
    // only `this.II.push(this.If)` site and it is reached from `Xc.Xhb` (the
    // `Regular` path, L931). So a Notification never enters the `Wb` queue and
    // the `Regular` is `Wb`'s top dialog the moment `He.S` runs (its chained
    // condition `this.type=="Notification"&&x || … || (Ib.RP=!1, this.sa())`
    // advances the chain when `x=ba.Zv(a,XVa="0")` is false, L1050). The bar is
    // ONE instance (`Ib`), so the LAST posted Notification is what it shows
    // (`Ib.Qhb` L1907 overwrites the visible bar).
    static bool is_bar_notification(const EngineDialog& d) {
        return d.type == "Notification";
    }
    // `Wb`'s top = the first queued dialog that is NOT a bar Notification.
    std::size_t modal_index() const {
        for (std::size_t i = 0; i < dialogs_.size(); ++i) {
            if (!is_bar_notification(dialogs_[i])) return i;
        }
        return dialogs_.size();
    }
    const EngineDialog* modal_top() const {
        const std::size_t i = modal_index();
        return i < dialogs_.size() ? &dialogs_[i] : nullptr;
    }
    // The `Ib` bar's content = the LAST queued Notification (`Ib.Qhb` L1907
    // overwrites the single bar instance, so `_NotificationTextPunchBag` wins
    // over `_NotificationTextMove` in `StoryTutorialWelcome`).
    const EngineDialog* notification_top() const {
        for (std::size_t i = dialogs_.size(); i-- > 0;) {
            if (is_bar_notification(dialogs_[i])) return &dialogs_[i];
        }
        return nullptr;
    }
    bool has_modal() const { return modal_index() < dialogs_.size(); }
    bool has_notification() const { return notification_top() != nullptr; }
    std::size_t dialog_count() const { return dialogs_.size(); }
    // `Ib.close` (the ReadTime budget spent, `Ib.aa` L1905 -> `y4(!1)`)
    // drops the BAR entry only — the `Wb` queue is untouched.
    void pop_notifications() {
        std::size_t w = 0;
        for (std::size_t i = 0; i < dialogs_.size(); ++i) {
            if (!is_bar_notification(dialogs_[i])) {
                if (w != i) dialogs_[w] = std::move(dialogs_[i]);
                ++w;
            }
        }
        dialogs_.resize(w);
    }

    // JS `He.dhb(a)` L1061: pops the head dialog and runs the deferred nested
    // actions of the slot selected by `button_index` — 0=Left(`Ng`),
    // 1=Right(`rh`, the historical default), 2=Middle(`Nh`), 100=Close(`Hj`).
    // Returns the recorded fight-request names (`Sn`) for the caller to
    // launch (the engine never navigates). Save writes are applied.
    std::vector<std::string> press_dialog(App& app, int button_index = 1);

    // JS `hab()` L1060: any slot carries actions (`Ng`/`rh`/`Nh`/`Hj`). A
    // dialog with no such slot advances on tap instead of firing a plate.
    // Reads the `Wb` top (the first non-Notification), not a bar Notification.
    bool dialog_has_button() const {
        const EngineDialog* d = modal_top();
        return d != nullptr &&
               (d->has_right_button || !d->button_actions.empty() ||
                !d->left_.actions.empty() || !d->middle_.actions.empty() ||
                !d->close_.actions.empty());
    }

    // Drops every queued dialog (tutorial handoff / scene reset).
    void clear_dialogs() { dialogs_.clear(); }

    // --- `Do`/`Eo`: the StoryTutorial lesson GATE (JS L1242/L1243) ----------
    // `StoryTutorialWelcome` (reference/extracted/xml/res/quest_extensions/
    // tutorial_quests.xml L26-42) is ONE serialized action list:
    //   Dialog(Notification Move) -> <StoryTutorialMove/> ->
    //   Dialog(Notification PunchBag) -> <StoryTutorialPunchbag/> ->
    //   Dialog(Regular characterSensei = the В БОЙ training-fight modal).
    // The JS `Do` (move) / `Eo` (punchbag) action classes SUSPEND the chain
    // there: `Do.S` arms `this.Ni = new Re(w(this,this.Cm), v.su.a_)` — a
    // timer of `v.su.a_` = `TutorialStepTimeout` (`internal_settings.xml`
    // L115 `Value="15"`) — and `Cm` calls `this.sa()`, which resumes the tail
    // (`Yb` L954 serializes the list). So the JS only ever has ONE tutorial
    // beat queued at a time: the Move bar, then the PunchBag bar, then the
    // Regular modal. The oracle tutorial captures are exactly those beats
    // (`oracle_matrix/MANIFEST.md`: tut_fight_stance = the move lesson,
    // tut_fight_phase2 = the punchbag lesson, tut_block/dojo_sensei = the
    // modal).
    // The port has no dojo move / punchbag lesson hooks (the actions were
    // "record only"), so it queued the whole list at frame 0 and `Wb`'s top
    // became the Regular from f=0 — which hides the bar beats. This gate
    // restores the JS serialization using the JS timeout on the APP clock
    // (the top screen's fixed 60 Hz `time()`, like the `ReadTime` bar budget),
    // never the wall clock.
    static constexpr float kTutorialStepTimeoutSec = 15.0f;
    // Beat the suspended chain is parked at: 1 = `StoryTutorialMove`,
    // 2 = `StoryTutorialPunchbag`, 0 = not gated. The fidelity driver waits on
    // it so each tutorial capture lands on the oracle's own beat.
    int tutorial_gate_beat() const { return tutorial_gate_.active ? tutorial_gate_.beat : 0; }
    // Advances the gate clock by `dt` (app-time seconds); when it elapses the
    // stashed tail runs (which may immediately hit the second lesson and
    // re-park). Returns true when the tail resumed this call.
    bool tutorial_gate_tick(App& app, float dt);

    // Test hook (the `--dialog-verify` harness): queue a dialog record built
    // in-process, so the display/dispatch contracts (Left-vs-Right plate,
    // colour→frame, page caption precedence) can be asserted without firing a
    // quest. Goes through the same queue + cap the parse path uses.
    void push_dialog_for_test(EngineDialog d) {
        if (dialogs_.size() >= 8) dialogs_.erase(dialogs_.begin());
        dialogs_.push_back(std::move(d));
    }

    // One fixed step (called by App::update_fixed AFTER the screen update):
    // resumes deferred `Wait` runs (`Ro` L1119) and performs the queued
    // scene/shop navigation (`Gn`/`go`). A no-op while headless (the driver
    // paths keep the record-only behaviour). Never throws.
    void tick(App& app);

    // `Nn` (L1114) non-ignored target currently armed for the player's press.
    // The screen's own hit-test consults this to log the dispatch (the JS
    // `Qg` listener completes the quest step on the click).
    bool click_armed(const std::string& target) const {
        for (const std::string& t : armed_clicks_) {
            if (t == target) return true;
        }
        return false;
    }
    // Consumes the armed target after the player's press dispatched it.
    void clear_click_armed() { armed_clicks_.clear(); }

    // Test hooks for the interactive verification: how many `ChangeScene` /
    // `OpenShop` actions actually EXECUTED (resolved + performed) rather than
    // being recorded. Monotonic; headless runs leave them at 0.
    std::size_t scene_actions() const { return scene_actions_; }
    std::size_t shop_actions() const { return shop_actions_; }

    // --- live UI-guidance signals (draw-only; no navigation) --------------
    // `Nn` `ClickButton UseFlashing="1"` target — the shell pulses the named
    // plate (the map's `InfoBattle.FightButton`). Cleared on the next
    // SceneLoaded (it belongs to the screen it was requested on).
    const std::string& flash_target() const { return flash_target_; }
    // `MenuBtnFlashing BtnName` — the `za` nav button (by scene name) to
    // highlight until the player navigates there.
    const std::string& nav_flash() const { return nav_flash_; }
    // Last `SetMapFocus Battle=` value applied (`qo` L1086 = `p.o.m5(battle)`
    // + the `Ya` focus refresh). The Map re-targets `Rr` on change, so a
    // focus landing after the map's construction still takes effect.
    const std::string& last_map_focus() const { return last_map_focus_; }

    // --- `He` pager (L1042-1062) ------------------------------------------
    // A `Regular` dialog with several `<Line>` rows shows one row per page;
    // the page's `ButtonText` labels the advance plate and only the LAST
    // page's plate fires the nested actions (`hab()` L1060 / `dhb(1)` L1061).
    std::string dialog_button_text() const;
    bool dialog_has_next_page() const;
    void advance_dialog_page();

    // For logs/tests.
    std::size_t quest_count() const { return quests_.size(); }
    bool loaded() const { return loaded_; }
    // Shipped files the loader actually read (root + every resolvable
    // `<Include>` alternative + every executed `<AttachQuestFile>`).
    std::size_t file_count() const { return loaded_files_.size(); }
    const std::vector<std::string>& loaded_files() const { return loaded_files_; }
    // Queries the shell could not answer (logged; a condition using one is
    // UNKNOWN and never fires the quest).
    std::size_t unanswerable_count() const { return logged_queries_.size(); }
    const std::set<std::string>& unanswerable_queries() const { return logged_queries_; }

private:
    // One condition-evaluation context (the JS `Bj` journal `ta` plus the
    // live save snapshot the `?`-queries read).
    struct EvalCtx {
        QuestJournal journal;
        std::string story_step;
        int level = 1;
        mutable bool save_loaded = false;
        mutable WarriorSave save;
        const WarriorSave& live(App& app) const;
    };

    bool ensure_loaded(App& app);
    // JS `ha.GEa` (L521470): true while a quest instance with this name is
    // still in the ACTIVE queue (`Dh`) — a queued dialog, a deferred `Wait`
    // run, or the parked StoryTutorial gate. `RP.a.RXa` (`AllowDoubles`)
    // bypasses it. This is the ONLY re-entry gate: `Unresumable` (`be.cyb`,
    // L518544) is read solely by the resume path (`p.o.lpb`/`ResumeQuests`
    // `REa()`), never by the fire gate — so a quest whose event+conditions
    // recur re-fires once its run completes. That is what re-opens the Lynx
    // `StoryTutorialBossFight` dialog on the Map after a LOSS
    // (`tutorial_quests.xml` L131-152: `step==MAP && SceneTo==Map`).
    bool quest_active(const std::string& name) const;
    // JS `L3(a,b)` (L184): read one file, walk the root's children —
    // `Quest` -> register, `Include` -> `Sjb`.
    void load_quest_file(App& app, const std::string& rel);
    // JS `Sjb(a,b)` (L184): evaluate the Include's `<Conditions>` against
    // the current journal; if they hold, load every `File` alternative.
    void load_include(App& app, const pugi::xml_node& include_node);
    void parse_quest_node(App& app, const pugi::xml_node& quest_node,
                          const std::string& file);
    void fire_inner(App& app, const std::string& event, const QuestJournal& journal,
                    std::vector<std::string>& fired, int depth);
    // JS `yb.compare` (L959): 3-valued so a condition using a query the
    // shell cannot answer is UNKNOWN (the quest does not fire) instead of
    // silently true/false.
    enum class Tri { False, True, Unknown };
    Tri eval_cond(App& app, const QuestCond& cond, const EvalCtx& ctx);
    bool conditions_hold(App& app, const QuestCond& cond, const EvalCtx& ctx);
    // Resolves one Value1/Value2 expression. Returns false when the shell
    // cannot answer it (a `?`-query it does not model) — the caller then
    // treats the comparison as UNKNOWN.
    bool resolve_token(App& app, const std::string& token, const EvalCtx& ctx,
                       std::string& out);
    // `?Method[arg].Field` — the subset of the JS query engine the shipped
    // conditions read. Returns false (UNKNOWN, logged) for the rest.
    bool resolve_query(App& app, const std::string& token, const EvalCtx& ctx,
                       std::string& out);
    void note_unanswerable(const std::string& token);
    // Remainder of one action list when a `Wait` suspends it: `Yb` (L954)
    // serializes the list and `Ro` (L1119) completes N frames later, so the
    // actions AFTER the Wait run only once the delay elapses. `rest` is the
    // suspended tail (`inner remainder ++ outer remainder`).
    struct ActionRest {
        bool suspended = false;
        int frames = 0;
        std::vector<QuestAction> rest;
    };
    // One deferred action run (a suspended tail + its journal/locals).
    struct PendingRun {
        std::vector<QuestAction> actions;
        QuestJournal journal;
        std::map<std::string, std::string> locals;
        std::string quest;
        int frames = 0;
    };
    // The StoryTutorial lesson gate (see `tutorial_gate_beat`): the tail of the
    // suspended run plus its journal/locals and the remaining app-time.
    struct TutorialGate {
        bool active = false;
        int beat = 0;          // 1 = move lesson, 2 = punchbag lesson
        float remaining = 0.0f;  // app-time seconds until `Cm` (`TutorialStepTimeout`)
        std::vector<QuestAction> rest;
        QuestJournal journal;
        std::map<std::string, std::string> locals;
        std::string quest;
    };
    TutorialGate tutorial_gate_;
    // Runs the stashed tail (shared by the resume path); returns true if the
    // tail completed (false when it re-parked on the next lesson).
    bool resume_tutorial_gate(App& app);

    ActionRest run_actions(App& app, const std::vector<QuestAction>& acts,
                           const QuestJournal& journal, QuestSideEffects& fx,
                           std::map<std::string, std::string>& locals,
                           const std::string& quest, int depth);
    // Collects the executable side effects of one run (interactive only).
    void enqueue_effects(App& app, const QuestSideEffects& fx,
                         const QuestJournal& journal,
                         const std::map<std::string, std::string>& locals,
                         const std::string& quest);
    // Resumes a deferred run (its tail) and re-defers when another Wait hits.
    void resume_run(App& app, PendingRun& run);
    // `Gn.qIa` (L1032): navigate to a resolved scene name.
    void do_navigate(App& app, const QuestSceneRequest& req);
    // `go.Thb` (L1092): open/point the Shop at a tab + item.
    void do_open_shop(App& app, const QuestShopOpen& open);
    void apply_effects(App& app, const QuestSideEffects& fx);
    std::string battle_zone(const std::string& battle) const;
    bool loaded_ = false;
    std::vector<QuestDef> quests_;
    // Evaluation context of the load pass (the JS `ha.ta` journal an
    // `<Include>`'s conditions read). The root pass uses the boot journal.
    EvalCtx load_ctx_;
    std::vector<std::string> loaded_files_;  // shipped files the loader read
    std::vector<std::string> fired_;  // Unresumable session latch
    std::map<std::string, std::string> battle_zone_;  // battle -> zone index
    std::string last_fight_;
    std::string last_result_;
    std::vector<EngineDialog> dialogs_;  // Sensei-modal queue (cap below)
    // Live UI guidance (see flash_target/nav_flash/last_map_focus above).
    std::string flash_target_;
    std::string nav_flash_;
    std::string last_map_focus_;
    // Harness gate (JS fresh profile): an empty `_$StoryTutorialStep` reads
    // as `NotStarted` only while the fresh-tutorial path is armed (the
    // fidelity tour), so the seeded post-tutorial saves stay chain-silent.
    bool fresh_tutorial_ = false;
    // `?`-queries the shell could not answer (JS has a full query engine;
    // the port answers the subset it models). Logged once each; a condition
    // whose operand is unanswerable is UNKNOWN (never fires).
    std::set<std::string> logged_queries_;
    // Deferred `Wait` runs + the queued live actions (see `tick`).
    std::vector<PendingRun> pending_;
    std::vector<QuestSceneRequest> nav_queue_;
    std::vector<QuestShopOpen> shop_queue_;
    bool collapse_nav_pending_ = false;
    std::vector<std::string> armed_clicks_;  // `Nn` non-ignored targets
    std::size_t scene_actions_ = 0;          // executed `ChangeScene` count
    std::size_t shop_actions_ = 0;           // executed `OpenShop` count
};

// The `<Button Type>` slot census of the shipped quest tree (`quests.xml` plus
// every `quest_extensions/**` file — the tree `ensure_loaded` walks). Each
// `<Button>` is one slot: `He.Rib` L1057-1058. Used by the `--dialog-verify`
// parse assertion; expected 680 Right / 159 Left / 16 Middle / 1 Close.
struct QuestButtonCensus {
    std::size_t right = 0;
    std::size_t left = 0;
    std::size_t middle = 0;
    std::size_t close = 0;
    std::size_t dialogs = 0;  // `<Dialog>` elements seen
    std::size_t files = 0;    // quest XML files read
    std::size_t typed() const { return right + left + middle + close; }

    // Raw TEXT census (regex-equivalent scan of the file bytes, INCLUDING
    // commented-out XML). The parser excludes comments, so this is always >=
    // the element counts above. zone_4/zone_5/zone_6/zone_7 each ship three
    // commented-out quests, so the raw inventory is +12 dialogs / +12 Right /
    // +6 Left over the parsed slots.
    std::size_t text_right = 0;
    std::size_t text_left = 0;
    std::size_t text_middle = 0;
    std::size_t text_close = 0;
    std::size_t text_dialogs = 0;
    std::size_t text_typed() const {
        return text_right + text_left + text_middle + text_close;
    }

    // The `<Dialog Type=...>` distribution over the shipped tree — one entry
    // per `He.S` L1045-1051 routing case (every Type the parser meets, missing
    // `Type` counted as "Regular", the JS default at `He` L1043).
    std::map<std::string, std::size_t> types;
};

// Reads and counts the shipped quest tree. Never throws; a missing tree
// yields an all-zero census (the caller reports FAIL).
QuestButtonCensus census_quest_tree();

} // namespace sf2::app
