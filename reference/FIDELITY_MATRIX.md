# FIDELITY_MATRIX — oracle vs port state captures

Phase 0 harness (phase1 step9). One row per state in the oracle's fixed state
list. The oracle agent captures `reference/traces/oracle_matrix/<state>.png`;
the port captures `reference/traces/port_matrix/<state>.png` via
`game --fidelity-tour` (driver: `app/game/main.cpp`, `kFidelitySteps` L398-460).

## Diff method

- **diff %** — pixels with `max-channel abs-delta > 12/255` at 1280x720
  (the `ui_diff.py` gate, `reference/tools/ui_diff.py:36-52`). Computed by
  invoking the same algorithm inline over the two directories (the shipped
  script hard-codes `oracle_<name>.png`/`port_<name>.png` in **one** dir, so
  the split `oracle_matrix/`+`port_matrix/` layout needs the inline pass; no
  temp files written).
- **structural check** — measured, not expected: near-black fraction
  (lum<16), tile-level content/black/flat classification on a 16x9 grid
  (80x80 px tiles), and the diff bounding box.
- **verdict** — `MATCH` (same screen, diff <= 8% and only localized state
  differences), `CLOSE` (same screen/intent, moderate diff), `WRONG`
  (reachable but different screen content/layout/backdrop), `UNREACHABLE`
  (port cannot render the oracle state; artifact is a substitute, often a
  byte-identical duplicate frame).

Regenerate the port column with:

```
build/app/game/Release/game.exe reference/www/res reference/saves/save.xml --fidelity-tour
```

Oracle-side reachability (which oracle states are MATCH vs CLOSEST in the
MANIFEST) is carried in the owner note; it is orthogonal to the port diff.

## Matrix

