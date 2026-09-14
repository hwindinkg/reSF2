# FIDELITY_MATRIX — oracle vs port state captures (DEFINITIVE)

Phase 1 step 9. One row per state, all **33** pairs
`reference/traces/oracle_matrix/<state>.png` vs
`reference/traces/port_matrix/<state>.png`, each verified **1280×720**.
This file supersedes every earlier revision: earlier waves mixed
`thr12 %` with MAD and were computed on stale port captures (the port_matrix
PNGs were regenerated 13.09 23:55 at HEAD `132bfca1`, after
`0def24b6`/`ca8e2a20`/`b55cd05c`/`132bfca1`). The table below is the single
authoritative read of the on-disk artifacts.

## Metric definition

Computed inline with `python -c` (numpy + PIL), one pair at a time, no temp
files, no image bulk-loads. Exact algorithm = `reference/tools/ui_diff.py:36-52`:

- `a`, `b` = RGB arrays (`int16`), shape `(720,1280,3)`.
- per-pixel delta `d = max_channel |a - b|` (max over R,G,B).
- **`%pixels>12`** = `100 * mean(d > 12)` — fraction of pixels whose **max
  channel** abs-delta exceeds 12/255. This is the `ui_diff.py` gate metric
  (`--threshold 12`).
- **MAD** = `mean(|a - b|)` over **all** pixels and all 3 channels (mean
  absolute difference, 0–255 scale), reported for cross-wave comparability.
- **dominant residual region** = the 80×80 tile (16×9 grid over the frame)
  with the largest mean max-channel delta, given as `x0-x1 y0-y1 (Δmean)`.
- **verdict**: `MATCH` `<10%`; `CLOSE` `10–30%`; `WRONG` `30–60%`;
  `UNREACHABLE` `>60%`, **or** the port artifact is a byte-identical
  substitute frame, **or** the port has no route to the state (tutorial).

## Invalid oracle captures — do NOT chase

| state | oracle MD5 | why it is not the state it claims |
|---|---|---|
| `results_win` | `03CDD2107B8034AA47441A938D04ECB0` | **byte-identical to `results_lose`** — the oracle only ever reached the BOSS_LYNX timed loss (`kk` "You lose!"); there is no oracle win frame. |
| `map_node_sel` | `D5FA21852E3881831B9B5173697A5F34` | identical to `map_panels`/`map_zone1` — no distinct node-selection highlight exists. |
| `map_panels` | `D5FA21852E3881831B9B5173697A5F34` | identical to `map_zone1` — the panels are visible but there is no distinct "panels" capture. |
| `moves` | `E8A8D1C204EB7E0720A1E35403BF0F2A` | identical to `profile_tab1` — the oracle folds MOVES into Profile tab 1; no separate screen. |
| `tut_block` | `46987E8BE6EF8B66E856ADF913EFA0AD` | CLOSEST substitute: punchbag lesson, the tutorial has no distinct block step. |
| `tut_win` | `421DA63DB2C66CBA3CA6533B6AA682FB` | CLOSEST substitute: "FIRST STRIKE!" frame; the tutorial fight was not won. |
| `fight_block` | `93CA3E6F8CF12589D89D35E479AB5F2A` | CLOSEST substitute: mid-fight frame; block is a move interval, not a raw key. |

Also excluded: `pause.tutorial.bak.png`, `results_win.tutorial.bak.png`,
`results_lose.tutorial.bak.png` — the **old tutorial battle** (ТЕНЬ vs КЕНДЖИ,
bamboo) captures, replaced by the normalized BOSS_LYNX frames; not part of the
33-pair set.

## Definitive table — ranked worst-first by `%pixels>12`

