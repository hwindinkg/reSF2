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
    std::string title;  // characterSensei / boss_lynx / ...
    std::string image;
    std::vector<std::string> lines;  // resolved Line texts (lang-applied)
    std::string quest;               // firing quest name
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
    std::vector<std::string> scene_requests;      // Gn (record only)
    std::vector<std::string> fight_requests;      // Sn (record only)
    std::vector<std::string> dialogs;             // He summaries (record only)
    std::vector<std::string> clicks;              // Nn targets (record only)
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
};

} // namespace sf2::app