| state | oracle file | port file | diff % | structural check | verdict | owner note |
|---|---|---|---|---|---|---|
| splash | `oracle_matrix/splash.png` | `port_matrix/splash.png` | 7.30 | Art (cast/logo/bg) aligns; diff is the loading banner only: oracle "Загрузка 94%" in a scroll frame vs port "Loading 31%" in a plain rect (x500-780, y560-660). bbox 250,37-1278,719. | CLOSE | Locale: port EN vs oracle RU. `loading_word` (`app.cpp:137-147`) + `ensure_lang` EN-only (`screens.cpp:3189`; comment `screens.cpp:121-123`). Oracle MANIFEST: MATCH. |
| loader | `oracle_matrix/loader.png` | `port_matrix/loader.png` | 43.54 | Oracle = gold SHADOW FIGHT 2 logo+figure centred (x96-1219, y128-400, 64.4% black); port = 98.8% black, only "Loading 100%" text. Loader art entirely missing. | WRONG | `app.cpp:749` `if (!loader)` gates all splash art; loader branch (`app.cpp:829-831`) draws only the string, so `res/splash/logo.png` is never drawn in the loader window. |
| tut_fight_stance | `oracle_matrix/tut_fight_stance.png` | `port_matrix/tut_fight_stance.png` | 56.00 | Port = Dojo hub with the SENSEI modal open (not a fight). Oracle = tutorial fight idle stance. | UNREACHABLE | No tutorial fight in the port — `app.cpp:601-611 boot()` pushes Dojo directly; fidelity steps L404-407 capture the hub. Oracle MANIFEST: MATCH. |
| tut_fight_phase2 | `oracle_matrix/tut_fight_phase2.png` | `port_matrix/tut_fight_phase2.png` | 54.61 | Port = Dojo hub + sensei modal. Oracle = tutorial punchbag lesson. | UNREACHABLE | Same as `tut_fight_stance` (step L405). |
| tut_block | `oracle_matrix/tut_block.png` | `port_matrix/tut_block.png` | 95.43 | Port = Dojo hub + sensei modal. Oracle = punchbag lesson (oracle itself CLOSEST — no distinct block step). | UNREACHABLE | Step L406. Oracle MANIFEST CLOSEST. |
| tut_win | `oracle_matrix/tut_win.png` | `port_matrix/tut_win.png` | 86.21 | Port = Dojo hub + sensei modal. Oracle = "FIRST STRIKE!" tutorial frame. | UNREACHABLE | Step L407. Oracle MANIFEST CLOSEST. |
| results_win | `oracle_matrix/results_win.png` | `port_matrix/results_win.png` | 85.31 | Port = fight-scene overlay "You lose!" / GREAT / TAP TO CONTINUE; stat table (PRIZE/PERFECT/FIRST STRIKE/MAX COMBO/SHOCK/PASSIVE STYLE) absent; moonlit backdrop vs oracle bamboo. Oracle only ever reached a loss. | WRONG | `ResultsScreen::render_impl` (`screens.cpp:5636`) is an INVENTED screen (no `dJ()==10`); JS `kk` result dialog `sf2.js L2057-2061`. Oracle MANIFEST CLOSEST. |
| dojo_hub | `oracle_matrix/dojo_hub.png` | `port_matrix/dojo_hub.png` | 54.47 | Port dojo room + SENSEI modal (x760-1230, y40-290) + a zoomed viewer framing (bag x795 y295-615 vs oracle x830 y75-575; lantern larger). Oracle hub = nav collapsed, no modal. | WRONG | Sensei modal should not be up: `quest_modal_top` only drains in headless (`screens.cpp:74-77`), but the fidelity capture is non-headless, so `draw_quest_modal` (`screens.cpp:257`) renders. Viewer/bag framing invented (`DojoScreen::render_impl` `screens.cpp:3530`; PORT_AUDIT_UI §2.2). |
| dojo_menu_open | `oracle_matrix/dojo_menu_open.png` | `port_matrix/dojo_menu_open.png` | 77.36 | Port nav column expanded (left icon stack) BUT the sensei modal is still open; flat icon buttons vs oracle framed `Le` buttons; oracle has no modal. | WRONG | Same stuck-modal root; nav is the INVENTED flat row/column vs JS `za` vertical `Le` column (`sf2.js L1972-1980`; PORT_AUDIT_UI §2.1). |
| dojo_sensei | `oracle_matrix/dojo_sensei.png` | `port_matrix/dojo_sensei.png` | 95.13 | Port == `dojo_menu_open` (modal already up); oracle = centred sensei quest dialog over a dimmed hub. | UNREACHABLE | Port never renders the oracle's modal state distinctly; `draw_quest_modal` flat panel vs JS `od` 9-slice (`sf2.js L1894-1900`; PORT_AUDIT_UI §2.9/§0 item 29). Oracle MANIFEST: MATCH. |
| map_zone1 | `oracle_matrix/map_zone1.png` | `port_matrix/map_zone1.png` | 75.02 | Port = invented flat circular node buttons (TOURNAMENT/DUEL/SURVIVAL/OLD WOUNDS/LYNX) over a sepia backdrop + header "Hero Reborn \| TOURNAMENT 0/2 NEXT TOURNAMENT \| SURVIVAL BEST 0"; no `Rr` info panel, no МЕНЮ. Oracle = ZONE_1 parchment, Рысь highlighted, right panel (Рысь/Телохранители/Нормально/50/В БОЙ). | WRONG | Backdrop by file order vs JS `fileName#` (`sf2.js L2143`); `load_zone_map` (`screens.cpp:2655`). Node art INVENTED (single `BattleBtnActive/*lynx`); `Rr` panel `L2098` / `Xr` list `L2133` not modelled; zone tab strip + BRACKET invented. `MapScreen::render_impl` (`screens.cpp:3911`). |
| map_node_sel | `oracle_matrix/map_node_sel.png` | `port_matrix/map_node_sel.png` | 75.02 | Port == `map_zone1` (byte-identical MD5). Oracle node highlight not modelled. | UNREACHABLE | Step L417. Oracle MANIFEST CLOSEST. |
| map_panels | `oracle_matrix/map_panels.png` | `port_matrix/map_panels.png` | 75.02 | Port == `map_zone1` (byte-identical). Oracle `Rr`/`Xr` panels absent. | UNREACHABLE | Step L418. Oracle MANIFEST: MATCH (panels) but not ported. |
| act_boss | `oracle_matrix/act_boss.png` | `port_matrix/act_boss.png` | 98.64 | Port == `map_zone1` (byte-identical). Oracle = BOSS_LYNX intro roster. | UNREACHABLE | Boss act bypassed; step L419-421. Oracle MANIFEST: MATCH. |
| shop_tab1 | `oracle_matrix/shop_tab1.png` | `port_matrix/shop_tab1.png` | 91.85 | Port = flat card grid on a blue/sky gradient (NOT the dojo), left nav column, "Knives" EN, invented dark detail panel, top-left "Fists DMG 0". Oracle = dojo bg + scroll + right item panel "Ножи" + bottom icon strip + ПРИМЕРИТЬ. | WRONG | `ShopScreen::render_impl` (`screens.cpp:6384`); fixed grid vs JS `Oa.layout` `sf2.js L2293-2295`; card art `attributes/*` vs item images (`sf2.js L2307`; PORT_AUDIT_UI §2.4/§0 item 16/18). Locale EN vs RU. |
| shop_tab2 | `oracle_matrix/shop_tab2.png` | `port_matrix/shop_tab2.png` | 91.84 | Same invented grid; armor tab. | WRONG | As `shop_tab1`; tab art guessed. |
| shop_tab3 | `oracle_matrix/shop_tab3.png` | `port_matrix/shop_tab3.png` | 91.84 | Same invented grid; helm tab. | WRONG | As `shop_tab1`. |
| shop_tab4 | `oracle_matrix/shop_tab4.png` | `port_matrix/shop_tab4.png` | 91.85 | Same invented grid; ranged tab (oracle locked "Побей Рысь..."). | WRONG | As `shop_tab1`. |
| shop_tab5 | `oracle_matrix/shop_tab5.png` | `port_matrix/shop_tab5.png` | 91.85 | Same invented grid; magic tab (oracle locked "Побей Отшельника..."). | WRONG | As `shop_tab1`. |
| shop_detail | `oracle_matrix/shop_detail.png` | `port_matrix/shop_detail.png` | 91.85 | Port == `shop_tab1` (byte-identical): the "select row 0" step produced no distinct detail state. Oracle = detail panel + ПРИМЕРИТЬ. | UNREACHABLE | Step L443 ("shop detail") is a no-op on the port's grid. Oracle MANIFEST: MATCH. |
| profile_tab0 | `oracle_matrix/profile_tab0.png` | `port_matrix/profile_tab0.png` | 91.52 | Port = LV1-9 perk list, EN ("LEARN AT LV n"), blue bg, left nav; level badge "1" overlaps the nav column (x~230, y~150). Oracle = skill scroll "У вас нет изученных умений" over the dojo. | WRONG | `EquipmentScreen::render_impl` (`screens.cpp:7291`) shows slots+grid, not JS `vb` tabbed Profile (`sf2.js L2189-2201`); `cs` tabs (`L2188`) MISSING (PORT_AUDIT_UI §2.5/§0 item 19/20). Locale EN vs RU. |
| profile_tab1 | `oracle_matrix/profile_tab1.png` | `port_matrix/profile_tab1.png` | 91.82 | Port == `moves` (byte-identical); EN; wrong bg. Oracle = profile MOVES tab. | WRONG | As `profile_tab0`. |
| profile_tab2 | `oracle_matrix/profile_tab2.png` | `port_matrix/profile_tab2.png` | 91.70 | Port = invented tab content, EN. Oracle = achievements. | WRONG | As `profile_tab0`. |
| profile_tab3 | `oracle_matrix/profile_tab3.png` | `port_matrix/profile_tab3.png` | 91.84 | Port = invented tab content, EN. Oracle = seal. | WRONG | As `profile_tab0`. |
| moves | `oracle_matrix/moves.png` | `port_matrix/moves.png` | 91.82 | Port == `profile_tab1` (byte-identical). Oracle = MOVES list (folded into Profile tab 1). | UNREACHABLE | Step L452 captures tab 1's screen; the port's separate `MovesScreen` is INVENTED (`sf2.js` `To.kOa`=11, `L2201`). Oracle MANIFEST: MATCH. |
| fight_intro | `oracle_matrix/fight_intro.png` | `port_matrix/fight_intro.png` | 92.77 | Port = in-fight ROUND banner (0/99, "ROUND") with both fighters. Oracle = VS roster (ТЕНЬ / ШИН) intro act. | WRONG | The `Rd` intro act (fade+lines, `sf2.js L2095-2098`) is not rendered; port jumps straight to the round. `FightScreen::render_impl` (`screens.cpp:4935`). Oracle MANIFEST: MATCH. |
| fight_stance | `oracle_matrix/fight_stance.png` | `port_matrix/fight_stance.png` | 43.88 | Scene + both fighters align (moonlit rooftops). Port HUD missing: fighter portrait avatars (`Hf=oe`), names (`Sh=Fr`), health-bar segment marks; timer 97 vs oracle 99; bars orange/blue vs oracle both orange. | CLOSE | HUD hard-coded 440x25@115.7 vs JS `Sf`/`lk` (`sf2.js L2033-2038`: 425x43, `c=min(W,H)/2/675*g`, centers `∓520*c*e`); PORT_AUDIT_UI §2.6/§0 items 21-23. |
| fight_attack | `oracle_matrix/fight_attack.png` | `port_matrix/fight_attack.png` | 49.54 | Same scene; attack/action frame differs (pose + timing). | CLOSE | As `fight_stance` (HUD) + move-interval timing. Step L429. |
| fight_hit | `oracle_matrix/fight_hit.png` | `port_matrix/fight_hit.png` | 49.04 | Port = mid-fight frame (no deterministic hit trigger). Oracle = "FIRST STRIKE!" hit frame. | UNREACHABLE | Step L433. Oracle MANIFEST CLOSEST. |
| fight_block | `oracle_matrix/fight_block.png` | `port_matrix/fight_block.png` | 36.95 | Port = attack-recovery frame. Oracle (CLOSEST) mid-fight frame. | UNREACHABLE | Blocking is a move interval, not a raw key (`on_key`); step L430-432. Oracle MANIFEST CLOSEST. |
| pause | `oracle_matrix/pause.png` | `port_matrix/pause.png` | 87.33 | Both "Pause" + 4 circular buttons over the fight scene. Port leaves the ROUND banner + HUD visible, button order differs (music/sound/play/exit vs oracle exit/music/sound/play), moonlit vs bamboo backdrop. | CLOSE | Pause is INVENTED 4-btn overlay vs JS `Dr` dialog (`sf2.js L2065`; PAUSE_STATIC.md; PORT_AUDIT_UI §0 items 25/28). `FightScreen` render (`screens.cpp:4935`). |
| settings | `oracle_matrix/settings.png` | `port_matrix/settings.png` | 37.45 | Both 4 rows (Sound/Music/Credits/Language). Port = full-screen parchment "SETTINGS" EN + BACK/RESTART; oracle = НАСТРОЙКИ RU over the dojo + НАЗАД (no RESTART). | CLOSE | `SettingsScreen::render_impl` (`screens.cpp:7803`); INVENTED screen (no `dJ()==11`); language row EN-only (`screens.cpp:7836-7857`). PORT_AUDIT_UI §0 item 30. |
| results_lose | `oracle_matrix/results_lose.png` | `port_matrix/results_lose.png` | 85.31 | Port = fight-scene overlay "You lose!" / GREAT / TAP TO CONTINUE; oracle = dedicated results screen with the stat table over bamboo. Stat table + OK button absent. | WRONG | As `results_win`; `ResultsScreen` INVENTED (`screens.cpp:5636`). |

