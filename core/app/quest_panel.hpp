#pragma once

// Tutorial quest UI data (Phase 7 shell gaps) — the Sensei hint chain shown
// on the Dojo screen + the quest toast on the Results screen.
//
// JS cites (verified against the repo's reference snapshot):
//   - Quest chain — `quest_extensions/tutorial_quests.xml`, pulled in from
//     `quests.xml` (`Z.Jna = "quests.xml"` while `_$StoryTutorialStep != END`;
//     JS_FLOW.md §1): `StoryTutorialWelcome` (Priority -10) sets
//     `NotificationTextMove = tutorial_move` / `NotificationTextPunchBag =
//     tutorial_punchbag` and points at `<Fight Name="Punchbag|Bosses|1"/>`
//     (the first Punchbag-dummy fight); `StoryTutorialShop` (Priority -100)
//     carries the Sensei line `SenseiDialogText = tutorial_shop`.
//   - Hint strings — `reference/www/res/lang/en.af2d6604.xml`:
//     `tutorial_move` = "Let me see you move! Show me what a shadow can do.",
//     `tutorial_punchbag` = "Fascinating... Now, see that punching bag?
//     Attack it!", `tutorial_shop` = "I knew you could do it. Now you need
//     to find yourself a weapon." (embedded below so the shell needs no
//     lang-table lookup at runtime).
//   - Save step — the Warrior's `Tutorial` attribute, `"MOVE"` on a fresh
//     save (`reference/save.xml` L10; JS_FLOW.md §9).
//
// Display-only derivation: the real chain needs the save's `Quests` +
// `Variables` tables (`p.L3(Z.Jna)` / `ha.WO`) and the `Fights` win records
// — none of which exist in `WarriorSave` (see the stream report). This
// helper maps the two fields the shell CAN read (`Tutorial`, last Training
// result via `PendingBattle`) to the Sensei banner. No save writes.

#include <string>

namespace sf2::app {

// One Sensei hint step (quest banner / hint panel content).
struct QuestStep {
    int id = 0;                // 0 = move, 1 = punchbag, 2 = shop
    const char* speaker = "SENSEI";
    const char* line1 = "";    // the lang-table line (see file comment)
    const char* line2 = "";    // the shell's actionable hint
    const char* target = "";   // where the step points ("Training"/"Shop")
};

// Derives the current tutorial step (read-only; never touches the save).
// `tutorial` = WarriorSave.tutorial ("MOVE" fresh); `training_won` = true
// once a Training (Punchbag dummy) victory was recorded this run.
inline QuestStep quest_step_for(const std::string& tutorial, bool training_won) {
    QuestStep s;
    if (tutorial == "MOVE" && !training_won) {
        s.id = 0;
        s.line1 = "Let me see you move! Show me what a shadow can do.";
        s.line2 = "Press FIGHT and defeat the Punchbag dummy.";
        s.target = "Training";
        return s;
    }
    if (tutorial == "MOVE" && training_won) {
        s.id = 1;
        s.line1 = "Fascinating... Now, see that punching bag? Attack it!";
        s.line2 = "Keep training - punch and kick the bag.";
        s.target = "Training";
        return s;
    }
    s.id = 2;
    s.line1 = "I knew you could do it. Now you need a weapon.";
    s.line2 = "Visit the SHOP from the Dojo.";
    s.target = "Shop";
    return s;
}

} // namespace sf2::app