| state | oracle | port | %pixels>12 | MAD | verdict | dominant residual region | owner note |
|---|---|---|---|---|---|---|---|
| tut_block | `oracle_matrix/tut_block.png` | `port_matrix/tut_block.png` | 94.663 | 54.361 | UNREACHABLE | x720-880 y320-480 (Δ162) — centre fight area | No tutorial flow: port boots to Dojo hub. `App::boot` `app.cpp:661`; fidelity steps `app/main.cpp:412`. Oracle CLOSEST (no block step). |
| tut_win | `oracle_matrix/tut_win.png` | `port_matrix/tut_win.png` | 84.717 | 68.178 | UNREACHABLE | x720-800 y320-400 (Δ224) — centre fight area | As `tut_block`. Oracle CLOSEST ("FIRST STRIKE!" frame, no win). |
| profile_tab2 | `oracle_matrix/profile_tab2.png` | `port_matrix/profile_tab2.png` | 66.150 | 28.521 | WRONG | x240-400 y400-560 (Δ116) — centre-left content list | Profile is the invented Equipment screen (slots+grid); `vb` tabbed content missing. `EquipmentScreen::render_impl` `screens.cpp:8856`; JS `vb` `sf2.js L2189-2201`, `cs` strip `L2188`. |
| profile_tab3 | `oracle_matrix/profile_tab3.png` | `port_matrix/profile_tab3.png` | 60.910 | 24.906 | WRONG | x240-400 y400-560 (Δ117) — centre-left content list | As `profile_tab2`. |
| profile_tab1 | `oracle_matrix/profile_tab1.png` | `port_matrix/profile_tab1.png` | 60.881 | 24.899 | WRONG | x240-400 y400-560 (Δ117) — centre-left content list | As `profile_tab2`. |
| moves | `oracle_matrix/moves.png` | `port_matrix/moves.png` | 60.881 | 24.899 | WRONG | x240-400 y400-560 (Δ117) — centre-left content list | Port now renders a distinct Moves screen, but layout ≠ oracle. Oracle `moves.png` == `profile_tab1.png` (invalid). `EquipmentScreen::render_impl` `screens.cpp:8856`; JS `To.kOa`=11 `sf2.js L2201`. |
| pause | `oracle_matrix/pause.png` | `port_matrix/pause.png` | 52.985 | 18.424 | WRONG | x720-800 y240-400 (Δ80) — centre pause buttons | Pause overlay layout/button order/backdrop. `FightScreen::render_impl` `screens.cpp:6236`; JS `Dr` dialog `sf2.js L2065`. |
| profile_tab0 | `oracle_matrix/profile_tab0.png` | `port_matrix/profile_tab0.png` | 50.286 | 19.598 | WRONG | x80-160 y80-160 (Δ62) nav tab + x480-800 y480-560 (Δ56) content | As `profile_tab2`. |
| loader | `oracle_matrix/loader.png` | `port_matrix/loader.png` | 42.933 | 20.427 | WRONG | x400-960 y240-400 (Δ149) — loader art centre | Loader logo/figure art missing; only the "Loading" string draws. `App::draw_boot_splash` `app.cpp:774`. |
| fight_hit | `oracle_matrix/fight_hit.png` | `port_matrix/fight_hit.png` | 42.038 | 25.477 | WRONG | x480-720 y320-480 (Δ133) — centre impact | Frame substitute (no deterministic hit trigger) + HUD. `FightScreen::render_impl` `screens.cpp:6236`; JS `Sf`/`lk` `sf2.js L2021-2038`. Oracle CLOSEST. |
| fight_intro | `oracle_matrix/fight_intro.png` | `port_matrix/fight_intro.png` | 41.124 | 17.854 | WRONG | x160-400 y0-80 (Δ195) — top VS banner | VS roster intro act. `FightScreen::render_impl` `screens.cpp:6236`; JS `Rd`/`ik` intro `sf2.js L2095-2098`. |
| results_win | `oracle_matrix/results_win.png` | `port_matrix/results_win.png` | 39.846 | 21.768 | WRONG | x560-880 y320-480 (Δ96) — centre stat overlay | **Oracle invalid (== `results_lose`).** Layout/stat-table; `ResultsScreen::render_impl` `screens.cpp:7032`; JS `kk` `sf2.js L2057-2061`. |
| results_lose | `oracle_matrix/results_lose.png` | `port_matrix/results_lose.png` | 39.838 | 21.826 | WRONG | x560-880 y320-480 (Δ96) — centre stat overlay | Results stat table/OK button; `ResultsScreen::render_impl` `screens.cpp:7032`; JS `kk` `sf2.js L2057-2061`. |
| fight_attack | `oracle_matrix/fight_attack.png` | `port_matrix/fight_attack.png` | 38.962 | 23.562 | WRONG | x480-720 y320-400 (Δ195) — centre fighters/impact | Fight HUD + move-interval timing. `FightScreen::render_impl` `screens.cpp:6236`; JS `Sf`/`lk` `sf2.js L2021-2038`. |
| tut_fight_stance | `oracle_matrix/tut_fight_stance.png` | `port_matrix/tut_fight_stance.png` | 37.885 | 23.370 | UNREACHABLE | x400-560 y400-560 (Δ148) — centre fight area | No tutorial flow `App::boot` `app.cpp:661`. Oracle MANIFEST MATCH. |
| tut_fight_phase2 | `oracle_matrix/tut_fight_phase2.png` | `port_matrix/tut_fight_phase2.png` | 34.722 | 22.711 | UNREACHABLE | x400-560 y400-560 (Δ146) — centre fight area | As `tut_fight_stance`. |
| fight_stance | `oracle_matrix/fight_stance.png` | `port_matrix/fight_stance.png` | 28.331 | 14.389 | CLOSE | x720-960 y320-480 (Δ116) — right fighter | Scene aligns; HUD incomplete (portraits/names/bar segments, bar formula). `FightScreen::render_impl` `screens.cpp:6236`; JS `Sf`/`lk` `sf2.js L2021-2038`. |
| fight_block | `oracle_matrix/fight_block.png` | `port_matrix/fight_block.png` | 27.020 | 12.241 | CLOSE | x320-400 / x880-960 y400-480 (Δ53) — both fighters | Frame substitute (block is a move interval, `on_key`); `screens.cpp:6236`. Oracle CLOSEST. |
| settings | `oracle_matrix/settings.png` | `port_matrix/settings.png` | 26.106 | 10.899 | CLOSE | x1120-1280 y400-560 (Δ79) — right value rows | Settings rows/locale; `SettingsScreen::render_impl` `screens.cpp:9454`; language EN-only `ensure_lang` `screens.cpp:3729`. |
| dojo_hub | `oracle_matrix/dojo_hub.png` | `port_matrix/dojo_hub.png` | 23.589 | 14.533 | CLOSE | x400-560 y400-560 (Δ146) — dojo centre | Dojo room framing/viewer; `DojoScreen::render_impl` `screens.cpp:4362`; JS `za` nav `sf2.js L1972-1980`. |
| shop_tab3 | `oracle_matrix/shop_tab3.png` | `port_matrix/shop_tab3.png` | 21.377 | 13.762 | CLOSE | x240-400 y400-560 (Δ108) — item grid | Shop layout/art/backdrop; `ShopScreen::render_impl` `screens.cpp:7886`; JS `Oa.layout` `sf2.js L2293-2295`, item art `L2307`. |
| shop_tab2 | `oracle_matrix/shop_tab2.png` | `port_matrix/shop_tab2.png` | 20.981 | 13.925 | CLOSE | x240-400 y400-560 (Δ108) — item grid | As `shop_tab3`. |
| shop_tab1 | `oracle_matrix/shop_tab1.png` | `port_matrix/shop_tab1.png` | 19.112 | 13.204 | CLOSE | x320-400 y400-480 (Δ127) — item grid | As `shop_tab3`. |
| shop_detail | `oracle_matrix/shop_detail.png` | `port_matrix/shop_detail.png` | 18.796 | 12.808 | CLOSE | x240-400 y400-480 (Δ108) — detail/grid | Detail panel; `ShopScreen::render_impl` `screens.cpp:7886`. Oracle MANIFEST MATCH. |
| dojo_menu_open | `oracle_matrix/dojo_menu_open.png` | `port_matrix/dojo_menu_open.png` | 16.952 | 8.571 | CLOSE | x400-560 y400-560 (Δ73) + x160-240 y560-640 (Δ68) — dojo centre + left nav | Sticky sensei modal + nav column; `quest_modal_top` `screens.cpp:74`, `draw_quest_modal` `screens.cpp:295`. |
| map_zone1 | `oracle_matrix/map_zone1.png` | `port_matrix/map_zone1.png` | 15.775 | 12.013 | CLOSE | x1040-1200 y160-320 (Δ77) — right info panel | Map right `Rr` panel / `Xr` list; `MapScreen::render_impl` `screens.cpp:5070`; `load_zone_map` `screens.cpp:3056`; JS `Rr` `sf2.js L2098`, `Xr` `L2133`. |
| map_node_sel | `oracle_matrix/map_node_sel.png` | `port_matrix/map_node_sel.png` | 15.775 | 12.013 | UNREACHABLE | x1040-1200 y160-320 (Δ77) — right info panel | **Oracle invalid (== `map_zone1`)** and port frame byte-identical to `map_zone1` (`022B9E34B8E243FEE69EC3C2D10EF359`) — node-select is not separately rendered. `app/main.cpp:412`. |
| map_panels | `oracle_matrix/map_panels.png` | `port_matrix/map_panels.png` | 15.775 | 12.013 | UNREACHABLE | x1040-1200 y160-320 (Δ77) — right info panel | **Oracle invalid (== `map_zone1`)** and port frame byte-identical to `map_zone1` — panels not separately captured/reinterpreted. Same source as `map_zone1`. |
| shop_tab4 | `oracle_matrix/shop_tab4.png` | `port_matrix/shop_tab4.png` | 14.634 | 9.791 | CLOSE | x240-400 y400-480 (Δ109) — item grid | As `shop_tab3`. |
| shop_tab5 | `oracle_matrix/shop_tab5.png` | `port_matrix/shop_tab5.png` | 14.587 | 9.744 | CLOSE | x240-400 y400-480 (Δ108) — item grid | As `shop_tab3`. |
| dojo_sensei | `oracle_matrix/dojo_sensei.png` | `port_matrix/dojo_sensei.png` | 14.106 | 9.158 | CLOSE | x400-480 y240-400 (Δ64) + x1120-1200 y480-560 (Δ58) — modal body + buttons | Sensei dialog 9-slice/panel; `draw_quest_modal` `screens.cpp:295`; JS `od` `sf2.js L1894-1900`. Oracle MANIFEST MATCH. |
| act_boss | `oracle_matrix/act_boss.png` | `port_matrix/act_boss.png` | 9.218 | 6.015 | MATCH | x1200-1280 y240-480 (Δ38) — right edge | Boss roster right column; `FightScreen::render_impl` `screens.cpp:6236`. Oracle MANIFEST MATCH. |
| splash | `oracle_matrix/splash.png` | `port_matrix/splash.png` | 7.337 | 4.577 | MATCH | x480-640 y560-720 (Δ98) — bottom loading banner | Loading banner + locale ("Loading" vs "Загрузка"); `App::draw_boot_splash` `app.cpp:774`; `ensure_lang` `screens.cpp:3729`. Oracle MANIFEST MATCH. |

