# 12 — Main loop model

> Status: Stage 3 high-level. The exact main-loop function address
> in `.s3e` `.text` is not yet recovered (needs disassembly cross-ref
> with `s3eDeviceCheckQuitRequest` import). The structure below is
> inferred from (a) Marmalade SDK conventions, (b) Cocos2d-x 2.x
> `CCDirector::mainLoop()` source, (c) string evidence in `.s3e`.

## Overview

Shadow Fight 2 runs a **single-threaded main loop** with these
characteristics:

- **Variable-step updates** driven by `s3eTimerGetMs()` wall-clock.
- **60 FPS target** (typical for 2014 mobile fighters).
- **Cooperative multitasking** — `s3eDeviceYield(0)` lets the OS
  breathe each frame; no preemption.
- **Cocos2d-x 2.x-style scene graph** walked once per frame.
- **Fixed-step physics** is **not** used — combos are timed against
  wall-clock ms, which is acceptable because the game has no online
  competitive mode (raids are PvE with no shared state).

## Single-threaded, not multi-threaded

Evidence:

- Only 1 import of `s3eThreadCreate` (would be many if multi-threaded).
- No mutex / condition-variable imports (`s3eMutex*` not in import list).
- `LoaderThread` in Java is named "Loader" but is actually the only
  game thread — it loads AND runs the game in the same thread.
- Audio runs on a separate thread (managed by `s3eSoundStart` /
  `generateAudio` callback), but the game logic does not sync with it
  beyond setting volumes.

reSF2 will follow the same model: one main thread + one audio thread.
No worker thread pool. Simpler is better for a 2D fighter.

## Main loop pseudocode (recovered)

```cpp
// In libs3e_android.so after JNI_OnLoad, the loader calls the .s3e
// entry point, which is Marmalade's main(). Marmalade's main() in
// turn calls the game's user_main() (defined in the .s3e binary).

int user_main() {
    // === INIT (one-time) ===
    s3e::Initialize();

    // Initialize Cocos2d-x-style subsystems:
    //   CCDirector::sharedDirector()->initOpenGLView(view);
    //   CCTextureCache::sharedTextureCache();
    //   CCActionManager::sharedManager();
    //   CCAnimationCache::purgeSharedAnimationCache();

    // Game-specific init:
    //   1. Read assets/settings.xml -> enumerate game data XMLs.
    //   2. Parse assets/localSettings.bin (AES decrypt + load save).
    //   3. Parse all game XMLs (achievements, quests, perks, models,
    //      localizations, raid stages, tactic settings, user defaults).
    //   4. Mount assets/animations.dz and assets/files.dz.
    //   5. Build the first scene (LoadingScreen -> EntryScreen).
    //   6. Push first scene to CCDirector.

    uint32_t last_frame_ms = s3eTimerGetMs();

    // === MAIN LOOP ===
    while (!s3eDeviceCheckQuitRequest()) {
        s3eDeviceYield(0);              // cooperative yield to OS
        s3eKeyboardUpdate();            // poll key state
        s3ePointerUpdate();             // poll touch state

        const uint32_t now = s3eTimerGetMs();
        const uint32_t dt_ms = now - last_frame_ms;
        last_frame_ms = now;

        // Clamp dt to avoid spiral-of-death on hitches.
        // (Cocos2d-x 2.x default: max dt = 200ms.)
        const uint32_t clamped_dt = std::min(dt_ms, uint32_t(200));

        // === UPDATE phase ===
        // Cocos2d-x CCDirector::mainLoop():
        //   1. CCActionManager::update(dt)        -- advance all running actions
        //   2. CCDirector::updateScene(dt)        -- game-side scene update
        //   3. CCDrawNode / scheduler callbacks   -- per-node update(dt)
        //
        // Game-side scene update (Module::update(dt)) does:
        //   - Process pending touch / key events
        //   - Update active Fight (advance animations, run AI tick,
        //     resolve hitbox/hurtbox intersections, apply damage)
        //   - Update particles (CCParticle)
        //   - Update camera (follow + shake decay + zoom interp)
        //   - Update audio (set volumes, trigger SFX)
        //   - Update network (SmartFox poll, fire callbacks)

        // === RENDER phase ===
        // IwGxClear();                              -- clear framebuffer
        // CCDirector::drawScene():                  -- recursive visit/draw
        //   scene->visit()                          -- depth-first walk
        //     layer->visit()
        //       sprite->draw()                      -- batched sprite draws
        //       label->draw()
        //       particle->draw()
        // IwGxFlush();
        // IwGxSwapBuffers();                        -- present
    }

    // === SHUTDOWN (one-time) ===
    //   1. Write assets/localSettings.bin (encrypt + save).
    //   2. Disconnect SmartFox client (if connected).
    //   3. Release all textures from CCTextureCache.
    //   4. CCDirector::purgeDirector().
    //   5. s3e::Terminate();

    return 0;
}
```

