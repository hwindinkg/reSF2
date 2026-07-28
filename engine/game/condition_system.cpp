// engine/game/condition_system.cpp
//
// High-level condition evaluation for move selection.
// [ORIGINAL] Binary reference: ConditionCurrentAnimation::isEqual @ 0x10083bb0,
// findMatchingSlotInList @ 0x10083ca0.

#include "condition_system.hpp"
#include "types.hpp"

#include <cstdio>
#include <string>

namespace resf2::game {

bool frame_in_interval(
    int current_frame,
    int interval_start,
    int interval_end
) {
    if (interval_start < 0) return false;  // no interval declared
    // moves.xml uses 1-based frames; current_frame is 0-based.
    // Start=1 means frame 0 is the first frame of the interval.
    int start = interval_start - 1;
    int end = interval_end > 0 ? interval_end - 1 : 9999;
    return current_frame >= start && current_frame <= end;
}

bool check_current_animation_condition(
    const std::string& required_current_animation,
    const std::string& current_move,
    const std::string& current_anim
) {
    if (required_current_animation.empty()) return true;

    // [ORIGINAL] moves.xml <CurrentAnimation Name="X"/> matches the Move Name
    // of the predecessor. PC source: sf2.js np.isEqual() (line 42544).
    // The primary check is against the current MOVE name (e.g. "HeavyPunch"),
    // with a fallback to the animation name for moves that don't set
    // current_move_ but do play the right animation.
    if (current_move == required_current_animation) return true;

    // Fallback: match animation name (filename without .bin extension)
    // This handles cases where current_move_ is not yet set but the animation
    // is correct (e.g. first frame of a combo chain).
    if (current_anim == required_current_animation) return true;

    return false;
}

ConditionResult evaluate_conditions(
    const MoveDef& move,
    const std::string& current_anim,
    const std::string& current_move,
    int current_frame
) {
    ConditionResult result;

    // [ORIGINAL] CurrentAnimation condition — the most common condition type.
    // 3key combos require the current animation to match a specific name.
    // e.g., DoublePunch requires CurrentAnimation="HeavyPunch".
    if (!move.required_current_animation.empty()) {
        if (!check_current_animation_condition(
                move.required_current_animation, current_move, current_anim)) {
            result.satisfied = false;
            result.failed_condition = "CurrentAnimation=" +
                move.required_current_animation +
                " (current_move='" + current_move +
                "' current_anim='" + current_anim + "')";
            return result;
        }
    }

    // Future: Distance conditions, interval conditions, etc.
    // Currently handled separately in game.cpp move selection.

    return result;
}

} // namespace resf2::game
