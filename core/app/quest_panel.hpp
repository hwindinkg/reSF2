#pragma once

// Tutorial quest UI (visible shell) — the Sensei hint chain on the Dojo
// screen + the quest toast on the Results screen, driven by the 13-quest
// tutorial machine (FLOW_STATIC.md §1, `quest_extensions/tutorial_quests.xml`
// via `Z.Jna = "quests.xml"` while `_$StoryTutorialStep != END`).
//
// Chain (quest id, trigger step, beat):
//   1  StoryTutorialWelcome      NotStarted  ChangeScene Dojo + FIGHT
//  2  StoryTutorialReturnToFight FIGHT       funnel back to the dummy fight
//   3  StoryTutorialShop          FIGHT+win   STEP_BUY_ITEM, tutorial_shop → Shop
//   4  StoryTutorialBuyItem       STEP_BUY_ITEM  knives → MAP, tutorial_map
//   5  StoryTutorialRetryGoToMap  MAP         funnel to Map
//   6  StoryTutorialBossFight     MAP         MapFocus ZONE_1|BOSS_LYNX|1,
//                                            tutorial_boss_hello → boss fight
//   7  StoryTutorialLearnPerk     LEARN_PERK  (Level>=2) tutorial_dojo_new_move
//   8  StoryTutorialGoToDojo      SHOW_DOUBLE_SWEEP → Dojo
//   9  StoryTutorialGoToDojoFromLoader (same, from Loader)
//  10  StoryTutorialDoubleSweep   SHOW_DOUBLE_SWEEP@Dojo → tutorial_profile_moves
//  11  StoryTutorialGoToProfile   SHOW_BLOCK → Profile
//  12  StoryTutorialShowBlock     SHOW_BLOCK@Profile, tutorial_block → END,
//                                            tutorial_return_map → Map
//  13  StoryTutorialOpenScene     delayed NextScene opens (SenseiDialogText)
//
// Display-only derivation. THE swap point is `quest_state_for()`: the save
// now carries `story_step()` (WarriorSave L105, `_$StoryTutorialStep`),
// `map_focus` (ys), `battles` (iF) and `variables` (rv) — all flow into
// QuestState, so every beat serves live. Fo-beat resolved (Stream 2): `Fo`
// (L1126-1127) sets no vars — SHOW_BLOCK unconditionally shows the beat-8
// block-lesson hint on `story_step` alone; no variables expectation remains
// (`variables` still flows for traceability).
//
// line1 comes from the runtime lang table (`lang_table.hpp`) with the
// verified EN embedded as fallback (titles in en.af2d6604.xml); line2 is
// shell-authored chrome (EN only). No save writes, ever.

#include <map>
#include <string>
#include <vector>

#include "app/lang_table.hpp"