## Per-subsystem update order

Within the `UPDATE phase`, the order matters because later subsystems
read state written by earlier ones:

| # | Subsystem | Reads | Writes |
| - | --------- | ----- | ------ |
| 1 | Input (touch / key) | OS state | `InputState` struct |
| 2 | Network (SmartFox) | socket | event queue |
| 3 | Game events (process network events + UI events) | event queue | scene mutations |
| 4 | Animation (`ModelAnimation::update`) | current state | new bone transforms |
| 5 | AI (`RulesInspector::checkEvent`) | game state | NPC decisions |
| 6 | Physics (hitbox vs hurtbox) | bone transforms | hit events |
| 7 | Battle logic (`Fight::update`) | hit events | damage, HP, score |
| 8 | Particles (`CCParticle::update`) | emitter state | particle positions |
| 9 | Camera (follow + shake + zoom) | player position | view matrix |
| 10 | Audio (trigger SFX, set volumes) | hit events, state | audio queue |

The render phase then walks the scene graph in `zOrder` order (children
drawn after parents; siblings drawn in ascending `zOrder`).

## Frame timing budget (60 FPS target)

| Phase | Budget (ms) | Notes |
| ----- | ----------: | ----- |
| Yield + input poll | 0.2 | `s3eDeviceYield(0)` + `s3eKeyboardUpdate` + `s3ePointerUpdate` |
| Network poll | 0.5 | `BitSwarmClient::processEvents()` non-blocking |
| Game update | 4.0 | Animation + AI + physics + battle logic + particles + camera |
| Audio dispatch | 0.3 | Set volumes, queue SFX |
| Render: scene walk | 3.0 | `CCDirector::drawScene()` |
| Render: GPU submit | 5.0 | Batched sprite draws, atlas binding, GL state changes |
| Render: present | 2.0 | `IwGxSwapBuffers` + vsync wait |
| **Total** | **15.0** | Leaves ~1.6 ms slack for 60 FPS (16.67 ms budget) |

These are estimates based on the game's observed smoothness on 2014
hardware (Snapdragon 801, ~10 GFLOPS GPU). reSF2 on modern hardware
will have 10–50× more headroom.

## Pause / resume behavior

When the Android `Activity` is paused (`onPause`), `LoaderThread` calls
`nativePause()` which sets a flag. The main loop checks this flag at
the top of each iteration:

```cpp
while (!s3eDeviceCheckQuitRequest()) {
    if (s3eDeviceIsPaused()) {
        s3eDeviceYield(100);  // sleep 100ms while paused
        continue;
    }
    // ... normal frame ...
}
```

While paused:

- The audio thread continues running (so music can fade out).
- The network socket stays open (so the SmartFox server doesn't
  time out the session — `BitSwarmClient::LagMonitor` sends keep-alive
  pings).
- The OpenGL context is **not** destroyed (Marmalade keeps it alive
  in the background).

On resume:

- `nativeResume()` clears the pause flag.
- The first frame after resume has a large `dt` — the loop clamps it
  to 200 ms (per Cocos2d-x convention) to avoid spiral-of-death.
- Audio fade-in over 0.5 s.

reSF2 will follow the same pattern, but using platform-native pause
signals (`WM_ACTIVATE` on Windows, `NSApplicationDidResignActive` on
macOS, `APP_CMD_PAUSE` on Android).

## Fixed-step vs variable-step (decision)

