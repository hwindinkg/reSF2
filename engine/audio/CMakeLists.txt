# engine/audio/CMakeLists.txt

option(RESF2_ENABLE_AUDIO "Enable audio system (OpenAL backend)" ON)

# --- OpenAL dependency ---
if(RESF2_ENABLE_AUDIO)
    # Try to find system OpenAL first
    find_package(OpenAL QUIET)
    if(NOT OpenAL_FOUND)
        message(STATUS "OpenAL not found on system — auto-downloading OpenAL-soft via FetchContent")
        include(FetchContent)
        FetchContent_Declare(
            openal-soft
            GIT_REPOSITORY https://github.com/kcat/openal-soft.git
            GIT_TAG        1.24.2
            GIT_SHALLOW    TRUE
        )
        # Suppress OpenAL-soft's own tests/examples
        set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(ALSOFT_TESTS   OFF CACHE BOOL "" FORCE)
        set(ALSOFT_UTILS   OFF CACHE BOOL "" FORCE)
        set(ALSOFT_CONFIG  OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(openal-soft)
        if(TARGET OpenAL AND NOT TARGET OpenAL::OpenAL)
            add_library(OpenAL::OpenAL ALIAS OpenAL)
        endif()
    endif()
endif()

# --- Library ---
add_library(resf2_audio STATIC
    audio.cpp
)

target_include_directories(resf2_audio PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_features(resf2_audio PUBLIC cxx_std_23)

if(RESF2_ENABLE_AUDIO AND TARGET OpenAL::OpenAL)
    target_sources(resf2_audio PRIVATE al_audio.cpp)
    target_link_libraries(resf2_audio PUBLIC OpenAL::OpenAL)
    target_compile_definitions(resf2_audio PRIVATE RESF2_ENABLE_AUDIO=1)
    target_compile_definitions(resf2_audio INTERFACE RESF2_ENABLE_AUDIO=1)
    message(STATUS "Audio: OpenAL backend ENABLED")
else()
    target_compile_definitions(resf2_audio INTERFACE RESF2_ENABLE_AUDIO=0)
    message(STATUS "Audio: OpenAL backend DISABLED (null backend only)")
endif()
