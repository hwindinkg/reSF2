#pragma once

// SFX engine for the native port (Phase A3 — sound events). Plays the
// game's REAL wav samples (assets/sounds/*.wav — the APK sfx, e.g.
// hit1..6 / f_pl_jump* / swish* / buy) through a miniaudio device. The
// device runs its own audio thread, so play() only queues a start/seek —
// it never blocks the 60 Hz game loop.
//
// Events map 1:1 to the original's audio triggers:
//   "hit"   -> a landed hit (FightController::apply_hit; JS ca.Cgb plays
//              the impact sfx)      -> hit1..hit6.wav   (volume 0.85)
//   "jump"  -> jumping moves start (JumpUp/Jump*Kick/BackFlip/WallJump;
//              JS plays the jump whoosh) -> f_pl/m_pl_jump1..3.wav (0.8)
//   "step"  -> stepping/dash moves start (StepForward/StepBack/
//              DoubleStep/Dash/Roll) -> swish1..4.wav (0.45 — a step is
//              quieter than a jump)
//   "click" -> UI button press (the menu "snd_click_1" equivalent)
//              -> buy.wav (the closest single UI tick in the sample set)
//
// Design: ONE preloaded sample per (event, voice). Every event has a small
// pool of overlapping voices so rapid re-triggers MIX instead of cutting
// each other off; a round-robin cursor spreads consecutive plays over the
// event's candidate files (the game's own hit1..6 / f_pl_jump* pools).
//
// The engine is a process-wide singleton: App::init() -> init(), the
// scene/screens call play(...), App::shutdown() -> shutdown(). If no wav
// samples resolve, a generated sine buffer (beep) stands in for every
// event — the engine still "plays", so the integration can be verified
// headless via the played() counters in the shutdown log.

#include <cstdint>
#include <string>

namespace sf2::audio {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // The process-wide engine (App owns the init/shutdown lifecycle).
    static AudioEngine& instance();

    // Loads the event samples from `res_root` (the resolved sfx dir; see
    // audio.cpp). Starts the device. Returns false only when even the beep
    // fallback cannot be set up (no audio device at all) — callers still
    // count plays and log, the engine is just silent.
    bool init(const std::string& res_root);
    void shutdown();
    bool enabled() const { return enabled_; }

    // Fire-and-forget play of a named event ("hit"/"jump"/"step"/"click").
    // Never blocks, never throws. Counts EVERY call (even with the engine
    // off) so the headless log proves the integration: played("hit") > 0.
    void play(const std::string& event);

    // Music streaming (JS `ta.Ut(name, loop=true)` L1264-1265):
    // `assets/music/<name>.mp3` streamed from disk (never fully preloaded).
    // Same-track re-play is a no-op. Silent no-op when the engine is off or
    // the file is missing (headless-safe); every call is counted + logged.
    // NOTE: www/res ogg/m4a are NOT wired (miniaudio has no AAC decoder).
    void play_music(const std::string& track, bool loop = true);
    void stop_music();
    std::string music_track() const;
    std::uint64_t music_plays() const;

    // Diagnostics (headless verification).
    std::uint64_t played_total() const { return played_total_; }
    std::uint64_t played(const std::string& event) const;

private:
    struct Impl;
    Impl* impl_ = nullptr;  // owns the miniaudio state (hpp stays header-light)
    bool enabled_ = false;
    std::uint64_t played_total_ = 0;
};

}  // namespace sf2::audio