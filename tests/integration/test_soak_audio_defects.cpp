// tests/integration/test_soak_audio_defects.cpp
//
// Soak-fix Wave 6 (reverse/analysis/SOAK_TRIAGE.md §6 Audio): behavioral
// tests for S1-S3, written from the player's perspective — each asserts
// what the player HEARS (which sound the engine actually plays), not what
// the internals trace.
//
//   S3: hit sounds must resolve. The soak showed "[audio] Sound not found
//       or invalid: f_pl_hit1/2/3" — Game::load_sounds() shipped a
//       hardcoded 12-name list that omitted every hit sound, even though
//       assets/sounds/ contains the full original bank (f_pl_hit1-3.wav and
//       m_pl_hit1-4.wav are on disk and in the APK manifest). After a
//       registered hit the played sound must resolve (no "not found").
//   S1: player attack voices must follow <Warrior Voice=> in
//       usersDefault.xml / user.xml ("Male" in both shipped saves) — a male
//       player must hear m_pl_attack*, a female one f_pl_attack*. The game
//       hardcodes the female f_pl_attack* at every player sound site.
//   S2: enemy attack voices must follow the stages.xml warrior template
//       Voice (Dojo_Disciple→Male, Girl_Sai→Female). The enemy currently
//       plays f_pl_attack2 — the female player set — regardless of the
//       fight.
//
// RED on HEAD (2026-08-02): S3 f_pl_hit*/m_pl_hit* are not in the loaded
// bank (get_sound == nullptr) and the bag impact request fails to resolve;
// S1 the default (Male) player attack plays f_pl_attack*; S2 the enemy
// voice is never resolved from stages.xml (Male for every fight) and the
// male enemy (Dojo_Disciple) attack plays f_pl_attack2.
// Fixes are implemented test-first: no fix before these RED tests.

#include "../headless_test_runner.hpp"
#include "../engine/audio/audio.hpp"
#include "../engine/game/save.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL [line %d]: %s\n", __LINE__, msg); ++tests_failed; } \
    else { std::printf("  PASS: %s\n", msg); ++tests_passed; } \
} while (0)

// Suppress noisy stdout from the game's internal logging so the test
// doesn't time out from I/O overhead. FAIL diagnostics go to stderr.
static void suppress_stdout() {
#ifdef _WIN32
    std::freopen("NUL", "w", stdout);
#else
    std::freopen("/dev/null", "w", stdout);
#endif
}

namespace plat = resf2::platform;
namespace scn = resf2::scene;
namespace aud = resf2::audio;

static resf2::test::HeadlessTestRunner make_dojo_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;  // no save load, no tutorial dialogue
    return resf2::test::HeadlessTestRunner(config);
}

static resf2::test::HeadlessTestRunner make_battle_runner() {
    resf2::test::HeadlessTestConfig config;
    config.asset_root = "assets";
    config.width = 320;
    config.height = 180;
    config.fixed_dt_ms = 16;
    config.hermetic = true;
    config.start_scene = "battle";
    return resf2::test::HeadlessTestRunner(config);
}

// ---------- deterministic key driving (same law as the wave-3/4/5 tests) ----------

static void frame(resf2::test::HeadlessTestRunner& r) {
    r.game().on_update(r.platform(), 16);
    r.game().on_render(r.platform());
    r.platform().advance_time_ms(16);
}

static void edge_down(resf2::test::HeadlessTestRunner& r, plat::Key k) {
    r.platform().poll_events();
    r.platform().inject_key_down(k);
    frame(r);
}

static void edge_up(resf2::test::HeadlessTestRunner& r, plat::Key k) {
    r.platform().poll_events();
    r.platform().inject_key_up(k);
    frame(r);
}

