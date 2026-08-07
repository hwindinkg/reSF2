// tests/e2e/test_e2e_battle_music.cpp
//
// Wave 11C P2 — BATTLE MUSIC: "нет музыки локации" — the battle track must
// be data-driven from the LOCATION's params.xml <Root Music="id|id"> list
// (numeric IDs, random pick), resolved through the music registry to
// assets/music/fight<ID>_*.mp3 — NOT the hardcoded fight1_samurai_spirit
// the port used to play everywhere.
//
// Verified (VERIFY_W11.md Q2 GREEN after the attribution correction;
// SPEC_PRESENTATION.md Q2):
//   - Location::parse FUN_8F43C6F8 (attr read @ 0x8F43CB54) appends the
//     Music IDs to Location+0x18;
//   - the fight-screen play site (ScreenFight ctor 0x8F426524) random-picks
//     one ID from that list (FUN_8F43BC98, its ONLY caller) and plays
//     assets/music/<name>.mp3 looped (FUN_8F282EF8; registry FUN_8F64B174);
//   - stages.xml <Battle Music> (battle+0x18, Battle::parse FUN_8F2C2E84)
//     is a SEPARATE path — NOT the fight-screen track.
//   - the registry files ARE the shipped assets/music/fight<ID>_*.mp3
//     (the only IDs used by the shipped params.xml files are 6 and 7 ->
//     fight6_sparring / fight7_fat_boss).
//
// E2E on the REAL binary, three runs:
//   (a) --scene battle --location dojo: the [MUSIC] probe picks a random ID
//       from the dojo's OWN Music list and resolves it to fight<ID>_*.mp3 —
//       never fight1_samurai_spirit;
//   (b) --scene battle --location arena: the probe is location-driven (the
//       arena's list feeds its own pick);
//   (c) a fixture location with an unresolvable Music ID (no fight999_*.mp3
//       on disk) — graceful fallback, clear [MUSIC] log, exit code 0.
// RED on HEAD: no [MUSIC] rows at all; the audio line always reads
// fight1_samurai_spirit.mp3.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "../check.hpp"
#include "e2e_runner.hpp"

using resf2::test::check;

