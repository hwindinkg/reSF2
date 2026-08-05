// engine/game/quest_loader.hpp
//
// [Wave 9B] quests.xml loader: parses the REAL 498-quest file (assets/
// quests.xml, byte-identical to the device pull) into QuestDef records so
// host_trigger_quest_event can dispatch quest events the way the original's
// QuestManager @ 0x101c7d20 does — find the quests that list the event,
// evaluate their <Conditions>, execute the <Actions> that hold.
//
// Scope note: the loader captures the full event/condition/action structure
// of every quest, but the CONDITION EVALUATOR only implements literal
// Equal tests (plus the Not="1" negation) with the event binding
// (_$Fight / _$FightResult). Conditions built from quest queries
// ("?Fight(...).WinCount", "?Item(...).Quantity", ...) or other operators
// FAIL CLOSED — the quest does not fire — so a half-understood quest can
// never trigger user-visible actions spuriously. This is the minimum the
// FIRST_FIGHT -> ZONE_1 story chain needs: the tutorial quests are
// hardcoded in the original's tutorial state machine (0x1027d6c0), and the
// first quests.xml quest in the chain ("FirstGuardBeaten") gates on two
// literal Equal conditions.

#pragma once

#include <string>
#include <vector>

#include "quest_engine.hpp"

namespace resf2::game {

// One condition from <Conditions>: the tag name is the operator
// ("Equal"/"Less"/"Greater"/...), Value1/Value2 are the operands, Not="1"
// negates the comparison.
struct QuestCondition {
    std::string op;      // "Equal", "Less", "Greater", ...
    std::string v1;      // Value1 — literal, "_$Fight"/"_$FightResult" binding, or "?..." quest query
    std::string v2;      // Value2 — same shapes
    bool negate = false; // Not="1"
};

// One <Quest> parsed from quests.xml.
struct QuestDef {
    std::string name;
    std::vector<std::string> events;                 // <Events> child tag names ("FightEnd", "Activate", ...)
    std::vector<QuestCondition> conditions;          // <Conditions> entries
    std::vector<quest::QuestAction> actions;         // <Actions> entries
    bool fired = false;                              // fired-once guard
};

// Parse quests.xml from <root>/ or <root>/assets/ into `out`. Returns false
// when the file is absent or unparseable (the engine then runs without quest
// dispatch — same behavior as before this loader).
bool load_quest_defs(const std::string& asset_root, std::vector<QuestDef>& out);

}  // namespace resf2::game