// Every scenario starts with the battle intro: the start-stance animation
// must run to completion before input is accepted.
static void warm_up(resf2::test::HeadlessTestRunner& r) {
    r.run_frames(330);              // intro stance animation runs to completion
    r.tap_key(plat::Key::D, 2);     // first input ends the A6 hold
    for (int i = 0; i < 80; ++i) {  // settle into stance_idle
        r.run_frames(1);
        if (!r.game().host_get_start_stance() &&
            r.game().host_get_player_move_state() == 0 &&
            r.game().host_get_player_anim() == "stance_idle")
            break;
    }
    r.run_frames(10);
}

static void step_tap(resf2::test::HeadlessTestRunner& r, plat::Key k) {
    edge_down(r, k);
    r.run_frames(2);
    edge_up(r, k);
    for (int i = 0; i < 70; ++i) {
        r.run_frames(1);
        if (r.game().host_get_player_move_state() == 0 &&
            r.game().host_get_player_anim() == "stance_idle")
            break;
    }
}

// Step toward the bag until within ~120 world units of it.
static void walk_to_bag(resf2::test::HeadlessTestRunner& r) {
    for (int i = 0; i < 8; ++i) {
        const float bag_x = r.game().host_get_enemy_pos_x();
        const float px = r.game().host_get_player_pos_x();
        const float dist = std::fabs(bag_x - px);
        if (dist < 120.0f) {
            std::fprintf(stderr, "  [walk] reached dist=%.0f after %d step(s)\n",
                         dist, i);
            return;
        }
        const bool bag_right = bag_x > px;
        step_tap(r, bag_right ? plat::Key::D : plat::Key::A);
    }
}

// One kick (P): fires a kick move, plays through the attack interval, recovers.
static void kick(resf2::test::HeadlessTestRunner& r) {
    edge_down(r, plat::Key::P);
    r.run_frames(2);
    edge_up(r, plat::Key::P);
    r.run_frames(60);
}

// ---------- S3: hit sounds resolve and play on a registered hit ----------

static void test_s3_hit_sounds_resolve() {
    std::printf("\n=== S3: hit sounds resolve after game load ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: S3 init() returned false\n"); ++tests_failed; return; }
    suppress_stdout();

    // The whole original bank ships in assets/sounds/ — including both hit
    // sets. The soak's "Sound not found or invalid: f_pl_hit1/2/3" must be
    // impossible: every hit sound the game can request must be loaded.
    const char* hit_sounds[] = {
        "f_pl_hit1", "f_pl_hit2", "f_pl_hit3",
        "m_pl_hit1", "m_pl_hit2", "m_pl_hit3", "m_pl_hit4",
    };
    bool all_resolve = true;
    for (const char* name : hit_sounds) {
        const bool ok = aud::AudioEngine::instance().get_sound(name) != nullptr;
        std::fprintf(stderr, "  [S3] %s %s\n", name, ok ? "loaded" : "MISSING");
        if (!ok) all_resolve = false;
    }
    CHECK(all_resolve, "S3: f_pl_hit1-3 and m_pl_hit1-4 resolve (no 'Sound not found')");

    // Behavioral half: a registered hit on the punching bag must PLAY a
    // resolving hit sound — the last played sound is the body impact.
    warm_up(runner);
    walk_to_bag(runner);
    const int hits0 = runner.game().host_get_player_hits_landed();
    int kicks = 0;
    while (kicks < 4 && runner.game().host_get_player_hits_landed() == hits0) {
        kick(runner);
        ++kicks;
    }
    const int hits1 = runner.game().host_get_player_hits_landed();
    std::fprintf(stderr, "  [S3] bag hits: %d -> %d after %d kick(s)\n", hits0, hits1, kicks);
    CHECK(hits1 > hits0, "S3: a bag kick registers a hit");

    const std::string& last = aud::AudioEngine::instance().last_played_name();
    std::fprintf(stderr, "  [S3] last played sound: '%s'\n", last.c_str());
    const bool is_hit = last.size() > 6 && last.compare(last.size() - 6, 6, "_pl_hit") == 0;
    CHECK(is_hit, "S3: the registered hit plays a _pl_hit body-impact sound");
    CHECK(is_hit && aud::AudioEngine::instance().get_sound(last) != nullptr,
          "S3: the played hit sound resolves (non-zero play, no 'not found')");
}

