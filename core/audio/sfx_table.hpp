#pragma once

// Table-driven SFX mapping — the native equivalent of the JS `ta.WBa()`
// name -> asset-id table (sf2.502f0946.js L1264-1274).
//
// JS cites:
//   - `ta.ak(name, looped)` (L1264): `a = ta.WBa(name); if (a != null)
//     L.K.$f.play(a, looped)` — the single "play sound by id" point. A name
//     ABSENT from `ta.WBa` plays NOTHING. The third argument callers pass
//     (`wd.dwb` L519 `ta.ak(a.name, a.ceb, a.volume)`) is ignored — `ak`
//     takes only (name, looped), so `<Sound Volume=..>` never reaches the
//     mixer (`fm.volume` L735 is parsed and unused).
//   - `ta.WBa()` (L1265-1274) builds ONE table: 154 `snd_*` names -> the
//     bundled asset ids (`snd_armor`=65558 ... `snd_win`=65703). The native
//     port plays wav stems, so each row also resolves to a file: the stem is
//     the name minus the `snd_` prefix (`snd_hit1` -> `hit1.wav`), which is
//     exactly how `assets/sounds/` was extracted.
//   - Combat triggers: `wd.dwb` (L519 Sound -> `ta.ak(a.name, a.ceb, ...)`),
//     `wd.fwb` (L519 RandomSound -> `ta.ak(a.ab())`), `wd.ewb` (L519
//     StopSound -> `ta.Jwb(a.name)`).
//   - UI ids L1277 (`rb`): `um()`/`iJa()`/`PS()`/`U3()`/`QS()`/`Xkb()`/
//     `Wkb()` -> `snd_click_1`/`snd_click_2`/`snd_focus_1`/`snd_buy`/
//     `snd_upgrade`/`snd_learn`/`snd_gong`.
//
// PRUNED (were invented rows with no JS id AND no JS trigger):
//   `hit`, `jump`, `step` (played by the removed fabricated triggers in
//   `fight.cpp`) and `voice_hit` (no caller at all). Their stems
//   `f_pl_hit1..3` / `m_pl_hit1,3,4` / `m_pl_death` / `coin_hit1..3` are
//   APK-only wavs with NO `ta.WBa` id — `snd_f_pl_hit*` and `snd_m_pl_hit1/3/4`
//   are absent from the JS table, and searches of moves.xml for those names
//   return 0 hits. They stay on disk (unreferenced) but resolve to nothing.
//   The real ids in that family — `snd_m_pl_hit2` (65657), `snd_f_pl_death`
//   (65587), `snd_f_cough` (65580), `snd_m_cough` (65656) — are all rows of
//   the table below, so any authored `<Sound>` naming them now plays.
//
// NAMES IN moves.xml THAT RESOLVE TO NOTHING (JS-identical silence — the
// shipped data references names `ta.WBa` does not carry, so the real game
// plays nothing either; reported, never faked):
//   - the PACK voices/impacts `<Sound PackName="CLANS|ZONE_*">`:
//     `snd_low_pl_attack1..6`, `snd_low_pl_jump1..3`, `snd_low_pl_hit2`,
//     `snd_low_cough`, `snd_midsphere_*`, `snd_bigsphere_end`,
//     `snd_smallsphere_*`, `snd_magic_ice_cloud`, `snd_gust_whoosh_*`,
//     `snd_arcane_attack`, `snd_rats_*`, `snd_hoaxen_cast`,
//     `snd_hoaxen_tentacle_hit1..3`, `snd_perk_hunger_claws`,
//     `snd_magic_dragon`, `snd_blade_fury`, `snd_rayshot*`,
//     `snd_electric_hit`, `snd_magic_nrtyu_scythe_1`, `snd_bone_boss_soul`,
//     `snd_energy_burst`, `snd_saturn_blaster_shot`, `snd_knife_reveal`,
//     `snd_knife_stroke`, `snd_electric_release`, `snd_shadow_grasp`,
//     `snd_time_shift`, `snd_ability_root_start`, `snd_cleric_bottle`.
//     `fm.parse` (L735) reads `PackName` into `ES` and `ta.ak` NEVER consults
//     it, so pack sounds are silent through this path.
//   - the case typo `snd_Roots_start` / `snd_Roots_end`: `ta.WBa` registers
//     `snd_roots_start`/`snd_roots_end` (65559/65658 are `blizzard_*`/... —
//     the ids ARE lowercase) while moves.xml spells them with a capital R,
//     so `WBa("snd_Roots_start")` returns null. The stems on disk kept the
//     capital (`Roots_start.wav`) — reproduced 1:1 below.
//
// `volume`/`voices` are NATIVE MIXING choices (the JS `ta.ak` has neither):
// `volume` is the per-event gain and `voices` the number of overlapping
// `ma_sound` copies so rapid re-triggers mix instead of cutting each other
// off. They never affect which id resolves.

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