namespace sf2::app {

// One Sensei hint step (quest banner / hint panel content).
struct QuestStep {
    int id = 0;
    std::string speaker = "SENSEI";
    std::string line1;    // lang-table line (fallback = verified EN)
    std::string line2;    // shell hint (EN chrome)
    std::string target;   // where the step points
};

// What the shell knows about quest progress (grows when the save API lands).
struct QuestState {
    std::string tutorial = "MOVE";  // WarriorSave.tutorial (readable today)
    std::string story_step;         // _$StoryTutorialStep - "" = unknown yet
    bool training_won = false;      // session Training victory (readable today)
    bool boss_focus = false;        // MapFocus on BOSS_LYNX (save ys)
    int level = 1;                  // WarriorSave.level (readable today)
    // Landed feeds (kept for traceability of the derivation):
    std::string map_focus;                       // save MapFocus/ys
    std::vector<std::string> battles;            // save Battles/iF names
    std::map<std::string, std::string> variables;  // save quest vars (rv)
};

// THE swap point: builds QuestState from shell-readable inputs. Landed and
// wired: `story_step` (WarriorSave::story_step — "" = not started, i.e. the
// Welcome `NotStarted` step per tutorial_quests.xml StoryTutorialWelcome),
// `map_focus` (ys — `boss_focus` when it names BOSS_LYNX, cf. `qo` L1086),
// `battles` (iF — a recorded "Training" win counts as training_won
// persistently, but ONLY once the tutorial has left the Welcome beat: the
// Welcome notifications (move -> punchbag, then the Regular training-fight
// modal) run BEFORE any fight (FLOW_STATIC.md §1 rows 1-3; UI_EXCLUSIVITY
// truth table), so a stored Training record with the step still at/before
// NotStarted must not flip the banner to the punchbag step — that was the
// boot crater (fresh-port MOVE hint vs progressed-oracle wall, y90-180)).
// Still stubbed: `block_lesson` — FLOW_STATIC row 12 shows the `Fo` beat as
// unconditional, and no Stream-2 quest-var name for its mid-beat landed in
// context; expected symbol is a quest variable read beside `Fo`
// (e.g. `variables["<ShowBlock-beat>"]`) — one line when known.
inline QuestState quest_state_for(const std::string& tutorial, const std::string& story_step,
                                  bool training_won, int level,
                                  const std::string& map_focus,
                                  const std::vector<std::string>& battles,
                                  const std::map<std::string, std::string>& variables) {
    QuestState st;
    st.tutorial = tutorial;
    // Empty save step IS the Welcome step (save_system.hpp: empty = not
    // started; the quest chain names it `NotStarted`).
    st.story_step = story_step.empty() ? "NotStarted" : story_step;
    st.training_won = training_won;
    st.level = level;
    st.map_focus = map_focus;
    st.battles = battles;
    st.variables = variables;
    if (map_focus.find("BOSS_LYNX") != std::string::npos) st.boss_focus = true;
    // Quest-gated win memory (see above): battles feed the banner only
    // after the Welcome beat (He modal L1045-1048 RP path + Xob→txa bar
    // exclusivity stand — untouched). The live session flag above stays
    // ungated (a just-played Training win still advances the hint).
    if (!st.story_step.empty() && st.story_step != "NotStarted") {
        for (const std::string& b : battles) {
            if (b == "Training") {
                st.training_won = true;
                break;
            }
        }
    }
    return st;
}

namespace quest_detail {

inline QuestStep make_step(const std::string& res_root, int id, const std::string& speaker,
                           const std::string& lang_key, const std::string& fallback,
                           const std::string& line2, const std::string& target) {
    QuestStep s;
    s.id = id;
    s.speaker = speaker;
    s.line1 = lang_text(res_root, lang_key, fallback);
    s.line2 = line2;
    s.target = target;
    return s;
}

} // namespace quest_detail

// Full step table (FLOW_STATIC.md §1 rows 1–13). `story_step` empty → legacy
// 3-step path (beats 1–3), so today's shell behaves exactly as before.
inline QuestStep quest_step_for_state(const std::string& res_root, const QuestState& st) {
    using namespace quest_detail;
    if (!st.story_step.empty()) {
        const std::string& k = st.story_step;
        if (k == "FIGHT")
            return make_step(res_root, 1, "SENSEI", "tutorial_punchbag",
                             "Fascinating... Now, see that punching bag? Attack it!",
                             "Keep training - punch and kick the bag.", "Training");
        if (k == "STEP_BUY_ITEM")
            return make_step(res_root, 2, "SENSEI", "tutorial_shop",
                             "I knew you could do it. Now you need to find yourself a weapon.",
                             "Visit the SHOP from the Dojo.", "Shop");
        if (k == "MAP")
            return st.boss_focus
                       ? make_step(res_root, 4, "LYNX", "tutorial_boss_hello",
                                   "Who is this lowly worm before me? I'll not waste my "
                                   "time fighting the likes of you.",
                                   "Face BOSS LYNX in Zone 1.", "BOSS_LYNX")
                       : make_step(res_root, 3, "SENSEI", "tutorial_map",
                                   "I know where you can find him. Open your map.",
                                   "Open the MAP and find Lynx.", "Map");
        if (k == "LEARN_PERK")
            return make_step(res_root, 5, "SENSEI", "tutorial_dojo_new_move",
                             "Head to your dojo and try a new move.",
                             "Reach Level 2, then return to the Dojo.", "Dojo");
        if (k == "SHOW_DOUBLE_SWEEP")
            return make_step(res_root, 6, "SENSEI", "tutorial_dojo_new_move",
                             "Head to your dojo and try a new move.",
                             "Master the double sweep in the Dojo.", "Dojo");
        if (k == "SHOW_BLOCK")
            return make_step(res_root, 8, "SENSEI", "tutorial_block",
                             "Take a note that the block is performed automatically "
                             "if you're not making another move.",
                             "Blocking is automatic while you idle.", "Profile");
        if (k == "END")
            return make_step(res_root, 9, "SENSEI", "tutorial_return_map",
                             "For now you can return to your map and continue your journey.",
                             "Return to the MAP and continue your journey.", "Map");
        // Unknown step value (future vars) → fall through to legacy path.
    }
    if (st.tutorial == "MOVE" && !st.training_won)
        return make_step(res_root, 0, "SENSEI", "tutorial_move",
                         "Let me see you move! Show me what a shadow can do.",
                         "Press FIGHT and defeat the Punchbag dummy.", "Training");
    if (st.tutorial == "MOVE" && st.training_won)
        return make_step(res_root, 1, "SENSEI", "tutorial_punchbag",
                         "Fascinating... Now, see that punching bag? Attack it!",
                         "Keep training - punch and kick the bag.", "Training");
    return make_step(res_root, 2, "SENSEI", "tutorial_shop",
                     "I knew you could do it. Now you need to find yourself a weapon.",
                     "Visit the SHOP from the Dojo.", "Shop");
}

// Compat wrapper (legacy 3-step inputs; res_root added for lang lookup).
inline QuestStep quest_step_for(const std::string& res_root, const std::string& tutorial,
                                bool training_won) {
    return quest_step_for_state(
        res_root, quest_state_for(tutorial, "", training_won, 1, "", {}, {}));
}

} // namespace sf2::app
