# app/move_probe/ - moves.xml parser + condition evaluator probe (Phase 3.2a).
#
# Loads res/moves.xml (extracted copy or from xml.dat), parses all moves
# into MoveDef (template inheritance, conditions, intervals, locks, align),
# verifies the HighPunch contract, then evaluates HighPunch's <Conditions>
# against a neutral and a Punch-Tap context and prints the trace.
#
# Usage: move_probe [moves.xml path] [xml.dat path]
add_executable(move_probe main.cpp)
target_link_libraries(move_probe PRIVATE sf2_scene sf2_data)

if(MSVC)
    target_compile_options(move_probe PRIVATE /W4)
else()
    target_compile_options(move_probe PRIVATE -Wall -Wextra)
endif()