// One JS sound id (`ta.WBa` L1265-1274).
struct SfxGroup {
    const char* event;  // the JS `snd_*` id name (`ta.WBa` key) — play(name)
    float volume;       // native mixing gain (JS carries none)
    int voices;         // overlapping copies (native mixing)
};

// The full `ta.WBa` id table (154 rows, L1265-1274), in the JS source order.
inline const SfxGroup* sfx_groups(std::size_t& count) {
    static constexpr SfxGroup kGroups[] = {
        {"snd_armor", 0.70f, 1}, {"snd_bodyfall1", 0.70f, 1}, {"snd_bodyfall3", 0.70f, 1}, {"snd_buy", 0.55f, 2},
        {"snd_click_1", 0.55f, 2}, {"snd_click_2", 0.55f, 2}, {"snd_coin_hit4", 0.60f, 1}, {"snd_disk", 0.60f, 1},
        {"snd_f_pl_attack1", 0.70f, 2}, {"snd_f_pl_attack2", 0.70f, 2}, {"snd_f_pl_attack3", 0.70f, 2}, {"snd_f_pl_attack4", 0.70f, 2},
        {"snd_f_pl_attack5", 0.70f, 2}, {"snd_f_pl_attack6", 0.70f, 2}, {"snd_f_pl_death", 0.80f, 1}, {"snd_f_pl_jump1", 0.70f, 2},
        {"snd_f_pl_jump2", 0.70f, 2}, {"snd_f_pl_jump3", 0.70f, 2}, {"snd_focus_1", 0.55f, 2}, {"snd_gong", 0.80f, 1},
        {"snd_hit1", 0.85f, 4}, {"snd_hit2", 0.85f, 4}, {"snd_hit3", 0.85f, 4}, {"snd_hit4", 0.85f, 4},
        {"snd_hit5", 0.85f, 4}, {"snd_hit6", 0.85f, 4}, {"snd_knife", 0.60f, 1}, {"snd_learn", 0.55f, 2},
        {"snd_m_pl_attack1", 0.70f, 2}, {"snd_m_pl_attack2", 0.70f, 2}, {"snd_m_pl_attack3", 0.70f, 2}, {"snd_m_pl_attack4", 0.70f, 2},
        {"snd_m_pl_attack5", 0.70f, 2}, {"snd_m_pl_attack6", 0.70f, 2}, {"snd_m_pl_jump1", 0.70f, 2}, {"snd_m_pl_jump2", 0.70f, 2},
        {"snd_m_pl_jump3", 0.70f, 2}, {"snd_shopshuriken", 0.60f, 1}, {"snd_shopshurikencatch", 0.60f, 1}, {"snd_shuriken_fly", 0.60f, 1},
        {"snd_smoke_bomb", 0.70f, 1}, {"snd_super_hit1", 0.90f, 2}, {"snd_swish_sword1", 0.45f, 2}, {"snd_swish_sword2", 0.45f, 2},
        {"snd_swish_sword3", 0.45f, 2}, {"snd_swish1", 0.45f, 2}, {"snd_swish2", 0.45f, 2}, {"snd_swish3", 0.45f, 2},
        {"snd_swish4", 0.45f, 2}, {"snd_swish5", 0.45f, 2}, {"snd_swish6", 0.45f, 2}, {"snd_swish7", 0.45f, 2},
        {"snd_throwing", 0.60f, 1}, {"snd_upgrade", 0.55f, 2}, {"snd_wall3", 0.70f, 2}, {"snd_win", 0.80f, 1},
        {"snd_blizzard_hit", 0.75f, 2}, {"snd_magic_bomb_end", 0.75f, 2}, {"snd_magic_acid_cloud", 0.75f, 2}, {"snd_magic_asteroid_end", 0.75f, 2},
        {"snd_magic_asteroid_start", 0.75f, 2}, {"snd_magic_asteroid", 0.75f, 2}, {"snd_magic_bomb_middle", 0.75f, 2}, {"snd_magic_bomb_start", 0.75f, 2},
        {"snd_magic_deathray", 0.75f, 2}, {"snd_magic_energyball_end", 0.75f, 2}, {"snd_magic_energyball_middle", 0.75f, 2}, {"snd_magic_energyball_start", 0.75f, 2},
        {"snd_magic_fire_splash_end", 0.75f, 2}, {"snd_magic_fire_splash_middle1", 0.75f, 2}, {"snd_magic_fire_splash_middle2", 0.75f, 2}, {"snd_magic_fire_splash_middle3", 0.75f, 2},
        {"snd_magic_fire_splash_start", 0.75f, 2}, {"snd_magic_fireball_end", 0.75f, 2}, {"snd_magic_fireball_middle", 0.75f, 2}, {"snd_magic_fireball_start", 0.75f, 2},
        {"snd_magic_firepillar_end", 0.75f, 2}, {"snd_magic_firepillar_start", 0.75f, 2}, {"snd_magic_ice_ball_end", 0.75f, 2}, {"snd_magic_ice_ball_start", 0.75f, 2},
        {"snd_magic_ice_pins_end", 0.75f, 2}, {"snd_magic_ice_pins_middle", 0.75f, 2}, {"snd_magic_ice_pins_start", 0.75f, 2}, {"snd_magic_lightningarrow_end", 0.75f, 2},
        {"snd_magic_lightningarrow_middle", 0.75f, 2}, {"snd_magic_lightningarrow_start", 0.75f, 2}, {"snd_magic_massbomb_end", 0.75f, 2}, {"snd_magic_massbomb_middle", 0.75f, 2},
        {"snd_magic_massbomb_middle2", 0.75f, 2}, {"snd_magic_massbomb_start", 0.75f, 2}, {"snd_magic_mind_throw_hit", 0.75f, 2}, {"snd_magic_mind_throw_start", 0.75f, 2},
        {"snd_magic_saw_long", 0.75f, 2}, {"snd_magic_water_ball_end", 0.75f, 2}, {"snd_magic_water_ball_start", 0.75f, 2}, {"snd_magic_wave_end", 0.75f, 2},
        {"snd_magic_wave_start", 0.75f, 2}, {"snd_blizzard_1", 0.75f, 2}, {"snd_blizzard_2", 0.75f, 2}, {"snd_blizzard_3", 0.75f, 2},
        {"snd_bow_fast", 0.70f, 1}, {"snd_bow_long", 0.70f, 1}, {"snd_bucher_jump_new", 0.70f, 2}, {"snd_bucher_touchdown", 0.70f, 2},
        {"snd_composite_sword_heavy_slash1", 0.70f, 2}, {"snd_composite_sword_heavy_slash2", 0.70f, 2}, {"snd_composite_sword_heavy_slash3", 0.70f, 2}, {"snd_composite_sword_stance", 0.70f, 2},
        {"snd_composite_sword_whip", 0.70f, 2}, {"snd_earthquake", 0.70f, 2}, {"snd_f_cough", 0.70f, 1}, {"snd_harpoon_shoot", 0.70f, 2},
        {"snd_hermit_lightning", 0.70f, 2}, {"snd_hermit_lightning2", 0.70f, 2}, {"snd_hermit_storm_idle", 0.70f, 2}, {"snd_hermit_storm_start", 0.70f, 2},
        {"snd_m_cough", 0.70f, 1}, {"snd_m_pl_hit2", 0.70f, 2}, {"snd_musket_shot_1", 0.70f, 2}, {"snd_musket_shot_2", 0.70f, 2},
        {"snd_roots_end", 0.70f, 2}, {"snd_roots_start", 0.70f, 2}, {"snd_sawblade_1", 0.70f, 2}, {"snd_sawblade_2", 0.70f, 2},
        {"snd_sawblade_3", 0.70f, 2}, {"snd_sawblade_long", 0.70f, 2}, {"snd_shoker2", 0.70f, 2}, {"snd_smallsphere_middle", 0.75f, 2},
        {"snd_smallsphere_start", 0.75f, 2}, {"snd_spin1", 0.70f, 2}, {"snd_spin2", 0.70f, 2}, {"snd_super_hit2", 0.90f, 2},
        {"snd_sword_pierce", 0.70f, 2}, {"snd_titan_attack1", 0.80f, 1}, {"snd_titan_attack2", 0.80f, 1}, {"snd_titan_attack3", 0.80f, 1},
        {"snd_titan_attack4", 0.80f, 1}, {"snd_titan_death", 0.80f, 1}, {"snd_titan_hit1", 0.80f, 2}, {"snd_titan_hit2", 0.80f, 2},
        {"snd_titan_hit3", 0.80f, 2}, {"snd_titan_hit4", 0.80f, 2}, {"snd_titan_laugh", 0.80f, 1}, {"snd_titan_loose", 0.80f, 1},
        {"snd_titan_swish1", 0.80f, 2}, {"snd_titan_swish2", 0.80f, 2}, {"snd_titan_swish3", 0.80f, 2}, {"snd_titan_swish4", 0.80f, 2},
        {"snd_titan_throw_hit", 0.80f, 2}, {"snd_wasp_fly_end", 0.70f, 2}, {"snd_wasp_fly_mid", 0.70f, 2}, {"snd_wasp_fly_start", 0.70f, 2},
        {"snd_widow_teleport_end", 0.70f, 2}, {"snd_widow_teleport_start", 0.70f, 2},
    };
    count = sizeof(kGroups) / sizeof(kGroups[0]);
    return kGroups;
}

