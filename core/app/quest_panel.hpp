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
// Display-only derivation. THE swap point is `quest_state_for()`: it takes
// only what the shell can read today (Tutorial string + session Training
// result). When Stream 1 lands the save fields, its body becomes a direct
// read of the EXACT missing symbols (see report):
//   WarriorSave::story_step  (_$StoryTutorialStep, `p.o.zi.HH`, L964/so L1119)
//   WarriorSave::quest_vars  (zi/HH table — STEP_BUY_ITEM/MAP/... values)
//   WarriorSave::battles     (iF `Battles` map — WDa L256 + Fights/yc wins)
//   WarriorSave::map_focus   (ys `MapFocus`, m5/Ttb L255)
//   WarriorSave::disciple    (Y0 `Disciple`, oub/Nfb — Dojo toggle)
// Until then `story_step` stays empty and the legacy 3-step path serves
// (MOVE → MOVE+won → non-MOVE), which matches beats 1–3 above.
//
// line1 comes from the runtime lang table (`lang_table.hpp`) with the
// verified EN embedded as fallback (titles in en.af2d6604.xml); line2 is
// shell-authored chrome (EN only). No save writes, ever.

#include <string>

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
    std::string story_step;         // _$StoryTutorialStep — "" = unknown yet
    bool training_won = false;      // session Training victory (readable today)
    bool boss_focus = false;        // MapFocus on BOSS_LYNX (needs save ys)
    bool block_lesson = false;      // mid SHOW_BLOCK beat (needs quest vars)
    int level = 1;                  // WarriorSave.level (readable today)
};

// THE swap point (see file comment): builds QuestState from shell-readable
// inputs. When WarriorSave::{story_step,quest_vars,battles,map_focus}
// lands, fill story_step/boss_focus/block_lesson here — callers unchanged.
inline QuestState quest_state_for(const std::string& tutorial, bool training_won,
                                  int level) {
    QuestState st;
    st.tutorial = tutorial;
    st.training_won = training_won;
    st.level = level;
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
            return st.block_lesson
                       ? make_step(res_root, 8, "SENSEI", "tutorial_block",
                                   "Take a note that the block is performed automatically "
                                   "if you're not making another move.",
                                   "Blocking is automatic while you idle.", "Profile")
                       : make_step(res_root, 7, "SENSEI", "tutorial_profile_moves",
                                   "You can take a look at all of your moves in the "
                                   "profile screen.",
                                   "Open your moves in the PROFILE screen.", "Profile");
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
    return quest_step_for_state(res_root, quest_state_for(tutorial, training_won, 1));
}

} // namespace sf2::app