// ---------- S1: player attack voice follows the configured gender ----------

static void test_s1_player_attack_voice() {
    std::printf("\n=== S1: player attack voice follows <Warrior Voice=> ===\n");
    resf2::test::HeadlessTestRunner runner = make_dojo_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: S1 init() returned false\n"); ++tests_failed; return; }
    suppress_stdout();
    warm_up(runner);

    // Default voice (usersDefault.xml/user.xml say "Male"): the attack swing
    // must come from the male set. Snapshot the engine's last-played name
    // first — the engine is a process-wide singleton (earlier tests).
    const std::string before_male = aud::AudioEngine::instance().last_played_name();
    std::fprintf(stderr, "  [S1] default player voice: '%s'\n",
                 runner.game().host_get_player_voice().c_str());
    kick(runner);
    const std::string& male = aud::AudioEngine::instance().last_played_name();
    std::fprintf(stderr, "  [S1] male attack played: '%s' (before='%s')\n",
                 male.c_str(), before_male.c_str());
    CHECK(male != before_male && male.rfind("m_pl_attack", 0) == 0,
          "S1: a male player (user.xml Voice=Male) attack plays m_pl_attack*");
    CHECK(male != before_male && aud::AudioEngine::instance().get_sound(male) != nullptr,
          "S1: the male attack sound resolves");

    // Female voice: the same attack must come from the female set. (The
    // male kick above already played f_pl_attack1, so the name alone — not
    // a "did anything new play" delta — is the discriminator here.)
    runner.game().host_set_player_voice("Female");
    kick(runner);
    const std::string& female = aud::AudioEngine::instance().last_played_name();
    std::fprintf(stderr, "  [S1] female attack played: '%s'\n", female.c_str());
    CHECK(female.rfind("f_pl_attack", 0) == 0,
          "S1: a female player attack plays f_pl_attack*");
    CHECK(female.rfind("f_pl_attack", 0) == 0 &&
          aud::AudioEngine::instance().get_sound(female) != nullptr,
          "S1: the female attack sound resolves");
}

// ---------- S1 (config half): the save parser must read Voice ----------

static void test_s1_save_voice_parsed() {
    std::printf("\n=== S1: user.xml Voice= is parsed into the save ===\n");
    // A synthetic user.xml with a female voice: the parser must pick it up
    // (currently write_xml hardcodes Voice=\"Male\" and parse_xml ignores
    // the attribute entirely).
    const auto tmp = std::filesystem::temp_directory_path() / "resf2_test_user_voice.xml";
    {
        std::ofstream f(tmp);
        f << "<?xml version=\"1.0\"?>\n"
          << "<CurrentUser ID=\"1\" />\n"
          << "<Warriors>\n"
          << "  <Warrior ID=\"1\" FirstName=\"NAME_SHADOW\" Avatar=\"avatar_hero\"\n"
          << "    Voice=\"Female\" Money=\"50\" Level=\"1\" Tutorial=\"COMPLETE\">\n"
          << "  </Warrior>\n"
          << "</Warriors>\n";
    }
    resf2::save::SaveManager mgr;
    resf2::save::SaveData data;
    const bool ok = mgr.load(tmp.string(), data);
    std::filesystem::remove(tmp);
    CHECK(ok, "S1: the synthetic user.xml loads");
    std::fprintf(stderr, "  [S1] parsed voice: '%s'\n", data.voice.c_str());
    CHECK(data.voice == "Female",
          "S1: <Warrior Voice=\"Female\"> round-trips through the XML parser");
}

// ---------- S2: enemy voice comes from the stages.xml warrior template ----------