## Summary

- **33/33** oracle and port artifacts present, 1280x720.
- **diff (thr12)**: min 7.30 (`splash`), max 98.64 (`act_boss`), median ≈ 85.
- **Verdict counts**: MATCH **0**, CLOSE **5**
  (`splash`, `fight_stance`, `fight_attack`, `pause`, `settings`),
  WRONG **17**, UNREACHABLE **11**.
- **Byte-identical port clusters** (a substitute frame reused for several
  states — hard evidence a state is not separately reachable):
  `act_boss` = `map_node_sel` = `map_panels` = `map_zone1`;
  `shop_detail` = `shop_tab1`;
  `moves` = `profile_tab1`.
  Oracle clusters: `results_lose` = `results_win`;
  `map_node_sel` = `map_panels` = `map_zone1`; `moves` = `profile_tab1`.
- **Dominant cross-cutting defects**: (1) locale EN vs RU; (2) the dojo
  SENSEI modal stuck open in every dojo capture; (3) invented screen layouts
  (map/shop/profile/settings/results/pause) vs the JS `za`/`Rr`/`vb`/`Oa`/`kk`
  node specs; (4) missing Loader logo art.

## Ranked worklist — worst first

Root causes are grouped (a row's `source` is the cluster source unless noted).
Native line numbers are current (`screens.cpp` is 7705 lines; PORT_AUDIT_UI's
older 4666-line cites are stale). JS cites are `reference/www/sf2.502f0946.js`
1-based (`L###`).

| rank | state(s) | diff % | dominant defect | likely source |
|---|---|---|---|---|
| 1 | act_boss, map_node_sel, map_panels, map_zone1 | 98.64 / 75.02 | **Map is an invented screen**: flat circular node buttons, wrong/“global” backdrop, extra header line, no `Rr` info panel / `Xr` status list, `МЕНЮ` missing. | `MapScreen::render_impl` `screens.cpp:3911`; `load_zone_map` `screens.cpp:2655` (backdrop by file order vs JS `fileName#` `sf2.js L2143`); node art invented; panels `sf2.js L2098/L2133`. |
| 2 | tut_block, tut_win, tut_fight_stance, tut_fight_phase2 | 95.43 / 86.21 / 56.00 / 54.61 | **No tutorial flow**: port boots to the Dojo hub; every `tut_*` capture is the hub (+ stuck sensei modal). | `app.cpp:601-611 boot()` pushes `kScreenDojo` directly; fidelity steps `app.cpp:404-407`. (`FLOW_STATIC.md §1`.) |
| 3 | dojo_sensei, dojo_hub, dojo_menu_open | 95.13 / 54.47 / 77.36 | **Sensei modal stuck open + invented dojo viewer/nav**: modal is up in all three dojo captures; viewer framing + hand-placed bag; nav is a flat icon column vs the `za` `Le` column. | `quest_modal_top` drains only headless `screens.cpp:74-77`; `draw_quest_modal` `screens.cpp:257`; `DojoScreen::render_impl` `screens.cpp:3530`; JS `za` `sf2.js L1972-1980`. |
| 4 | fight_intro | 92.77 | **VS roster intro act missing**: port jumps to the in-fight ROUND banner. | JS `Rd` act `sf2.js L2095-2098`; `FightScreen::render_impl` `screens.cpp:4935`. |
| 5 | shop_detail, shop_tab1, shop_tab4, shop_tab5, shop_tab2, shop_tab3 | 91.85 / 91.85 / 91.85 / 91.84 / 91.84 | **Shop invented (layout + art + backdrop) + EN**: flat grid on a blue gradient (not the dojo), attribute icons vs item images, no responsive `Oa.layout`; detail select is a no-op. | `ShopScreen::render_impl` `screens.cpp:6384`; JS `Oa.layout` `sf2.js L2293-2295`, item images `L2307`. |
| 6 | profile_tab3, moves, profile_tab1, profile_tab2, profile_tab0 | 91.84 / 91.82 / 91.82 / 91.70 / 91.52 | **Profile wrong screen (Equipment) + EN**: slots+grid instead of the `vb` tabbed content; `cs` tab strip missing; Moves is a separate invented screen; level badge overlaps nav. | `EquipmentScreen::render_impl` `screens.cpp:7291`; JS `vb` `sf2.js L2189-2201`, `cs` `L2188`; PORT_AUDIT_UI §2.5. |
| 7 | pause | 87.33 | **Pause is an invented 4-btn overlay**: leaves ROUND banner + HUD up; button order differs; wrong backdrop. | JS `Dr` dialog `sf2.js L2065`; PAUSE_STATIC.md; `screens.cpp:4935`. |
| 8 | results_lose, results_win | 85.31 | **Results layout wrong**: no stat table (PRIZE/PERFECT/FIRST STRIKE/MAX COMBO/SHOCK/PASSIVE STYLE), no OK; overlay on the fight scene vs oracle's bamboo results screen. | `ResultsScreen::render_impl` `screens.cpp:5636` (INVENTED; no `dJ()==10`); JS `kk` `sf2.js L2057-2061`. |
| 9 | loader | 43.54 | **Loader logo art missing**: 98.8% black, only "Loading 100%". | `app.cpp:749 if (!loader)` gates art; loader branch `app.cpp:829-831`. |
| 10 | settings | 37.45 | **Settings layout + locale**: EN "SETTINGS", BACK/RESTART vs RU НАСТРОЙКИ over the dojo; RESTART is invented. | `SettingsScreen::render_impl` `screens.cpp:7803`; language EN-only `screens.cpp:7836-7857`. |
| 11 | fight_stance, fight_attack, fight_hit, fight_block | 49.04–43.88 (block 36.95) | **Fight HUD incomplete** (scene itself close): missing portraits/names/bar segments; hard-coded bar 440x25@115.7 vs JS formula; timer/pips off. | JS `Sf`/`lk`/`Er` `sf2.js L2021-2038`; `FightScreen::render_impl` `screens.cpp:4935`; PORT_AUDIT_UI §2.6. |
| 12 | splash | 7.30 | **Locale + loading %**: "Loading 31%" vs "Загрузка 94%" (scroll frame shape differs). | `app.cpp:137-147` + `ensure_lang` EN-only `screens.cpp:3189`. |

### Cluster source (fix order by blast radius)

1. **Locale RU support** (`screens.cpp:3189 ensure_lang` + `app.cpp lang_`) — unblocks `splash`, `settings`, all shop/profile/map labels. `res/ui/font-ru.*` ships (PORT_AUDIT_UI §0.3), so this is a wiring gap, not an asset gap.
2. **Dojo modal gating** (`quest_modal_top`, `screens.cpp:74`) — the captures are non-headless, so the modal must be dismissed/queued correctly; fixes `dojo_hub`/`dojo_menu_open`/`dojo_sensei`.
3. **Map rewrite** (`screens.cpp:3911`, `2655`) — biggest single-screen diff (`act_boss` 98.6%).
4. **Shop / Profile / Results / Settings rewrites** per PORT_AUDIT_UI §4 (HUGE/MEDIUM/SMALL).
5. **Fight HUD formula** (`sf2.js L2033-2038`).
6. **Tutorial flow** (`app.cpp:601`) — new screen(s), the largest build.
7. **Loader logo** (`app.cpp:749`).

## Structural FLOW divergence — CONFIRMED

The port and the original boot into **different default states**:

- **Port**: `App::boot()` (`app.cpp:601-611`) logs
  `Preloader(0) -> Loader(2) -> Dojo(3)` and immediately
  `screens_->push(make_screen(*screens_, kScreenDojo))`. Booting to the Dojo
  hub is hard-coded; there is no tutorial-fight screen. Every `tut_*` port
  artifact is the hub (differing only by animation frame; sizes within ~500 B
  of `dojo_hub.png`), and each carries the stuck sensei modal.
- **Original (JS), fresh profile**: boots into the **blocking tutorial**
  (move -> punchbag fight). The oracle MANIFEST is explicit (`MANIFEST.md`
  L30-34): *"Without (2) the game boots into the blocking tutorial (move ->
  punchbag fight); the hub and all of Map/Shop/Profile/Settings are
  unreachable."* The oracle's hub/map/shop/profile/settings captures were only
  reachable because `reference/runner/index.html` seeds a **post-tutorial**
  `localStorage` save (`Tutorial=END`, `ORACLE_POST_TUTORIAL=true`) — i.e. the
  harness had to defeat the tutorial to reach the hub at all.

