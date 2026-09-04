#pragma once

// Act/Rd cutscene player (app layer) — the native port of JS `Rd` (class
// `Rd extends ef`, sf2.502f0946.js L2095, g="425"), fed by the boss
// multi-intro list (`ca.hCa` L431-432 builds `lD`: every zone-chain battle's
// intro for FightBosses/FightBossesReplayable/FightFinalTitan, `uP` index,
// `Y1` has-intro flag).
//
// JS 8-step machine (`Rd.aa`, L2095), mirrored 1:1 here:
//   0: fade in (`ed(1)` ramp) + music duck; done → stop music, `Ut()`
//      (act music: `lb.GMa(1); ta.Zla(); lb.OS("act")`), step++
//   1: `ed(1)` hold → step++
//   2: lines present ? step++ (→3) : step += 2 (→4, textless hold)
//   3: per-line timer (`ed(lines[u2].value/60)`): advance `u2`, show next
//      localized line (`Y.na(key)`); exhausted → step = 5
//   4: `ed(5)` 5 s hold → step++
//   5: fade out → step++
//   6: `ge()` completion callback (try/catch) → step++
//   7: `ed(.5)` → `end()`: resume menu music (`lb.OS()`), destroy
// `tza()` guard (non-empty player list) and `oea()` width (1024) are port
// N/A. `ed(x)` easing is read as a seconds-linear ramp (assumption noted).
//
// Port choices (all documented at the call sites): durations in seconds;
// skip-on-press jumps to the fade-out (step 5) so the callback + music-stop
// still run (JS skip path untraced); end stops (not resumes) music — the
// shell never started menu music; headless start completes instantly with
// no music calls. Pure presentation: tick/draw only, no fight hooks.

#include <string>
#include <vector>

#include "audio/audio.hpp"

namespace sf2::app {

// One intro line (JS `lines[]` key + value/60 s; text pre-resolved).
struct ActLine {
    std::string text;
    float seconds = 2.5f;
};

class ActPlayer {
public:
    // Arms the sequence (fade-in from black + act music unless headless, in
    // which case the player is immediately done and silent).
    void start(const std::vector<ActLine>& lines, bool headless) {
        lines_ = lines;
        step_ = 0;
        time_ = 0.0f;
        cursor_ = 0;
        done_ = false;
        if (headless) {
            done_ = true;
            step_ = 8;
            return;
        }
        sf2::audio::AudioEngine::instance().play_music("act");
    }

    // Advances one frame; skip_pressed jumps to the fade-out (step 5).
    void tick(float dt, bool skip_pressed) {
        if (done_) return;
        if (dt < 0.0f) dt = 0.0f;
        if (skip_pressed && step_ < 5) {
            step_ = 5;
            time_ = 0.0f;
        }
        time_ += dt;
        switch (step_) {
            case 0:  // fade in (1 s) → act music already started
                if (time_ >= 1.0f) {
                    step_ = 1;
                    time_ = 0.0f;
                }
                break;
            case 1:  // 1 s hold
                if (time_ >= 1.0f) {
                    step_ = 2;
                    time_ = 0.0f;
                }
                break;
            case 2:  // branch on lines
                step_ = lines_.empty() ? 4 : 3;
                time_ = 0.0f;
                break;
            case 3: {  // per-line timers
                const float dur =
                    cursor_ < lines_.size() ? lines_[cursor_].seconds : 0.0f;
                if (time_ >= dur) {
                    time_ = 0.0f;
                    ++cursor_;
                    if (cursor_ >= lines_.size()) step_ = 5;
                }
                break;
            }
            case 4:  // textless 5 s hold
                if (time_ >= 5.0f) {
                    step_ = 5;
                    time_ = 0.0f;
                }
                break;
            case 5:  // fade out (1 s)
                if (time_ >= 1.0f) {
                    step_ = 6;
                    time_ = 0.0f;
                }
                break;
            case 6:  // completion point (shell acts after done())
                step_ = 7;
                time_ = 0.0f;
                break;
            case 7:  // 0.5 s → end (music stop)
                if (time_ >= 0.5f) finish();
                break;
            default: finish(); break;
        }
        if (skip_pressed && step_ >= 5 && !done_) finish();
    }

    bool active() const { return !done_; }
    bool done() const { return done_; }
    int step() const { return step_; }

    // Black-overlay alpha (ramps up on step 0, holds through the text,
    // ramps down on step 5).
    float fade() const {
        if (done_) return 0.0f;
        if (step_ == 0) return time_ >= 1.0f ? 1.0f : time_;
        if (step_ == 5) return time_ >= 1.0f ? 0.0f : 1.0f - time_;
        return 1.0f;
    }

    // Current line text ("" outside step 3 / when empty).
    std::string line() const {
        if (step_ != 3 || cursor_ >= lines_.size()) return "";
        return lines_[cursor_].text;
    }

    void reset() {
        lines_.clear();
        step_ = 0;
        time_ = 0.0f;
        cursor_ = 0;
        done_ = false;
    }

private:
    void finish() {
        if (!done_) {
            done_ = true;
            step_ = 8;
            sf2::audio::AudioEngine::instance().stop_music();
        }
    }

    std::vector<ActLine> lines_;
    int step_ = 0;
    float time_ = 0.0f;
    std::size_t cursor_ = 0;
    bool done_ = false;
};

} // namespace sf2::app
