# engine/ — top-level CMake

# Stage 2 introduces the first real target: libresf2_reverse, which holds
# the read-only parsers for the original game's binary formats (.s3e,
# .dz, .atf, etc.).

# Future layout:
#   engine/runtime    -> libresf2_runtime.a   (loop, scheduler, scenes)
#   engine/renderer   -> libresf2_renderer.a  (GLES2 backend)
#   engine/physics    -> libresf2_physics.a   (hitbox, hurtbox, constraints)
#   engine/animation  -> libresf2_animation.a (skeletal anim, state machine)
#   engine/audio      -> libresf2_audio.a     (mixer, sfx, music)
#   engine/network    -> libresf2_network.a   (SmartFox2X-compatible client)
#   engine/platform   -> libresf2_platform.a  (window, GL, input, fs, threads)
#   engine/tools      -> libresf2_tools.a     (offline asset tools support)
#   engine/reverse    -> libresf2_reverse.a   (format parsers, RE helpers)

# Order matters for dependency resolution
add_subdirectory(reverse)
add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(format)
add_subdirectory(fight)
add_subdirectory(runtime)
add_subdirectory(scene)
add_subdirectory(renderer)
add_subdirectory(audio)
add_subdirectory(game)
# Empty/placeholder subdirectories listed last
add_subdirectory(animation)
add_subdirectory(physics)
add_subdirectory(tools)
add_subdirectory(network)
add_subdirectory(ui)