namespace {

bool has_needle(const std::string& line, const char* needle) {
    return line.find(needle) != std::string::npos;
}

// Parse the <Root Music="id|id"> attr of a location's params.xml exactly the
// way Location::parse (FUN_8F43C6F8 @ 0x8F43CB54) does.
std::vector<std::string> music_ids_of(const std::string& root,
                                      const std::string& location) {
    std::vector<std::string> out;
    std::ifstream f(root + "/assets/locations/" + location + "/params.xml");
    if (!f) return out;
    std::string xml((std::istreambuf_iterator<char>(f)),
                    std::istreambuf_iterator<char>());
    std::smatch m;
    static const std::regex re(R"re(<Root[^>]*Music="([^"]+)")re");
    if (std::regex_search(xml, m, re)) {
        std::string list = m[1];
        size_t start = 0;
        while (start <= list.size()) {
            const size_t sep = list.find('|', start);
            const std::string tok = list.substr(
                start, sep == std::string::npos ? std::string::npos
                                                : sep - start);
            if (!tok.empty()) out.push_back(tok);
            if (sep == std::string::npos) break;
            start = sep + 1;
        }
    }
    return out;
}

// Run one battle and return the [MUSIC] probe lines.
std::vector<std::string> run_battle_music(const std::string& app,
                                          const std::string& root,
                                          const std::string& location,
                                          const std::string& out_name) {
    e2e::RunSpec spec;
    spec.app = app;
    spec.root = root;
    spec.script = root + "/build/e2e_music_" + out_name + "_input.txt";
    spec.out_name = "e2e_music_" + out_name;
    spec.max_frames = 250;  // the track is picked at battle entry
    spec.no_log = true;     // stdout [MUSIC] probes
    spec.extra_args = {"--scene", "battle", "--round-time", "99",
                       "--location", location};
    // One scripted punch keeps the input script armed: an empty script is
    // not armed, and a non-armed run polls the real keyboard and PAUSES when
    // the window has no focus (every headless run would hang).
    std::vector<e2e::InputEvent> events;
    events.push_back({120, true, "O"});
    events.push_back({122, false, "O"});
    if (!e2e::write_input_script(spec.script, events)) {
        std::fprintf(stderr, "FAIL: could not write input script\n");
        return {};
    }
    const e2e::RunResult run = e2e::run_app(spec);
    std::printf("music: (%s) exit=%d\n", out_name.c_str(), run.exit_code);
    check(run.exit_code == 0, (out_name + ": resf2_app exited cleanly").c_str());
    return e2e::filter_lines(run.stdout_lines, "[MUSIC]");
}

// A picked id is a member of the location's Music list.
bool id_in_list(const std::string& id, const std::vector<std::string>& ids) {
    for (const auto& i : ids)
        if (i == id) return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: test_e2e_battle_music <resf2_app> <repo_root>\n");
        return 1;
    }
    const std::string app = argv[1];
    const std::string root = argv[2];

    // ----------------------------------------------------- (a) dojo battle
    const auto dojo_ids = music_ids_of(root, "dojo");
    std::printf("music: dojo params.xml Music list: %zu id(s)\n",
                dojo_ids.size());
    check(!dojo_ids.empty(),
          "the dojo's params.xml carries a <Root Music> list");
    const auto dojo_rows = run_battle_music(app, root, "dojo", "dojo");
    check(!dojo_rows.empty(),
          "(a) the battle start picked a track from the location music list "
          "([MUSIC] probe)");
    bool a_loc = false, a_pick = false, a_track = false, a_not_hardcoded = false;
    for (const auto& l : dojo_rows) {
        if (has_needle(l, "location='dojo'")) a_loc = true;
        std::smatch m;
        static const std::regex re_pick(R"(picked id='([^']+)')");
        static const std::regex re_track(R"(track='([^']+)')");
        if (std::regex_search(l, m, re_pick)) {
            if (id_in_list(m[1], dojo_ids)) a_pick = true;
        }
        if (std::regex_search(l, m, re_track)) {
            const std::string t = m[1];
            if (t.rfind("fight", 0) == 0 && t.ends_with(".mp3")) a_track = true;
            if (t != "fight1_samurai_spirit.mp3") a_not_hardcoded = true;
        }
    }
    check(a_loc, "(a) the probe names the battle location (location='dojo')");
    check(a_pick,
          "(a) the picked music ID belongs to the dojo's own <Root Music> "
          "list");
    check(a_track,
          "(a) the picked ID resolved to an assets/music/fight<N>_*.mp3 "
          "track");
    check(a_not_hardcoded,
          "(a) the battle track is NOT the hardcoded "
          "fight1_samurai_spirit.mp3");

    // ----------------------------------------------- (b) location-driven
    const auto arena_ids = music_ids_of(root, "arena");
    check(!arena_ids.empty(), "the arena's params.xml carries a Music list");
    const auto arena_rows = run_battle_music(app, root, "arena", "arena");
    check(!arena_rows.empty(),
          "(b) the arena battle picked its own track ([MUSIC] probe)");
    bool b_loc = false, b_pick = false;
    for (const auto& l : arena_rows) {
        if (has_needle(l, "location='arena'")) b_loc = true;
        std::smatch m;
        static const std::regex re_pick(R"(picked id='([^']+)')");
        if (std::regex_search(l, m, re_pick)) {
            if (id_in_list(m[1], arena_ids)) b_pick = true;
        }
    }
    check(b_loc, "(b) the probe names the arena battle location");
    check(b_pick,
          "(b) the arena's pick comes from the ARENA's own Music list "
          "(location-driven, not a global fallback)");

    // --------------------------------- (c) unresolvable ID -> fallback
    const std::string fx = root + "/assets/locations/e2e_music_void";
    const std::string fx_params = fx + "/params.xml";
    const bool fx_created = std::filesystem::create_directories(fx);
    {
        std::ofstream f(fx_params);
        f << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
             "<Root Music=\"999\" Color=\"0x000000\" Wall=\"200\" Floor=\"80\" "
             "Width=\"1936\" Height=\"512\">\n"
             "</Root>\n";
    }
    const auto void_rows = run_battle_music(app, root, "e2e_music_void",
                                            "void");
    // Clean up the fixture even when the run failed.
    std::error_code ec;
    std::filesystem::remove_all(fx, ec);
    std::printf("music: fixture '%s' removed (%d)\n", fx.c_str(), (int)!!ec);
    check(!void_rows.empty(),
          "(c) an unresolvable Music ID produced a clear [MUSIC] fallback "
          "log");
    bool c_log = false;
    for (const auto& l : void_rows) {
        if (has_needle(l, "location='e2e_music_void'") &&
            has_needle(l, "id '999'") &&
            has_needle(l, "no assets/music/fight999_*.mp3") &&
            has_needle(l, "graceful fallback")) c_log = true;
    }
    check(c_log,
          "(c) the fallback names the location, the unresolvable ID and the "
          "missing registry file");
    check(fx_created || !std::filesystem::exists(fx_params),
          "(c) fixture location cleaned up");
    (void)fx_created;

    return resf2::test::summary();
}
