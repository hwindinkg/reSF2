#pragma once

// Table-driven SFX mapping (Phase 7.1) — the native equivalent of the JS
// `ta.WBa()` name -> asset table (sf2.502f0946.js L1265-1274).
//
// JS cites:
//   - `ta.ak(name, volume)` (L1264): `a = ta.WBa(a); if (a != null)
//     L.K.$f.play(a, b)` — the single "play sound by name" point.
//   - `WBa()` (L1265-1274): `snd_hit1..6 = 65536-65541`,
//     `snd_super_hit1/2 = 65673/65674`, `snd_swish1..7 = 65551-65557`,
//     `snd_armor = 65558`, `snd_bodyfall1/3 = 65563/65564`,
//     `snd_f_pl_attack1..6 = 65581-65586`, `snd_f_pl_death = 65587`,
//     `snd_f_pl_jump1..3 = 65588-65590`, `snd_m_pl_attack1..6 = 65542-65547`,
//     `snd_m_pl_jump1..3 = 65548-65550`, `snd_gong = 65591`,
//     `snd_win = 65703`, `snd_shuriken_fly = 65667`,
//     `snd_smoke_bomb = 65670`, `snd_bow_fast/long = 65565/65566`,
//     `snd_titan_attack1..4 = 65680-65683`, `snd_titan_hit1..4 = 65685-65688`,
//     `snd_blizzard_1..3 = 65559-65561`, `snd_blizzard_hit = 65562`,
//     `snd_magic_*` (fireball/energyball/ice/water/wave/lightning/massbomb/
//     bomb/asteroid/firepillar/fire_splash/mind_throw/saw/acid_cloud/deathray),
//     `snd_wasp_fly_* = 65698-65700`, `snd_widow_teleport_* = 65701/65702`,
//     `snd_musket_shot_1/2 = 65654/65555`, `snd_roots_start/end = 65658/65659`,
//     `snd_click_1 = 65535`, `snd_click_2 = 65570`.
//   - Combat triggers: `wd.dwb(a)` (L519 weapon events ->
//     `ta.ak(a.name, ...)`), `fwb`/`ewb` (L519 start/stop), scenario action
//     `S.S()` (L945: `ta.ak(this.Tla)`), UI `rb` (L1277: `snd_upgrade`,
//     `snd_buy`, `snd_learn`, `snd_gong`, `snd_click_1/2`, `snd_focus_1`).
//   - Music `ta.u0()` (L1275-1276): `menu = 1318`, `act = 1353`,
//     `fightN_* = 1319-1352` — files live in `reference/www/res/audio/`
//     (ogg/m4a pairs + `sounds_a`/`sounds_b` bundles). Music is NOT wired
//     here: this engine plays wav stems through miniaudio and has no music
//     backend — streaming the ogg/m4a tracks is a follow-up.
//
// Disk truth (read-only inventory, 2026-09-04): `assets/sounds/` holds 166
// wav stems; every `files[]` entry below was verified present on disk. The
// JS `snd_` prefix is stripped for the stem (`snd_hit1` -> `hit1.wav`).
// Two JS names have NO wav on disk and keep the legacy fallback:
//   - `snd_click_1`/`snd_click_2` -> `buy.wav` (the closest UI tick;
//     pre-existing behavior, kept).
//   - `snd_focus_1`, scenario-only stingers, etc. are not mapped yet.

#include <cstddef>

