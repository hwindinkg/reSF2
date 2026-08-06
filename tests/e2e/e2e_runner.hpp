// tests/e2e/e2e_runner.hpp
//
// E2E harness for the REAL resf2_app binary (the test_step_cooldown /
// test_input_trace pattern, generalized):
//
//   - write_input_script()  : emit an --input-script text file
//                             ("frame <N> keydown|keyup <KEY>", # comments)
//   - run_app()             : spawn resf2_app with a script + CLI flags,
//                             capture its stdout (--dump-state [STATE] rows)
//                             and the app's debug log (resf2_debug.log), with
//                             deterministic per-run output files under <root>/build
//   - parse_state_frames()  : [STATE] rows -> structs
//   - first_frame_with()    : frame index of the first frame playing an anim
//   - filter_lines()        : substring filter over captured lines
//
// The point of the harness is 1:1 play verification: the same binary the
// player runs, driven by the same scripted-input path, observed through the
// same diagnostics the original's behavior was pinned with. Hermetic tests
// cannot see these paths; the harness can.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace e2e {

// One input-script event. frame is a GAMEPLAY frame (1-based); key names are
// the ones parse_key_name accepts ("D", "Space", "P", "O", ...).
struct InputEvent {
    int frame = 0;
    bool down = false;       // true = keydown, false = keyup
    std::string key;
};

// One parsed [STATE] row from --dump-state.
struct StateFrame {
    long long frame = 0;
    int move_state = 0;
    unsigned hit_anim = 0;
    std::string anim;
    std::string move;
    float px = 0.0f;         // player world x
    float py = 0.0f;         // player world y
    float ex = 0.0f;         // enemy world x
    float ey = 0.0f;         // enemy world y
    float anchor_x = 0.0f;   // <Align> anchor world x (0 if none)
    std::string raw;         // the full line, for extra fields
};

// Everything needed to boot one app instance.
struct RunSpec {
    std::filesystem::path app;      // path to resf2_app(.exe)
    std::filesystem::path root;     // repo root, passed as --assets
    std::filesystem::path script;   // input script to feed
    std::string out_name;           // <root>/build/<out_name>.out (+ .log)
    int max_frames = 900;           // --max-frames (POLL frames)
    std::vector<std::string> extra_args;  // e.g. {"--scene","battle"}
    bool dump_state = true;         // pass --dump-state
    bool no_log = false;            // pass --no-log (skips debug-log capture)
};

struct RunResult {
    bool launched = false;
    int exit_code = -1;
    std::string stdout_text;
    std::vector<std::string> stdout_lines;
    std::vector<std::string> log_lines;  // resf2_debug.log, if it was produced
};

// Write an input script in the format the app's load_input_script expects.
// Returns false if the file could not be written.
bool write_input_script(const std::filesystem::path& path,
                        const std::vector<InputEvent>& events);

// Boot the real resf2_app with the spec; capture stdout + debug log.
RunResult run_app(const RunSpec& spec);

// Parse [STATE] rows from a run's captured stdout.
std::vector<StateFrame> parse_state_frames(const RunResult& run);

// First frame index playing `anim` strictly after `after` (0 = anywhere).
// Returns -1 if never seen.
int first_frame_with(const std::vector<StateFrame>& frames,
                     const std::string& anim, long long after = 0);

// Lines (stdout or log) containing `needle`.
std::vector<std::string> filter_lines(const std::vector<std::string>& lines,
                                      const std::string& needle);

}  // namespace e2e
