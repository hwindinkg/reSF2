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
//     `S.S()` (L945: `ta.ak(this.Tla)`).
//   - UI `rb` (L1277) — the JS UI-sound helpers, one id each:
//       `rb.um()`  -> `snd_click_1` (65535): EVERY `Bb` button press
//                     (`Bb.Xw` L1844 `a&&rb.um()`), settings rows
//                     (`un.rHa` L1930), pause dialog (`Dr.aa` L2067), pause
//                     HUD disc (`Aia` L2018), disciple toggle (`Nfb` L1981),
//                     nav `Le` buttons (L1978-1982).
//       `rb.iJa()` -> `snd_click_2` (65570): tab/cell strip selection
//                     (`Eg.pa` L1853, inherited by the profile `cs` L2189 and
//                     the shop `ss` strip), map node select (`qe.mK` L2139).
//       `rb.PS()`  -> `snd_focus_1` (65579): scroll-header toggle (`gk.Bgb`
//                     L2000), scroll-arrow `je` (L2145), icon cell `Ed.JE`
//                     (L2204), `Chb` (L1980).
//       `rb.U3()`  -> `snd_buy` (65569), `rb.QS()` -> `snd_upgrade` (65696),
//                     `rb.Xkb()` -> `snd_learn` (65598),
//                     `rb.Wkb()` -> `snd_gong` (65591).
//     BACKGROUND/EMPTY-SPACE TAPS HAVE NO JS SOUND TRIGGER — a tap that is not
//     inside one of the widgets above must play NOTHING.
//   - Music `ta.u0()` (L1275-1276): `menu = 1318`, `act = 1353`,
//     `fightN_* = 1319-1352` — files live in `reference/www/res/audio/`
//     (ogg/m4a pairs + `sounds_a`/`sounds_b` bundles). Music is NOT wired
//     here: this engine plays wav stems through miniaudio and has no music
//     backend — streaming the ogg/m4a tracks is a follow-up.
//
// Disk truth (read-only inventory, 2026-09-04): `assets/sounds/` holds 166
// wav stems; every `files[]` entry below was verified present on disk. The
// JS `snd_` prefix is stripped for the stem (`snd_hit1` -> `hit1.wav`).
// `snd_click_1`/`snd_click_2`/`snd_focus_1` (the three UI ticks) shipped ONLY
// inside the web audio bank `reference/www/res/audio/sounds_a.ogg` — the APK
// wav set never carried them. `click_1.wav` here is that bank's slot 0,
// extracted 1:1: `Ss.kWa` (L1237018) assigns each bank sub-sound
// `id = GL_index + 65535`, and `ta.WBa` (L1266) pins `snd_click_1 = 65535`,
// i.e. GL index 0 == the bank's first slot, which the decoded bank confirms
// (23 bursts == the 23 ids <= 65557, grouped 1/6/6/3/7 exactly like
// click_1 | hit1-6 | m_pl_attack1-6 | m_pl_jump1-3 | swish1-7). The previous
// `snd_click_* -> buy.wav` alias was WRONG: `buy` is `snd_buy` (65569), the
// PURCHASE sound (`rb.U3`), not the button tick.

#include <cstddef>

namespace sf2::audio {

// Tiny `strcmp`-free string equality (keeps this header dependency-free).
constexpr bool eq(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

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
constexpr const char* kClick[] = {"click_1"};
constexpr const char* kBuy[] = {"buy"};
constexpr const char* kLearn[] = {"learn"};
constexpr const char* kUpgrade[] = {"upgrade"};
constexpr const char* kWin[] = {"win"};
constexpr const char* kGong[] = {"gong"};

}  // namespace sfx_detail

// The full event table (order is stable — AudioEngine sizes its per-event
// counters/voices from this; the first three rows preserve the legacy
// hit/jump/step pools 1:1, and the `snd_*` rows are the JS `rb` UI ids).
inline const SfxGroup* sfx_groups(std::size_t& count) {
    static constexpr SfxGroup kGroups[] = {
        {"hit", sfx_detail::kHit, 6, 0.85f, 4},
        {"jump", sfx_detail::kJump, 6, 0.80f, 2},
        {"step", sfx_detail::kStep, 4, 0.45f, 2},
        // The JS `rb` UI ids (L1277). `snd_click_1` is the only one with its
        // own sample; `snd_click_2`/`snd_focus_1` are the same bank's sibling
        // ticks (ids 65570/65579, slots 12/21 of `sounds_b.ogg`, whose slot
        // boundaries are not recoverable from the shipped assets) and
        // therefore reuse the proven `click_1` sample rather than the
        // PURCHASE sound.
        {"snd_click_1", sfx_detail::kClick, 1, 0.55f, 2},
        {"snd_click_2", sfx_detail::kClick, 1, 0.55f, 2},
        {"snd_focus_1", sfx_detail::kClick, 1, 0.55f, 2},
        {"snd_buy", sfx_detail::kBuy, 1, 0.55f, 2},
        {"snd_upgrade", sfx_detail::kUpgrade, 1, 0.55f, 1},
        {"snd_learn", sfx_detail::kLearn, 1, 0.55f, 1},
        {"snd_gong", sfx_detail::kGong, 1, 0.80f, 1},
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
// `snd_focus_1`'s bank-only tick). Callers strip nothing — pass the full JS
// name.
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
    // The JS UI ids with no standalone wav: all three are the web bank's UI
    // tick and resolve to `click_1` (see the module comment). NOTE: `snd_buy`
    // (65569) is a DIFFERENT id and maps to `buy` above — never alias these
    // to it (that alias was the "screen click plays the purchase sound" bug).
    if (eq(js_name, "snd_click_1") || eq(js_name, "snd_click_2") ||
        eq(js_name, "snd_focus_1")) {
        return "click_1";
    }
    return nullptr;
}

}  // namespace sf2::audio
