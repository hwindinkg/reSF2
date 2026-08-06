// tests/e2e/test_e2e_dialogue_background.cpp
//
// Wave 10A defect 4 — DIALOGUE DIM (D1 regression): with the dialogue open,
// the background was fully darkened again. The Dialogue scene is a separate
// screen (the scene manager renders only the active scene), so opening a
// dialog replaced the painted location with the renderer's flat clear
// colour — the dojo's background vanished behind the parchment.
//
// E2E on the REAL binary: boot into the Sensei tutorial flow (same script as
// the fight-timer test) and read actual rendered pixels through the [PIXEL]
// probe: p00 = top-left corner, p11 = screen centre (the dialogue parchment
// paint ~226,205,163 marks dialogue frames). Assert: whenever the dialogue
// is up, the corner pixel equals the location's corner pixel of the frames
// around it (the dojo renders behind the parchment). RED on HEAD: the
// dialogue corner is the flat clear colour (40,20,9) while the dojo corner
// is the painted background (27,17,10).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <string>
#include <vector>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;

namespace {

struct Pixel {
    long long frame = 0;
    int r = 0, g = 0, b = 0;
};

// Parse "[PIXEL] f=N pXX=r,g,b" rows. Returns the points matching `which`.
std::vector<Pixel> pixels_of(const std::vector<std::string>& lines,
                             const std::string& which) {
    std::vector<Pixel> out;
    static const std::regex re(R"(\[PIXEL\] f=(\d+) p(\d+)=(\d+),(\d+),(\d+))");
    for (const auto& l : lines) {
        std::smatch m;
        if (!std::regex_search(l, m, re)) continue;
        if (m[2] != which) continue;
        Pixel p;
        p.frame = std::stoll(m[1]);
        p.r = std::stoi(m[3]); p.g = std::stoi(m[4]); p.b = std::stoi(m[5]);
        out.push_back(p);
    }
    return out;
}

int diff_sum(const Pixel& a, const Pixel& b) {
    return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: test_e2e_dialogue_background <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // Same script as the fight-timer test, cut short after the training
    // dialog opens: intro dialog (f2..4), 4 hint steps, punchbag dialog
    // (f560), 3 bag punches (f700..760 -> FIRST_FIGHT), training dialog
    // (~f762). The run ends mid-dialog.
    std::vector<e2e::InputEvent> events;
    for (int f = 2; f <= 4; ++f) {
        events.push_back({f, true, "Space"});
        events.push_back({f + 1, false, "Space"});
    }
    for (int f : {170, 290, 410, 530}) {
        events.push_back({f, true, "D"});
        events.push_back({f + 8, false, "D"});
    }
    events.push_back({560, true, "Space"});
    events.push_back({561, false, "Space"});
    for (int f : {700, 730, 760}) {
        events.push_back({f, true, "O"});
        events.push_back({f + 2, false, "O"});
    }

    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_dialogue_bg_input.txt";
    spec.out_name = "e2e_dialogue_bg";
    spec.max_frames = 800;
    spec.extra_args = {"--scene", "battle", "--tutorial-start"};
    spec.no_log = true;       // stdout [PIXEL]/[STATE] are the probes
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return 1;
    }

    const e2e::RunResult run = e2e::run_app(spec);
    check(run.exit_code == 0, "resf2_app exited cleanly");

    const auto frames = e2e::parse_state_frames(run);
    check(!frames.empty(), "the run produced [STATE] rows");
    if (frames.empty()) return resf2::test::summary();

    const auto corner = pixels_of(run.stdout_lines, "00");
    const auto centre = pixels_of(run.stdout_lines, "11");
    std::printf("dialogue-bg: %zu corner rows, %zu centre rows\n",
                corner.size(), centre.size());
    check(corner.size() > 100 && centre.size() > 100,
          "the [PIXEL] probe produced a usable trace");

    // A frame is a DIALOGUE frame when any of its centre (p11) rows carries
    // the parchment paint (~226,205,163). Note: several [PIXEL] rows can
    // share one frame number — the gameplay counter freezes while the
    // dialogue is up — so a frame is dialogue iff ANY of its rows matches.
    auto is_dialogue = [&](long long f) {
        for (const auto& ct : centre) {
            if (ct.frame != f) continue;
            if (diff_sum(ct, Pixel{0, 226, 205, 163}) <= 24) return true;
        }
        return false;
    };

    // Reference corner per dialogue frame: the corner pixel of the nearest
    // NON-dialogue frame BEFORE it (the last real scene render). The
    // dialogue must not darken what was on screen a moment earlier.
    // RED on HEAD: the punchbag/training dialogues replace the dojo's
    // painted corner (27,17,10) with the flat clear colour (40,20,9).
    int dialogue_frames = 0, bad = 0;
    for (size_t i = 0; i < corner.size(); ++i) {
        const Pixel& c = corner[i];
        if (!is_dialogue(c.frame)) continue;
        ++dialogue_frames;
        // Nearest preceding non-dialogue corner row (same frame ok if the
        // frame also has non-dialogue rows — the last dojo render shares
        // the frozen frame number with the first dialogue render).
        const Pixel* ref = nullptr;
        for (size_t j = i; j > 0; --j) {
            if (!is_dialogue(corner[j - 1].frame)) { ref = &corner[j - 1]; break; }
        }
        if (!ref) continue;   // no preceding scene (boot) — nothing to compare
        if (diff_sum(c, *ref) > 12) ++bad;
    }
    std::printf("dialogue-bg: %d dialogue frames, %d with a darkened corner\n",
                dialogue_frames, bad);
    check(dialogue_frames > 20, "the dialogue was open for a measurable span");
    check(bad == 0,
          "the background corner stays lit while the dialogue is open "
          "(location rendered behind the parchment)");

    return resf2::test::summary();
}
