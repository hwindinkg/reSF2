#pragma once

// engine/game/condition_system.hpp
//
// High-level condition evaluation for move selection.
// Wraps the binary-accurate Condition* types from engine/reverse/conditions.hpp
// with a game-layer API that works with MoveDef and animation names.
//
// [ORIGINAL] ConditionCurrentAnimation::isEqual @ 0x10083ca0 (findMatchingSlotInList)
// checks whether the model's current animation matches the condition's name list.
// moves.xml uses <Condition Type="CurrentAnimation" Name="step_forward"/> to gate
// which moves can follow which (combo chains).

#include <string>
#include <vector>

namespace resf2::game {

struct MoveDef;

// ---------- Condition evaluation ----------
//
// Each move in moves.xml can declare <Conditions> that must ALL be satisfied
// for the move to be selectable. The most common condition type is
// CurrentAnimation: the fighter must be in a specific animation for the move
// to chain.
//
// [ORIGINAL] From ConditionCurrentAnimation::isEqual @ 0x10083bb0:
//   type 1-4 selects which model animation slot group to search
//   nameList is the list of animation names to match against
//   invert flips the result
//   noAnimationFlag matches when NO animation is active
//
// In our high-level system, we simplify: the MoveDef stores
// required_current_animation as a string (the Name attribute from the XML).
// The check is: does the current animation name match?

// Result of evaluating all conditions for a move.
struct ConditionResult {
    bool satisfied = true;
    // Which condition failed (empty if all passed)
    std::string failed_condition;
};

// Evaluate whether a move's conditions are satisfied given the current state.
//
// Parameters:
//   move             — the candidate move from moves.xml
//   current_anim     — name of the animation currently playing (e.g. "step_forward")
//   current_move     — name of the current move (e.g. "ForwardStep")
//   current_frame    — current animation frame index
//
// Returns ConditionResult with satisfied=true if all conditions pass.
//
// [ORIGINAL] Binary reference: ConditionCurrentAnimation::isEqual @ 0x10083bb0
// and findMatchingSlotInList @ 0x10083ca0.
ConditionResult evaluate_conditions(
    const MoveDef& move,
    const std::string& current_anim,
    const std::string& current_move,
    int current_frame
);

// Check only the CurrentAnimation condition.
// Returns true if the move's required_current_animation matches current_move
// or current_anim, or if no condition is declared.
//
// [ORIGINAL] moves.xml stores the condition as:
//   <Conditions><CurrentAnimation Name="HeavyPunch"/></Conditions>
// The Name matches the Move Name (not filename), e.g. "HeavyPunch".
// The check: current_move_ == move.required_current_animation.
// This is what the JS source (sf2.js np.isEqual line 42544) does.
bool check_current_animation_condition(
    const std::string& required_current_animation,
    const std::string& current_move,
    const std::string& current_anim
);

// Check if the current frame falls within a named interval.
// Used for SemiUninterrupt / SelfUninterrupt gating.
//
// [ORIGINAL] IntervalAttack::getFactors @ 0x10115910 returns the interval
// boundaries based on type (1 or 2). The interval is checked per-frame.
bool frame_in_interval(
    int current_frame,
    int interval_start,
    int interval_end  // -1 means "to end of animation"
);

} // namespace resf2::game
