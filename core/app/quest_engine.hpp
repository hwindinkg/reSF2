#pragma once

// Quest engine core (app scope) — data-driven tutorial quest machine.
//
// Spec: FLOW_STATIC.md §1 (engine mapping Fe.Ij/ha/Bj/Yb, event/condition/
// action tables) over `reference/extracted/xml/res/quest_extensions/
// tutorial_quests.xml` (root `quests.xml` includes it while
// `_$StoryTutorialStep != END`).
//
// What this IS: QuestDef parse (Quest/Events/Conditions/Actions incl.
// nested If/Dialog/Button children), session latch for Unresumable="1",
// condition eval (Equal/Not/GreaterEqual + And/Or nesting, `_$` journal
// vars + the `?`-queries the shell can answer), and runners for the
// APP-SCOPED actions only — save/UI writes (so/to/qo/oo/SetVariable,
// queue clears) plus RECORDS for everything else (scene/fight/click/
// dialog/minigame requests are logged, never executed: no auto-navigation,
// no auto-fights, no auto-clicks — headless-safe by construction).
// Async semantics (Wait Frames, Dialog modal gating, Activate delays,
// `Dh[]` ordering, `be.Mbb` gates) collapse to synchronous runs — noted.
//
// What this is NOT (missing hooks, all noted in the report, none touched):
// fight-affecting actions need scene hooks — Sn (battle start `v.Am`),
// Tn (`ca.Ka().kD`), Do/Eo/Bo/Co/Fo minigames (fighter/AI/perk hooks),
// Nn auto-click (deliberately record-only).
//
// Journal (Bj analog): story_step comes from the LIVE save at fire time;
// scene_to/from from ScreenManager push/pop; fight triple from FightEnd.

#include <map>
#include <string>
#include <vector>

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

// Condition node (Equal/GreaterEqual leaf or And/Or operator).
struct QuestCond {
    std::string kind = "Equal";  // "Equal" | "GreaterEqual" | "And" | "Or"
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
    // The Right button's deferred nested actions (JS `vh.actions`, run on
    // press via `He.dhb(1)` L1061). Empty = no button (Notification OK with
    // no actions, `hab()` false).
    std::vector<QuestAction> button_actions;
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
        if (!dialogs_.empty()) dialogs_.erase(dialogs_.begin());
    }

    // JS `He.dhb(1)` L1061 (the Right button): pops the head dialog and runs
    // its deferred nested actions (`SetStoryTutorialStep`/`Fight`/...).
    // Returns the recorded fight-request names (`Sn`) for the caller to
    // launch (the engine never navigates). Save writes are applied.
    std::vector<std::string> press_dialog(App& app);

    // Drops every queued dialog (tutorial handoff / scene reset).
    void clear_dialogs() { dialogs_.clear(); }

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

private:
    bool ensure_loaded(App& app);
    void fire_inner(App& app, const std::string& event, const QuestJournal& journal,
                    std::vector<std::string>& fired, int depth);
    bool conditions_hold(const QuestCond& cond, const QuestJournal& journal,
                         const std::string& story_step, int level) const;
    std::string resolve_token(const std::string& token, const QuestJournal& journal,
                              const std::string& story_step, int level) const;
    void run_actions(App& app, const std::vector<QuestAction>& acts,
                     const QuestJournal& journal, QuestSideEffects& fx,
                     std::map<std::string, std::string>& locals,
                     const std::string& quest, int depth);
    void apply_effects(App& app, const QuestSideEffects& fx);
    std::string battle_zone(const std::string& battle) const;

    bool loaded_ = false;
    std::vector<QuestDef> quests_;
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
};

} // namespace sf2::app