**Confirmed from the matrices**: oracle `tut_*` exist as real tutorial states
and the oracle MANIFEST documents tutorial-blocked reachability; the port
`tut_*` files are near-duplicates of `dojo_hub` with no fight content. The
approved fresh-profile target (`fresh/tutorial-from-0`) therefore requires the
port to add the tutorial flow, not the post-tutorial seed.

## Tools used

- `reference/tools/ui_diff.py` — the thr12 pixel-diff algorithm (run inline;
  its `oracle_<name>`/`port_<name>` single-dir convention does not fit the
  `oracle_matrix/`+`port_matrix/` split).
- Inline `python -c` (numpy + PIL): thr12 diff %, mirror probe, near-black
  fraction, 16x9 tile content/black/flat classification, diff bbox.
- `Get-FileHash` (MD5) — byte-identical cluster detection.
- `reference/traces/oracle_matrix/MANIFEST.md` — oracle reachability
  (MATCH vs CLOSEST) and the post-tutorial-seed harness fact.
- `reference/PORT_AUDIT_UI.md` — JS `L###` ↔ native line mapping and verdicts.
- `app/game/main.cpp` (`kFidelitySteps` L398-460) — port tour/state mapping.
- `core/app/screens.cpp` (`ensure_lang` L3189, `quest_modal_top` L74,
  screen `*_impl` cites above), `core/app/app.cpp` (`boot` L600-611,
  `draw_boot_splash` L703).
- Read-only image inspection of the differing pairs.