Fighting games traditionally use **fixed-step** physics for
deterministic replays and online rollback. Shadow Fight 2 does **not**
do this — it uses **variable-step** for everything.

Evidence:

- No `s3eTimerSchedule` import (Marmalade's fixed-step timer).
- Only `s3eTimerGetMs` (free-running wall clock).
- The `.atf` tactics files store frame windows as **milliseconds**,
  not frame counts (verified by zlib-decompressing
  `kusarigama_nunchaku.atf` — the binary data after the weapon names
  contains ms-scale integers).

**Implication for reSF2**: reSF2 can also use variable-step. This
simplifies the implementation — no accumulator pattern, no
fixed-step interpolation. The downside is that replays will not be
deterministic across machines with different framerates, but Shadow
Fight 2 has no replay system anyway.

## Multiplayer (raid) timing

Raids use SmartFoxServer 2X for matchmaking + lobby + leaderboard, but
the actual raid fight is **single-player PvE** (player vs boss). There
is no real-time PvP netcode.

This means:

- No client-side prediction needed.
- No rollback.
- No input synchronization.
- Only post-fight result upload (one SmartFox extension request at the
  end of the raid).

reSF2's `engine/network/` is therefore much simpler than a fighting
game's netcode would normally be. We just need:

- Login + room join (SmartFox2X protocol).
- Periodic leaderboard queries.
- Result upload after each raid.

## What's still unknown

1. **Exact main-loop function address** in `.s3e` `.text`. Requires
   cross-referencing the `s3eDeviceCheckQuitRequest` import (entry #?
   in `s3e_imports.txt`) with its callsites in `.text`. Stage 4 task.
2. **Exact Cocos2d-x update order** (does the game override
   `CCDirector::mainLoop`?). Likely no — the game follows the
   Cocos2d-x 2.x default. Stage 7.2 will confirm by checking the
   public Cocos2d-x 2.x source.
3. **Are there any background worker threads?** The audio thread is
   one (managed by Marmalade's `s3eSound`). Are there others? The
   OBB downloader runs in a Java service (separate process). The
   SmartFox client uses boost::asio which can be sync or async — the
   game appears to use sync (single-threaded main loop polls). Stage
   7.10 will confirm.
4. **dt clamp value**. Cocos2d-x 2.x default is 200 ms (5 FPS minimum).
   The game may use a different value. Not critical — reSF2 will use
   the Cocos2d-x default.

## reSF2 main loop target (clean-room)

```cpp
// engine/runtime/loop.cpp (Stage 7.1)

namespace resf2::runtime {

auto Loop::run(IGame& game) -> int {
    if (!platform_->init()) return 1;
    if (!renderer_->init(*platform_)) return 2;
    if (!audio_->init(*platform_)) return 3;

    game.on_init(*platform_, *renderer_, *audio_);

    auto last_ms = platform_->now_ms();
    bool paused = false;

    while (!platform_->should_quit()) {
        platform_->poll_events();

        if (platform_->is_paused()) {
            if (!paused) { game.on_pause(); paused = true; }
            platform_->sleep_ms(100);
            continue;
        }
        if (paused) { game.on_resume(); paused = false; last_ms = platform_->now_ms(); }

        const auto now = platform_->now_ms();
        const auto dt  = std::min(now - last_ms, std::uint32_t(200));
        last_ms = now;

        game.on_update(dt);
        renderer_->begin_frame();
        game.on_render(*renderer_);
        renderer_->end_frame();
    }

    game.on_shutdown();
    audio_->shutdown();
    renderer_->shutdown();
    platform_->shutdown();
    return 0;
}

}  // namespace resf2::runtime
```

Key differences from the original:

- C++20 coroutines not used (kept simple).
- `IGame` interface (reSF2's own; Nekki's `Module` is the equivalent).
- `dt` is `std::uint32_t` (ms); no sub-ms precision needed for a 60 FPS
  game.
- No separate input poll phase — `platform_->poll_events()` does it
  all and stores results in an `InputState` that `game.on_update`
  reads.
- Pause/resume are explicit callbacks (not flag polling).

This is the structure that will land in `engine/runtime/loop.{hpp,cpp}`
in Stage 7.1.