static void test_s2_enemy_voice_from_stages() {
    std::printf("\n=== S2: enemy voice resolves from stages.xml ===\n");
    resf2::test::HeadlessTestRunner runner = make_battle_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: S2 init() returned false\n"); ++tests_failed; return; }
    suppress_stdout();

    // Dojo_Disciple is Voice=\"Male\" in stages.xml, Girl_Sai Voice=\"Female\".
    scn::SceneHost::BattleInfo info;
    info.enemy_name = "Dojo_Disciple";
    runner.game().host_set_battle_info(info);
    std::fprintf(stderr, "  [S2] Dojo_Disciple -> '%s'\n",
                 runner.game().host_get_enemy_voice().c_str());
    CHECK(runner.game().host_get_enemy_voice() == "Male",
          "S2: Dojo_Disciple (Kenji) resolves to a Male voice from stages.xml");

    info.enemy_name = "Girl_Sai";
    runner.game().host_set_battle_info(info);
    std::fprintf(stderr, "  [S2] Girl_Sai -> '%s'\n",
                 runner.game().host_get_enemy_voice().c_str());
    CHECK(runner.game().host_get_enemy_voice() == "Female",
          "S2: Girl_Sai resolves to a Female voice from stages.xml");
}

static void test_s2_enemy_attack_voice() {
    std::printf("\n=== S2: a male enemy attack plays m_pl_attack* ===\n");
    resf2::test::HeadlessTestRunner runner = make_battle_runner();
    if (!runner.init()) { std::fprintf(stderr, "FAIL: S2 init() returned false\n"); ++tests_failed; return; }
    suppress_stdout();

    scn::SceneHost::BattleInfo info;
    info.enemy_name = "Dojo_Disciple";  // stages.xml Voice=\"Male\"
    runner.game().host_set_battle_info(info);

    // The audio engine is a process-wide singleton, so last_played_name()
    // carries state from earlier tests. Only consider a sound played AFTER
    // this point a product of this scenario.
    const std::string before = aud::AudioEngine::instance().last_played_name();

    // The A6 intro hold (start stance) ends on the player's first input;
    // the enemy AI is gated until then (A1). Break the stance like a real
    // player would, then wait for the enemy's first attack — when the
    // attack state flips, its swing sound was played this very frame.
    runner.run_frames(330);
    runner.tap_key(plat::Key::D, 2);
    for (int i = 0; i < 80; ++i) {  // settle into stance_idle
        runner.run_frames(1);
        if (!runner.game().host_get_start_stance() &&
            runner.game().host_get_player_move_state() == 0)
            break;
    }

    bool attacked = false;
    int first_attack_frame = -1;
    for (int i = 0; i < 3600 && !attacked; ++i) {
        runner.run_frames(1);
        attacked = runner.game().host_get_enemy_attacking();
        if (attacked) first_attack_frame = i;
    }
    std::fprintf(stderr, "  [S2] enemy attack at frame %d (anim='%s' pick='%s')\n",
                 first_attack_frame,
                 runner.game().host_get_enemy_anim().c_str(),
                 runner.game().host_get_ai_last_pick().c_str());
    CHECK(attacked, "S2: the enemy launches an attack within the window");

    const std::string& last = aud::AudioEngine::instance().last_played_name();
    std::fprintf(stderr, "  [S2] enemy attack played: '%s' (enemy voice '%s', before='%s')\n",
                 last.c_str(), runner.game().host_get_enemy_voice().c_str(),
                 before.c_str());
    CHECK(attacked && last != before && last.rfind("m_pl_attack", 0) == 0,
          "S2: the Male enemy's attack plays m_pl_attack* (stages.xml voice)");
    CHECK(attacked && last != before &&
          aud::AudioEngine::instance().get_sound(last) != nullptr,
          "S2: the enemy attack sound resolves");
}

int main() {
    std::printf("=== Soak Audio Defects Test (S1-S3) ===\n");
    std::fflush(stdout);

    test_s3_hit_sounds_resolve();
    test_s1_player_attack_voice();
    test_s1_save_voice_parsed();
    test_s2_enemy_voice_from_stages();
    test_s2_enemy_attack_voice();

    std::printf("\n%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
