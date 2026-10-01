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
//              DoubleStep/Dash/Roll) -> swish1..4.wav (0.45 - a step is
//              quieter than a jump)
//   "snd_click_1" -> UI BUTTON press, the JS `rb.um()` id (65535, played by
//              `Bb.Xw` for every button) -> click_1.wav
//   "snd_click_2" -> tab/cell strip selection, JS `rb.iJa()` (65570)
//   "snd_focus_1" -> scroll/arrow/icon-cell tap, JS `rb.PS()` (65579)
//   "snd_buy"/"snd_upgrade"/"snd_learn"/"snd_gong" -> the JS `rb.U3`/`QS`/
//              `Xkb`/`Wkb` ids (65569/65696/65598/65591)
// A tap that is not inside a JS sound-triggering widget must play NOTHING —
// the JS has no background/empty-space tap sound (see sfx_table.hpp).
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
    // `loop` is the JS `ta.ak(name, looped)` L1264 flag (the `<Sound
    // Looped="1">` descriptor, `fm.ceb`); a looped voice loops until a
    // `stop(name)` (`ta.Jwb`) stops it. Defaults to the one-shot path every
    // UI/combat caller uses.
    void play(const std::string& event, bool loop = false);

    // Stops every live voice of a named event — JS `ta.Jwb(a)` (L1264):
    // `a=ta.WBa(a); a!=null && L.K.$f.stop(a)`. The name resolves through
    // the same `ta.WBa` table `play` uses; an unknown name is a silent
    // no-op (JS `WBa` miss = nothing stopped). Never blocks, never throws.
    void stop(const std::string& event);

    // Music streaming (JS `ta.Ut(name, loop=true)` L1264-1265):
    // `assets/music/<name>.mp3` streamed from disk (never fully preloaded).
    // Same-track re-play is a no-op. Silent no-op when the engine is off or
    // the file is missing (headless-safe); every call is counted + logged.
    // NOTE: www/res ogg/m4a are NOT wired (miniaudio has no AAC decoder).
    void play_music(const std::string& track, bool loop = true);
    // JS `lb.OS(a="menu", b=true)` (L1276-1277): `lb.rJ||(lb.rJ=!0,ta.Ut(a,b))`
    // — the play-once-across-screen-hops guard. The Map/Dojo call this with
    // `menu`; after a fight/act cleared the guard the track replays.
    void play_music_once(const std::string& track, bool loop = true);
    // JS `lb.rJ=!1` (fight start `ai.Ut` L2008; act `Rd.Ut`/`Rd.end`
    // L2096-2098): clear the guard so the next `play_music_once` replays.
    void reset_music_guard() { music_guard_ = false; }
    bool music_guard() const { return music_guard_; }
    // JS `ta.VT(a)` (L1264): `L.K.$f.uF(a?0:1); ta.ZD=a`. `uF` is the bus the
    // voice `cy.play` (L1240813) routes to when `audio.tR` is true, and `tR` is
    // set exactly for the music assets (`f.tR=(new Ua("music","")).match(...)`
    // L29718) — so `uF`/`ta.ZD` is the MUSIC bus, NOT the SFX bus. Read back by
    // `lb.Lz()` (L1276) -> `ta.ZD`. This is a BUS volume mute, NOT a stop: the
    // track keeps its position and unmute resumes it.
    void set_music_muted(bool muted);
    bool music_muted() const { return music_muted_; }
    // JS `ta.WT(a)` (L1264): `L.K.$f.cMa(a?0:1); ta.$D=a`. `cMa` is the bus a
    // non-`tR` voice routes to (`oBa`, L1240811) — the SOUND/SFX bus. Read back
    // by `lb.Mz()` (L1276) -> `ta.$D`; persisted as `<Sounds>/<Sound>@Mute`
    // (`ta.WT`'s saver). `lb.WT`/`lb.VT` (L1276) persist via `p.TJ.save()`.
    void set_sfx_muted(bool muted);
    bool sfx_muted() const { return sfx_muted_; }
    // JS `L.K.$f.uF(0)` / `uF(1)` — the pause dialog `Dr` (L2065-2067): its ctor
    // `uF(0)` ducks the MUSIC bus for the dialog's lifetime; `Dr.B()` (L2067) and
    // `Dr.resume()` (L2067) restore `uF(1)`. Modelled as a SECOND gain term
    // composed with the mute flag, so the persisted `<Music>@Mute` stays truthful
    // (JS `uF(1)` in `Dr.B` writes the raw gain node and would clobber an explicit
    // mute — an artifact of sharing one gain node, not replicated).
    void set_music_ducked(bool ducked);
    bool music_ducked() const { return music_ducked_; }
    // The bus gains JS `ta.VT`/`ta.WT` write (1 or 0). Exposed so the headless
    // gates prove the two source buses are INDEPENDENT: muting SFX must not
    // touch the music bus (JS routes them to separate `uF`/`cMa` gains, L1240811).
    float sfx_bus_gain() const { return sfx_muted_ ? 0.0f : 1.0f; }
    // JS `uF`: 0 while the pause `Dr` dialog is open (its ctor) OR the music bus
    // is muted (`ta.ZD`); 1 otherwise. Both write the same JS gain node.
    float music_bus_gain() const {
        return (music_muted_ || music_ducked_) ? 0.0f : 1.0f;
    }
    // The miniaudio engine MASTER volume (1.0 = untouched), or -1 while no
    // device runs. JS keeps a master bus (`s0`, type 5, L1240812) that neither
    // `ta.VT` nor `ta.WT` touches; a source-bus mute must leave the master at 1.
    float master_gain() const;
    void stop_music();
    // [latency probe] Per-frame audio-feed measurement (no-op unless
    // `SF2_AUDIO_LATENCY=1`). The port also self-polls it on every play/music
    // trigger, so it is measurable without any app-loop hook.
    void latency_tick();
    std::string music_track() const;
    std::uint64_t music_plays() const;

    // Diagnostics (headless verification).
    std::uint64_t played_total() const { return played_total_; }
    std::uint64_t played(const std::string& event) const;
    // JS `Ss.pxb` (char 1238696), reached from `Ss.O6a` (char 1238636): a
    // non-`tR` cue re-triggered within `rxb.v[id] || qxb` seconds
    // (`qxb = 0.05`, char 1236651) is SUPPRESSED — `pxb` returns true and
    // `O6a` returns -1 without ever starting the voice. `suppressed_total()`
    // counts those suppressed re-triggers. `play()` is keyed by the
    // `ta.WBa` asset-event name, and `WBa` maps a name to exactly one cue id,
    // so the per-cue-id throttle is per-event here. `rxb` is never populated
    // by shipped content (`new jd` only, char 1236634), so every non-`tR`
    // event uses the `qxb = 0.05` default window.
    std::uint64_t suppressed_total() const { return suppressed_total_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;  // owns the miniaudio state (hpp stays header-light)
    bool enabled_ = false;
    bool music_guard_ = false;  // JS `lb.rJ` (L1276).
    bool music_muted_ = false;  // JS `ta.ZD` (L1264), `lb.Lz()` (L1276) = MUSIC.
    bool sfx_muted_ = false;    // JS `ta.$D` (L1264), `lb.Mz()` (L1276) = SOUND.
    bool music_ducked_ = false;  // JS `L.K.$f.uF(0)` — the pause `Dr` dialog (L2065).
    std::uint64_t played_total_ = 0;
    std::uint64_t suppressed_total_ = 0;  // JS `Ss.pxb` suppressed plays
};

}  // namespace sf2::audio