namespace sf2::audio {

// One round-robin pool: play(event) walks `files` across `voices` copies so
// rapid re-triggers MIX instead of cutting each other off (see audio.cpp).
struct SfxGroup {
    const char* event;          // play("event")
    const char* const* files;   // wav stems under the sfx dir
    std::size_t count;          // stems in files[]
    float volume;               // event loudness
    int voices;                 // overlapping copies
};

namespace sfx_detail {

constexpr const char* kHit[] = {"hit1", "hit2", "hit3", "hit4", "hit5", "hit6"};
constexpr const char* kSuperHit[] = {"super_hit1", "super_hit2"};
constexpr const char* kSwish[] = {"swish1", "swish2", "swish3", "swish4",
                                  "swish5", "swish6", "swish7"};
constexpr const char* kArmor[] = {"armor"};
constexpr const char* kBodyfall[] = {"bodyfall1", "bodyfall3"};
constexpr const char* kAttack[] = {"f_pl_attack1", "f_pl_attack2", "f_pl_attack3",
                                   "f_pl_attack4", "f_pl_attack5", "f_pl_attack6",
                                   "m_pl_attack1", "m_pl_attack2", "m_pl_attack3",
                                   "m_pl_attack4", "m_pl_attack5", "m_pl_attack6"};
constexpr const char* kVoiceHit[] = {"f_pl_hit1", "f_pl_hit2", "f_pl_hit3",
                                     "m_pl_hit1", "m_pl_hit2", "m_pl_hit3",
                                     "m_pl_hit4"};
constexpr const char* kDeath[] = {"f_pl_death", "m_pl_death"};
constexpr const char* kJump[] = {"f_pl_jump1", "f_pl_jump2", "f_pl_jump3",
                                 "m_pl_jump1", "m_pl_jump2", "m_pl_jump3"};
constexpr const char* kStep[] = {"swish1", "swish2", "swish3", "swish4"};
constexpr const char* kMagic[] = {"magic_fireball_start", "magic_energyball_start",
                                  "magic_ice_ball_start", "magic_water_ball_start",
                                  "magic_wave_start", "magic_lightningarrow_start"};
constexpr const char* kMagicHit[] = {"blizzard_hit", "magic_mind_throw_hit",
                                     "titan_throw_hit"};
constexpr const char* kBlizzard[] = {"blizzard_1", "blizzard_2", "blizzard_3"};
constexpr const char* kBow[] = {"bow_fast", "bow_long"};
constexpr const char* kTitan[] = {"titan_attack1", "titan_attack2", "titan_attack3",
                                  "titan_attack4"};
constexpr const char* kShuriken[] = {"shuriken_fly", "throwing", "shopshuriken"};
constexpr const char* kSmoke[] = {"smoke_bomb"};
constexpr const char* kCoin[] = {"coin_hit1", "coin_hit2", "coin_hit3", "coin_hit4"};
constexpr const char* kBuy[] = {"buy"};
constexpr const char* kLearn[] = {"learn"};
constexpr const char* kUpgrade[] = {"upgrade"};
constexpr const char* kWin[] = {"win"};
constexpr const char* kGong[] = {"gong"};

}  // namespace sfx_detail

// The full event table (order is stable — AudioEngine sizes its per-event
// counters/voices from this; the first four rows preserve the legacy
// hit/jump/step/click behavior 1:1).
inline const SfxGroup* sfx_groups(std::size_t& count) {
    static constexpr SfxGroup kGroups[] = {
        {"hit", sfx_detail::kHit, 6, 0.85f, 4},
        {"jump", sfx_detail::kJump, 6, 0.80f, 2},
        {"step", sfx_detail::kStep, 4, 0.45f, 2},
        {"click", sfx_detail::kBuy, 1, 0.55f, 2},
        {"super_hit", sfx_detail::kSuperHit, 2, 0.90f, 2},
        {"swish", sfx_detail::kSwish, 7, 0.45f, 2},
        {"armor", sfx_detail::kArmor, 1, 0.70f, 1},
        {"bodyfall", sfx_detail::kBodyfall, 2, 0.70f, 1},
        {"attack", sfx_detail::kAttack, 12, 0.70f, 2},
        {"voice_hit", sfx_detail::kVoiceHit, 7, 0.70f, 2},
        {"death", sfx_detail::kDeath, 2, 0.80f, 1},
        {"magic", sfx_detail::kMagic, 6, 0.75f, 2},
        {"magic_hit", sfx_detail::kMagicHit, 3, 0.80f, 2},
        {"blizzard", sfx_detail::kBlizzard, 3, 0.70f, 1},
        {"bow", sfx_detail::kBow, 2, 0.70f, 1},
        {"titan", sfx_detail::kTitan, 4, 0.80f, 2},
        {"shuriken", sfx_detail::kShuriken, 3, 0.60f, 1},
        {"smoke", sfx_detail::kSmoke, 1, 0.70f, 1},
        {"coin", sfx_detail::kCoin, 4, 0.60f, 1},
        {"buy", sfx_detail::kBuy, 1, 0.55f, 2},
        {"learn", sfx_detail::kLearn, 1, 0.55f, 1},
        {"upgrade", sfx_detail::kUpgrade, 1, 0.55f, 1},
        {"win", sfx_detail::kWin, 1, 0.80f, 1},
        {"gong", sfx_detail::kGong, 1, 0.80f, 1},
    };
    count = sizeof(kGroups) / sizeof(kGroups[0]);
    return kGroups;
}

// The `ta.WBa()` equivalent: JS `snd_*` name -> wav stem on this table.
// Returns nullptr when the JS name has no mapped stem (e.g. music ids,
// `snd_focus_1`). Callers strip nothing — pass the full JS name.
inline const char* sfx_stem_for_js(const char* js_name) {
    if (js_name == nullptr || *js_name == '\0') return nullptr;
    std::size_t n = 0;
    const SfxGroup* groups = sfx_groups(n);
    // Match "snd_<stem>" against every pooled stem (one linear pass; the
    // table is tiny and this runs only on cache-miss paths).
    for (std::size_t g = 0; g < n; ++g) {
        for (std::size_t f = 0; f < groups[g].count; ++f) {
            const char* stem = groups[g].files[f];
            // Compare "snd_" + stem with js_name without strcmp (no <cstring>
            // needed — keeps this header dependency-free).
            const char* p = js_name;
            for (const char* q = "snd_"; *q != '\0'; ++q, ++p) {
                if (*p != *q) goto next_stem;
            }
            for (const char* q = stem;; ++q, ++p) {
                if (*q == '\0') {
                    if (*p == '\0') return stem;
                    goto next_stem;
                }
                if (*p != *q) goto next_stem;
            }
        next_stem:;
        }
    }
    // No wav on disk for these JS names — documented fallbacks.
    if (js_name[0] == 's' && js_name[1] == 'n' && js_name[2] == 'd' &&
        js_name[3] == '_' && js_name[4] == 'c' && js_name[5] == 'l') {
        // snd_click_1 / snd_click_2 -> buy.wav (pre-existing behavior).
        return "buy";
    }
    return nullptr;
}

}  // namespace sf2::audio
