# MUSIC_STATIC — track mapping, file ids, screen wiring (web build)

Static-only spec from `reference/www/sf2.502f0946.js` (2533 lines, 1-based:
`ta` L1264-1276, `rb` L1277, fight hooks L384/L2008, cutscene L2096-2098)
plus the asset manifest (`G.rq`, L2490: 1357 entries) and
`reference/extracted/xml/res/stages.xml` (`Music` attrs).
Refines Stream 1's `stages.xml` Battle-Music wiring with exact ids/files.

## 1. Engine: `ta` (L1264-1266)

- `ta.Ut(name, loop=true)` (L1264-1265): `Zla()` stop previous → `u0(name)`
  → id → lazy-load if `G.data.v[id]` missing → `$f.play(id, loop)`.
- `ta.Zla()` (L1265): stop current (`u0(yP)`), clear.
- `ta.sK/m3` (L1265): deferred play when audio context unlocks.
- `ta.ak(name, loop=false)` SFX (L1264): `WBa(name)` → `play(id, loop)`;
  **`loop=true` hits `debugger`** — SFX are one-shot by construction.
- `ta.Jwb(name)` stop one (L1264). `WT/VT` mute toggles → `$f.cMa/uF`
  (L1276); `GMa(a)` fade → `$f.uF(a)` (L1277).
- `lb.OS(name="menu", loop=true)` (L1276): `rJ` guard (play once until
  reset) → `ta.Ut`. `rb` helpers (L1277): gong/buy/upgrade/learn/clicks/
  focus via `ak` (`snd_gong` etc.).

## 2. Track table: `u0` name → asset id → manifest file

`u0` builds `HGa` once (L1274-1276); ids index `G.rq` (manifest §proof in
COMBAT_STATIC App. B); files are `audio/<name>_music.{audio}` (`{audio}` →
ogg/m4a per `G.HI`, L2395-2396).

| `u0` name | id | file |
|---|---|---|
| `menu` | 1318 | `audio/menu_music.{audio}` |
| `act` | 1353 | `audio/act_music.{audio}` |
| `fight1_samurai_spirit` | 1342 | `audio/fight1_samurai_spirit_music.{audio}` |
| `fight2_blade_dance` | 1334 | `…/fight2_blade_dance_music.{audio}` |
| `fight3_vengeance` | 1325 | `…/fight3_vengeance_music.{audio}` |
| `fight4_forest_of_death` | 1324 | `…` |
| `fight5_ninja_in_the_night` | 1323 | `…` |
| `fight6_sparring` | 1322 | `…` |
| `fight7_fat_boss` | 1321 | `…` |
| `fight8_final_boss` | 1320 | `…` |
| `fight9_master_skills` | 1319 | `…` |
| `fight10_black_warrior` | 1352 | `…` |
| `fight11_ronin` | 1351 | `…` |
| `fight12_deadly_smoke` | 1350 | `…` |
| `fight13_old_sensei` | 1349 | `…` |
| `fight14_ship_battle` | 1348 | `…` |
| `fight15_shadow_lady` | 1347 | `…` |
| `fight16_the_battlefield_flowers` | 1346 | `…` |
| `fight17_cave` | 1345 | `…` |
| `fight18_fuji` | 1344 | `…` |
| `fight19_volcano` | 1343 | `…` |
| `fight21_lesson_in_the_dark_room` | 1341 | `…` |
| `fight22_heavenly_clouds` | 1340 | `…` |
| `fight23_burning_town` | 1339 | `…` |
| `fight24_ruins_village` | 1338 | `…` |
| `fight25_hive` | 1337 | `…` |
| `fight27_factory` | 1336 | `…` |
| `fight28_flying_rocks` | 1335 | `…` |
| `fight30_gates_of_shadows` | 1333 | `…` |
| `fight31_graveyard_ships` | 1332 | `…` |
| `fight32_starship` | 1331 | `…` |
| `fight33_stone_forest` | 1330 | `…` |
| `fight34_halls_of_the_dead_heroes` | 1329 | `…` |
| `fight36_stardocks` | 1328 | `…` |
| `fight37_Titan_Epic_Fight` | 1327 | `…` (case-sensitive key) |
| `fight38_sakura_forest` | 1326 | `…` |

34 fight tracks == 34 distinct `Music` values in `stages.xml` (verified by
parse). **No `fight20/26/29/35` rows exist in `u0` — and no stage uses
them.** SFX packs: 1316 `sounds_b`, 1317 `sounds_a`; `WBa` table
(L1265-1274, ~150 `snd_*` ids 65535+); `rb` helpers (L1277).

## 3. Screen wiring

| Screen / event | Call | Lines |
|---|---|---|
| Map (also Shop/Dojo/Profile: no call → inherit) | `Ya.init → lb.OS()` = menu | L2125 (FLOW), L1276 |
| Fight start | `ai.Ut() → ta.Ut(Da.tp)`; `Da.tp` = stages `Music` attr (L196 → `Lc.tp` L1404) | L2008, L196 |
| Fight end / FightNone enter+exit | `lb.OS()` (menu) | L384 |
| Act cutscene | `Rd.Ut`: `GMa(1)` fade → `Zla` → `OS("act")`; `Rd.end` → `OS()` menu | L2096-2098 |

Loop: music `loop=true`, SFX one-shot. No crossfade primitive —
stop-then-play (`Zla` inside `Ut`); `Rd` fades via `GMa`.

## OPEN (needs runtime trace)

1. `$f` backend timing (preload latency on first `Ut`, `uF` fade curve).
2. `rJ` guard resets across rapid screen hops (double-`OS` races).