// Is `js_name` one of the 154 `ta.WBa` ids?
inline bool sfx_is_js_id(const char* js_name) {
    if (js_name == nullptr || *js_name == '\0') return false;
    std::size_t n = 0;
    const SfxGroup* g = sfx_groups(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (eq(js_name, g[i].event)) return true;
    }
    return false;
}

// The `ta.WBa()` + "which wav" resolution: a JS `snd_*` name -> the wav stem
// under the sfx dir, or nullptr when NOTHING may play. nullptr covers both
// "not a `ta.WBa` id" (the JS `WBa` miss — pack sounds, the `snd_Roots_*`
// case typo) and "id present but the APK shipped no sample"
// (`snd_smallsphere_start/middle`).
inline const char* sfx_stem_for_js(const char* js_name) {
    if (!sfx_is_js_id(js_name)) return nullptr;
    // The three UI ticks shipped ONLY inside the web audio bank
    // `reference/www/res/audio/sounds_a.ogg`; the APK wav set never carried
    // them. `click_1.wav` here is that bank's slot 0 (see the module comment
    // history): `Ss.kWa` (L1237018) assigns each bank sub-sound
    // `id = GL_index + 65535`, and `ta.WBa` pins `snd_click_1 = 65535`.
    if (eq(js_name, "snd_click_1") || eq(js_name, "snd_click_2") ||
        eq(js_name, "snd_focus_1")) {
        return "click_1";
    }
    // Two stems were extracted with a capital R (`Roots_start.wav` /
    // `Roots_end.wav`) while the id table spells them lowercase.
    if (eq(js_name, "snd_roots_start")) return "Roots_start";
    if (eq(js_name, "snd_roots_end")) return "Roots_end";
    // Id present, sample absent from the shipped APK wav set -> JS-silent.
    if (eq(js_name, "snd_smallsphere_start") ||
        eq(js_name, "snd_smallsphere_middle")) {
        return nullptr;
    }
    return js_name + 4;  // skip the "snd_" prefix
}

}  // namespace sf2::audio