## Summary

- **33/33** pairs present and 1280×720. Metric: max-channel `d > 12` →
  `%pixels>12`; MAD over all pixels/channels.
- **Range**: `%pixels>12` 7.337 (`splash`) … 94.663 (`tut_block`);
  MAD 4.577 … 68.178.
- **Verdicts**: MATCH **2** (`splash`, `act_boss`), CLOSE **14**, WRONG **10**,
  UNREACHABLE **7** (`tut_block`, `tut_win`, `tut_fight_stance`,
  `tut_fight_phase2`, `map_node_sel`, `map_panels`, plus `moves` is WRONG and
  only the oracle side is invalid).
- **Byte-identical port clusters** (a substitute frame reused, hard evidence the
  state is not separately rendered): `map_zone1` = `map_node_sel` = `map_panels`
  (`022B9E34B8E243FEE69EC3C2D10EF359`); the port `tut_*` are near-duplicates of
  `dojo_hub`.
- **Invalid oracle captures** (see table above): `results_win`, `map_node_sel`,
  `map_panels`, `moves` (hash duplicates) + `tut_block`, `tut_win`,
  `fight_block` (CLOSEST substitutes).

### Remaining root causes (fix order by blast radius)

1. **Tutorial flow absent** — `App::boot` `app.cpp:661` pushes Dojo directly;
   fidelity steps `app/main.cpp:412`. Blocks all four `tut_*` states.
