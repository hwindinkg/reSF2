# app/ai_golden/ - Phase 6 Node-harness golden, C++ mirror (static-first).
#
# Prints the same decision-math vectors as reference/tools/ai_golden.js
# (DaPrng streams, weight-curve Gb, scripted update() trace) as JSON.
#
# NOTE (2026-09-04): NOT compiled (no toolchain on this machine).
add_executable(ai_golden main.cpp)
target_link_libraries(ai_golden PRIVATE sf2_scene)

if(MSVC)
    target_compile_options(ai_golden PRIVATE /W4)
else()
    target_compile_options(ai_golden PRIVATE -Wall -Wextra)
endif()
