# core/audio/ — the SFX engine (Phase A3). miniaudio is a single public-
# domain header (core/data/third_party/miniaudio.h); MINIAUDIO_IMPLEMENTATION
# is defined in audio.cpp, so only this lib compiles it. The engine's
# device runs its own thread; the game thread only queues play commands.
add_library(sf2_audio STATIC
    audio.cpp
)
# Include root is core/ (for "audio/audio.hpp" from the scene/app libs) and
# core/data/third_party (for "miniaudio.h").
target_include_directories(sf2_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../data/third_party
)
# The header is third-party and large — no strict warnings for this TU.
if(MSVC)
    target_compile_options(sf2_audio PRIVATE /W3 /wd4244 /wd4996)
    target_compile_definitions(sf2_audio PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(sf2_audio PRIVATE -w)
endif()