2. **Profile/Moves content** — `EquipmentScreen::render_impl`
   `screens.cpp:8856`; JS `vb` `sf2.js L2189-2201`. Worst *reachable* residuals
   (50–66%).
3. **Pause / Results / Fight HUD** — `FightScreen::render_impl`
   `screens.cpp:6236`, `ResultsScreen::render_impl` `screens.cpp:7032`;
   JS `Dr` `L2065`, `kk` `L2057-2061`, `Sf`/`lk` `L2021-2038`.
4. **Loader logo art** — `App::draw_boot_splash` `app.cpp:774`.
5. **Map right panel** — `MapScreen::render_impl` `screens.cpp:5070`; JS `Rr`
   `L2098`/`Xr` `L2133`.
6. **Shop grid/detail** — `ShopScreen::render_impl` `screens.cpp:7886`;
   JS `Oa.layout` `sf2.js L2293-2295`.
7. **Dojo sticky modal + framing** — `quest_modal_top` `screens.cpp:74`,
   `DojoScreen::render_impl` `screens.cpp:4362`.
8. **Locale / loading banner** — `ensure_lang` `screens.cpp:3729`
   (`splash`, `settings`).

## Repro / method

```
python -c "import os,numpy as np;from PIL import Image as I;\
o='reference/traces/oracle_matrix';p='reference/traces/port_matrix';\
ns=sorted(x for x in os.listdir(p) if x.endswith('.png'));\
[(lambda a,b: print(n, round(float(np.mean(np.max(np.abs(a-b),axis=2)>12))*100,3),\
round(float(np.mean(np.abs(a-b))),3)))(\
np.asarray(I.open(os.path.join(o,n)).convert('RGB'),dtype=np.int16),\
np.asarray(I.open(os.path.join(p,n)).convert('RGB'),dtype=np.int16)) for n in ns]"
```

Regenerate the port column with:

```
build/app/game/Release/game.exe reference/www/res reference/saves/save.xml --fidelity-tour
```

Tools: `reference/tools/ui_diff.py:36-52` (metric), inline `python -c`
(numpy/PIL), `Get-FileHash` MD5 (cluster/substitute detection),
`reference/traces/oracle_matrix/MANIFEST.md` (oracle reachability).
