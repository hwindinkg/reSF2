# reSF2 — Master TODO (web-only, 1:1)

Единственный оракул: reference/www/sf2.502f0946.js + shell/OracleShell.
Всё из .planning/archive/ — недействительно как референс.
Политика пуша: только локальные коммиты до слова пользователя (2026-09-04).

## METHODOLOGY — STATIC-FIRST (2026-09-04, до начала работ, не занижение)

Runtime AI/timer-трейсы отложены: tutorial не содержит AI-решений вообще
(`ia:0, Pqb:0` на 1202 записях пользователя; детерминизм при этом PASS
`a433d756==a433d756`, fidelity реплея 281/281 PASS). Логика целиком в JS —
порт идёт напрямую со строк, верификация — Node-harness golden tests
(`reference/tools/ai_golden.js` vs C++ `app/ai_golden`, 0 расхождений,
полное покрытие веток). Phase 6 gate ПЕРЕОПРЕДЕЛЁН ДО работ: Node-golden
вместо runtime-trace compare. Phase 8 (финальный runtime trace-match на
настоящем AI-бое) — без изменений.

## DONE

- Phase 0 — Hygiene (2026-09-03, `3bb38081`): архив stale-доков с баннерами,
  PROJECT.md, grep-gate PASS, `ai_controller.cpp`/`.gitignore`/STATE чистка.
- Phase 1 / Wave 1 (2026-09-04, `36d0fb74`): `trace_oracle.js` (инъекция до
  старта игры, энтропия методы `de.Pqb`/`de.ia`/`iPa`/`N0a`,
  покадровый JSONL), shell (fresh profile/ран, auto-close), `input_phase1.txt`.
  Build green, `node --check` clean.
- Phase 1 / Wave 2 (2026-09-04, `bde69b2f`): `extract_oracle.py` (canonical
  JSONL + sha256 + валидация полей), page-side `atframe`-стимул (drag/tap/key),
  `traces/README.md`. Baseline: tutorial без вводов доходит до phase 2 сам.
- Phase 1 / Prep (2026-09-04, `2efd3354`): input-путь резолвнут
  (`Df{control,index}`+`Gfa()` L453, `Za.DEa` гейты L459, `Nc.Lh` L461,
  стик-сектора L463-471 — координат после мэппинга нет); `O0a`-враппер,
  `[INPUT-REC]`, `atframe press/release` + прямой реплей через
  `fight.N0a/O0a`; `record_inputs.py`; smoke PASS (4/4 в точных кадрах,
  round-trip побайтово); `reference/AI_STATIC.md` (дерево `de.Pqb`,
  `Md`/`cc.Gb`, PRNG+сиды, файлы значений, OPEN-пункты).
- Manual record mode (2026-09-04, `a0d72ede`): запуск без `--input-script` =
  ноль инъекций + ноль auto-close; физический ввод 1:1; проверено headless
  (601 запись, 0 инжектов, процесс жив до kill).
- Gate re-run на записи пользователя (2026-09-04, локальный коммит — см. ниже):
  `recorded_inputs.txt` (1268 событий, f 153..939, один бой, монотонно);
  replay ×2 → **оба `a433d7561820ccad066185160c298f37174c2e7a30ad44656f75c8c4d229ba
    — детерминизм PASS** (602 строки каждый, вкл. `traceEvents=280`);
  fidelity реплея **PASS** (281/281 событие f≤600 побайтово: кадр, press/release,
  control, index, type).





- Modes/export wave (2026-09-04): stages parse + tournament/survival resolve (Number/z6a, Groups random+NoDoubles, rewards, series advance) + apply_mode_setup (rounds/time/recovery, DamageFactor rules, NoBullets flag, enemy rebuild); export Ug envelope aligned (ke-LE + yna frames + SF2/base64, Dpb string-length noted); S20 goldens + ElementTree checks. Node 99 GREEN, diffs 0, loop pending.
- Ranged/magic wave (2026-09-04): bh/dO/my state + Ka/yKa init + per-round reset; hZ/Hwa/LA + slot-8 publish; lp/sp conditions live; AddBullets/AddMagicCharge real; Jma recharge from dealt damage; JNa 27 log-only (no magic state), 28 SlowModel timescale real, 29 ChangeModelColor tint real; TurnOffCollision fighter gate + revert; ModAttributes aP expression eval; SetModFrames ns loop; NoBulletsReplenishment default-off hook (Stream 2: no refill path found); S19 goldens. Node 96 GREEN expected, loop 13/13 pending verify.
- Review-fix wave (2026-09-04, REVIEW_PHYS_TRIG): HIGH ModFlag installs named persistent mod; kp numeric comparisons (?PlayerParameter/?Hit/?Variable/?Abs + arithmetic, 86 shipped uses); MED margins raw-gb + Ula/Pda insets at build (telemetry fired: EHead margin2 0.5, old code inflated), verbatim Bz roles + n$/o$ split + Cz-collinear target-start + kd_null no-knockback, Yka re-register at round_start, Health absolute gd; LOW collinear (in Bz), dmg_add routed-doc, YZa namespace action-name match, Round gate verified 1-based aligned; NOTEs fixed (XY-plane comment); mass-absent guard + s2a/bFa documented OPEN. Node 94 GREEN, combat/ai/spatial diff 0, loop 13/13 + save/load PASS.
- Wea/TriggerBus/equip wave (2026-09-04): Bl.strike map (rest-length b, full-vector impulse, per-bone kb offsets + decay, wBa foe fallback, MG/NG OPEN); TriggerBus slots/fire/t0a/Axa/UKa-lY/Qh/ia-JNa/ModExpires-14/Provoke + perks.xml loader (_Var subst, Template merge) + equip mapping (list.xml Perks/Enchantments, ZOa at init, empty for Fists/Body/Head); SetHit-ppb bypass, Ly/jg state ordering, Lifesteal so-ratio, ChangeImpulse u.H=0 + set-semantics; qpb tactic switch; S17/S18/S18c goldens + ElementTree loader check; node 90 GREEN, combat/ai/spatial diff 0, loop 13/13 + save/load PASS.
- Gr-style + perks wave (2026-09-04): MoveDef::style_factor (RNa) parsed;
  StyleTable/StyleMeter + style_credit/style_vma/style_decay
  (TNa=0.5/tya=0.08/ZIa=2/SNa=1, JS L2090-2092), per-hit credit + b6 feed,
  bar-only decay in update_fighter (fixed an extra-brace build break in the
  decay hunk); core/scene/perks.hpp (31 Ma actions: 8 real + 23 no-op+log),
  decide_hit_perks pure + fight.cpp apply (SetHit/impulse/attrs/clears/
  lifesteal/dots), Fighter::clear_intervals, update_fighter DoT tick;
  S15+S16 goldens (node 81 asserts GREEN, combat diff 0); ai diff 0;
  loop 13/13 + save/load PASS; pose-vs-oracle still recorded-mismatch class
  (unpaired scenario, role-swap artifacts).

## Gate Phase 1 — по критериям (честно)

| Критерий | Статус |
|---|---|
| Два прогона → побитово идентичный JSONL | PASS (`a433d756` == `a433d756`) |
| `frame, phase, cf, chances, input_buffer_state, block_state, camera` — real | PASS (601/601, 0 ERRs) |
| `ai_branch, ai_zone, chosen_move` — real | FAIL (null/-1, 601/601 + 1202 записи пользователя: `ia:0, Pqb:0`) |
| `round_timer_xU` — real | FAIL (null; `iPa` не найден даже BFS-depth-4, в tutorial-HUD нет таймера) |

- BLOCKED (подтверждено данными пользователя 2026-09-04): tutorial-бой не
  содержит AI-решений вообще (`deCount:1`, скриптованный противник,
  `PhysicalDummy`-игрок, таймера нет). Критерии не занижены.
- Evidence (ignored, на диске): `console.user-play.log` (4.5MB, 2 прогона:
  played + idle), `recorded_inputs.txt` (`fd2f5b12…`), `console.replay1/2.log`,
  `oracle_replay1/2.jsonl` (`a433d756…`), `oracle_run3/4.jsonl`, `console.run*.log`.

## TODO

- Phase 2 — Gap inventory (CLOSED 2026-09-04 статически, см. ниже):
  - [x] AI-таблицы в web-ассетах — explicit вывод: ЕСТЬ, подтверждено по
    содержимому: `res/tactic_settings.xml` (21 564 Б в `xml.dat`, корень
    `<TacticsSettings>`, тактики `Standard/Test/NoTables/UseTables/Sensei/
    Lynx_Ranged/Shogun/…/Beginner/…`; парсер `P.Bmb` L626-629, `Md` L636-643)
    + `tactics/<name>.dat` / `tactics/<a>_<b>.dat` (лоадер `Si.cxb/dxb`
    L653-655, парсер `sb.load` L649-653, портирован в `ai.cpp`).
    Старый вывод «0 файлов» был по нативным ассетам и неприменим.
  - [x] §9-таблица `JS_GAMEPLAY.md` → задачи со СТАТИЧЕСКИМИ acceptance
    (JS-диапазон + чем проверено; AI/timer — Node-golden, не runtime-trace):
    - G1 гейт входа фазы 1: `ca.N0a` L426 + `llb` L429 → acceptance: порт
      имеет `WC`-буфер + flush при `Rkb`; проверка код-ревью + Node-вектор
      (`eu==1→WC`, `eu==2→yJa`).
    - G2 баннер→Next: `E3a/i4a` L412-413 + `vbh` cases 1/2/3/5 L410 + `Pf`
      L386 → acceptance: `apply_round_result` НЕ переходит сам, ждёт HUD.
    - G3 блок: `yD(5)` L553 + `wd.LAa` L536 + `hT(5)`+`Gc.DK` в `Cgb`
      L394-397 → acceptance: `Nbb`-гейт + блочный `LAa` + `hT(5)`.
    - G4 комбо: `HZa/tKa` L499-500 + `yD(4)/yD(6)` + `jga/iga` + `SZa`
      → acceptance: follow-up из стойки по комбо-связи, Node-вектор.
    - G5 AI 1:1: `de.Pqb` L604-608 + `dqb` L600 + `eh` + `Md` L637-638 →
      acceptance: Node-golden 0 расхождений (Phase 6 gate).
    - G6 таймер: `Sf.iPa` L2036 (`--xU`, `xU/60|0`, init `gma*60+1`) +
      `Ar.PEa` (`NF<=0`) L2020 → acceptance: целочисленный `xU` в порту,
      гейт конца раунда по `NF`.
    - G7 крит/шок: `R8a` L531-532 + `Orb/sr/threshold` L517 + `Wqb/kwb/Wx`
      L521/L528 → acceptance: порог, таймер возврата, подъём оружия.
    - G8 камера: `xCa=min(nC/(ECa+300),1)` L826/L831 + `dZa` L363-365 +
      `f3a` L367 + `d3a` L366-367 → acceptance: pose-compare gate Phase 4.
  - [~] Ссылки на строки JS в `core/` (механо-скан, 424 цитаты): ДВЕ конвенции
    (строки — валидны; смещения в символах в core/app — частично дрейфуют).
    Решение: НЕ чинить отдельным churn-коммитом; стандартизация на строки —
    по ходу Phase 3-6 в файлах, которых касаемся. 8 in-range подозрений
    (`v1`@L601-602 опечатка `V1`?; `bn`@L436; `p8a`@L605; `v.Lcb`@L604 —
    настоящий `Lcb` на L1204 = STALE; `yu`/`Lnb`@L803; дубликаты physics):
    чинить при работе с файлами.
- Phase 3 — Core loop & timing (G6 DONE 2026-09-04, build+regress green):
  G1 phase-1 buffer — pre-existing DONE (`WC` L648 + flush; untouched).
  G2 banner→Next — pre-existing DONE (`round_wait_`/`next_round_requested`;
  untouched). G6 integer timer — PORTED: `RoundState::time_xu/time_nf`
  (`--xU`, `NF=xU/60|0`, init `gma*60+1`, L2036), gate `NF<=0` (L2020),
  HUD/screens/demo/pose all read NF; float elapsed timer REMOVED (had no JS
  counterpart — `$t.time` write-once dead). 3.1 tick audit: headless forces
  1 fixed step/frame; timer cadence in log exactly 1/sec over 60-frame
  intervals → 60Hz confirmed. Regression: `--fight --headless 600` healthy
  (phase 2, AI acts, injected punch lands HighPunch, verify OK),
  `--headless-loop` 13/13 PASS + save/load PASS (both post-change binaries).
- Phase 4 — Spatial (DONE 2026-09-04, build+13/13 green):
  - [x] 4.1 spawn X: oracle phase=1,f=0 Me=972.954/-108.114, Enemy=690/-93.
    Live path (screens.cpp) already correct; fixed stale fallbacks
    (fight.hpp defaults + fight_controller_demo were mirror-swapped).
    f=0 identity check: P dx=1.1, E dx=2.9.
  - [x] 4.2 per-bone mirror: port formula `(px-com)*f+x` PROVEN ≡ JS
    (negate-buffer-then-shift with COM from negated buffer); PLUS fixed a
    real bug — swap was unconditional, JS swaps only on order disagreement
    (`Te.MYa`/`lwa`: world-order vs buffer-order; `prev_x_` kept as
    `ma`-analog). Skeleton core matches oracle at ~3u.
  - [x] 4.3 camera: pre-existing full port VERIFIED (Sya/dZa/Al/xCa/Bj,
    shake); init dcx=0.37, dzoom=0, clip-aligned dcy=3.8.
  - Directional numbers (settled idle, centered): P(+1)-vs-En 5.02 <10 ✓;
    E(-1)-vs-mirrored-En 5.95 <10 ✓ (negate path validated on real data).
    Key insight: align on SETTLED frames (oracle weapon nodes spring-settle
    cf≈1..10; cf=2 alignment caused the phantom 68-mean).
  - Gate verdict: PARTIAL PASS — numbers pass but the pre-fixed gate demands
    `compare_pose.py` on a PAIRED 500-frame round: impossible, scenarios are
    unpaired (port Training fight vs oracle tutorial; oracle Me is a 15-bone
    dummy forcing the tool's role-swap; official tool numbers 132/128/104/
    dcx184 are swap+scenario artifacts, NOT port errors — documented here,
    criteria NOT lowered). Residuals: head-cloth static ~40x (ragdoll layer),
    intro cy transient (phase-alignment), cf-phase mismatch.
  - Phase-6 leads (observed, out of scope): port enemy marches 690→1179
    (oracle holds 690→615) — AI advance/retreat differs; port enemy ATTACKS
    in phase 1 (DoublePunch F60-120) while JS gates AI to Je==2 (`Anb`) —
    real bug lead.
- Phase 5 — Combat (5.1 DONE 2026-09-04, build+13/13 green):
  Spec: helper `reference/COMBAT_STATIC.md` (§0 interval types … §5.1 block;
  harness `combat_golden.js` GREEN 51 asserts: S1-clean-hit, S2-blocked,
  S3-ignoreblock, S4-crit-disarm + selftest).
  Port: `blocked=false` hardcoded → WIRED: `Fighter::has_block` (`yD(5)`,
  L514) + `clear_block` (`hT(5)`, L554); `Interval.ignores_block` parsed
  (`Ul.J3` L774-775; 0 hits in shipped moves.xml — dead but faithful);
  `apply_hit` order = strike pre-break (`g.DDa`, L509) → `Nbb` → LAa
  (pre-existing `select_defense` order verified: KP[0]/pYa/capsule/lNa) →
  post-hit `hT(5)` when !blocked (`Cgb`, L394-397). crit still `false`
  (5.3); reaction-pick `Gc.DK` OPEN follow-up.
  Gate (event-flag match): `rec.blocked` plumbed (was constant false);
  runtime block EVENTS need a block-move occurring in-game (not observed in
  Training smoke — AI rarely blocks) → event-match pending 5.2/5.3.
  Regression: fight healthy + loop 13/13 PASS post-change.
- Phase 5 — 5.2 combos + phase-1 AI gate (DONE 2026-09-04, build+13/13 green):
  - `IgnoresBlock`/`IgnoresInvulnerable` are child ELEMENTS (159/154 live
    hits), not attributes — first parse caught 0; fixed to element parse +
    `|`-split bypass lists (`hga`/`iga`, COMBAT_STATIC §0).
  - `Fighter::has_invuln` (`yD(6)`); HZa chain gate in the hit path
    (L500-501: invuln target blocks unless `jga&&(iga empty||SZa)`).
  - `Cl.ia` one-shot `dW` (L566-567): per-attacker last-tested (move,iv)
    guard (`cl_last_`); `!aEa` empty-parts auto-connect (11/618 intervals).
  - Phase-1 enemy-attack bug (own lead) FIXED + verified: AI block now gated
    `phase==fight` (`Anb` Je==2, L499) — enemy holds stance through intro
    (was DoublePunch/ThrowForward at F60-120). Bonus: stops phase-1 QJa
    stream consumption (Da alignment).
  - Loop regression broke mid-turn (helper's zone-tab map rework moved
    fight nodes behind tabs; click missed → step 2 stall, NOT combat):
    fixed with tab pre-clicks in the driver (steps 1+11, still 13 steps).
    13/13 PASS + save/load PASS post-fix.
- Phase 5 TODO: 5.2 combos (`HZa/tKa`, `yD(4/6)`, `jga/iga`, `SZa`, L499-501);
  5.3 crit/shock/disarm (`R8a`, `Orb/sr/threshold`, `Wqb/kwb/Wx`,
  L517-532 + helper §S4 vectors).
- Phase 5 — 5.3 crit/shock/disarm + event flags (DONE 2026-09-04,
  build + both goldens + 13/13 green):
  - Real `hw` values resolved from `internal_settings.xml` `<Shock>`:
    threshold=999, Xza=0.001, MFa=12, crit/head Base=0.0001 (+attrs).
  - `ShockState` per fighter (`sr/vc/sn/Wx/ws`, L490) + free fns `orb_hit`
    (L517), `shock_tick` = Pnb decay + Wqb-fire (L528), `r8a_decide` with
    S8 decomposition (L531-532); `ShockConfig` on FightParams.
  - `apply_hit`: se via Lcb-exact (`a9>1` shortcut else fight-stream draw;
    `pga` OPEN→false path), R8a AFTER damage (Zi known), Ub→vc latch,
    disarm structurally Yi=false (no weapon items; Wqb body OPEN),
    `ep`/Dga (latched on UNBLOCKED only — cgbGuards transcription),
    `[hit]` event line (CRIT/SHOCK/BLOCK/FIRST).
  - Pnb ticked per fighter per tick in update_fighter.
  - C++ mirror in ai_golden: S5/S6/S8 vectors via the SAME free fns.
  - Combat golden verdict: 0 divergences (S5 pain/decay/veto, S6 Wx-fire,
    S8 raw+decomposition+blocked). One harness bug caught (printf arg
    order) — fixed, port was right.
  - Runtime: BLOCK+FIRST flags observed live (idle Block intervals work);
    no CRIT/SHOCK in smoke (threshold=999, crit ~1e-4 — expected).
    Gc.DK reaction-pick + Wqb item-swap remain OPEN; `atk.so`=1.0 OPEN;
    RJa draws merged into fight stream (documented approximation).
  - Regression: fight healthy + loop 13/13 PASS post-change.
- Phase 5 — DK reaction-pick + Wqb + OLa (DONE 2026-09-04, build + goldens
  + 13/13 green):
  - `combat_decide.hpp` (new): `dk_partition`/`dk_tail` exact mirrors of
    Node S10/S12 (C++ golden: e/ukb/d-all, pkb-len, nsb, jja, ukb — 0 divs).
    Pkb/jJa/Nsb bodies stay OPEN (geometry+weight tails); roulette-inside-
    Pkb OPEN (no tactic weights at reaction time — runtime picks
    priority-first via `dk_partition` order = hb_ order).
  - `Fighter::try_react` (54 Hit-event moves in moves.xml): fall-preference
    on shock as MS/jJa proxy (exact MS mapping OPEN). Wired into apply_hit
    !blocked branch with hit ctx (last_hit_type/dist/health/roll01).
    Runtime: `Player -> TitanBlock`, `Enemy -> ..._PHYSICAL_FALL` live.
  - Wqb REAL (not stub): `weapon` identity on fighters (save→player,
    enemy Fists); Yi = Ub with sn/Au-vs-own gate; kwb arms Wx=MFa;
    Pnb-fire swaps to Fists + WeaponDamage=0 + vc; sr/Wx reset per round
    (`wI`); fling/Wsb/drop presentation OPEN. Unreachable in practice
    (threshold=999) but structurally complete.
  - OLa from data: `exp_for_level` parses character_progress `<Threshold>`
    (150/190/350/...) with 100 fallback; round-reset sr/Wx verified in JS
    (`wI`: Wx=-1/sr=0; vc/sn persist). Threshold lookup not yet live-fired
    (loop fights enemy-won) — first player win exercises it.
- Phase-6 leads still open: enemy phase-2 march to 1179 (oracle holds ~615;
  advance/retreat tuning, needs real-fight oracle to judge); `eval_random`
  stream threading; `xaa` Ju-horizon; `iwb` eh-reset; kJ-vs-Xh.
- Debt wave A (DONE 2026-09-04): `eval_random` threaded through
  `FightContext::roll01` (fight stream at 5 combat sites + AI controller
  owned stream in v1/nwa; `Da.cT` no-draw shortcut; probes/demos keep
  legacy); march re-measured post-gate (enemy retreats 690→546 like oracle
  in phase 1; phase-2 advance residual = tactic behavior, needs real-fight
  oracle). Deferred with reasons: `xaa` Ju-horizon (parser skips Ju frames
  — needs parser+model surgery), `iwb` eh-reset (mwb routing unconfirmed),
  kJ-vs-Xh (animation deep-end), `Gc.DK` reaction-pick (needs candidate
  infra — own wave), `Wqb` item-swap (no weapon items; stub).
- Phase 6 — AI 1:1 (IN PROGRESS 2026-09-04, static-first):
  - [x] `DaPrng` exact port (`Xx`+`Rk`, L2352/2366; anchor `B0(1)=1103527590`
    hand-verified; Node↔Python independent transcriptions 0 diffs).
  - [x] QJa 5-roll cache + discarded lead draw + `Mu`/`lN` + `$x=gfa+1` cache
    on enemy-anim change (`jwb` L596-597); `rua/caa/nG/pqa/mqa` cached-vs-fresh
    (L593-594); `Aea` draw consumed per `XAa` (L611; Ju-horizon OPEN).
  - [x] Node harness `reference/tools/ai_golden.js` (pure fns, L-cited) +
    Python cross-check 0 diffs (PRNG bit-exact, Gb 1e-12, zones 1-4 covered,
    roulette spread, QJa order, gfa/Aea trunc+1).
  - [x] C++ mirror `app/ai_golden` written (DaPrng+Gb vectors, scripted
    update() trace) — NOT COMPILED (no toolchain on this machine).
  - [x] TOOLCHAIN FOUND `D:\MicrosoftVisualStudio\2022\BuildTools` (MSVC 14.44;
    lesson: check CMakeCache + D:\ before claiming absence). Build clean,
    0 errors (only pre-existing C4458/C4099).
  - [x] GOLDEN PASS 2026-09-04: Node vs C++ 0 divergences (PRNG bit-exact
    incl. stream positions; Gb ≤1e-9 observed vs 1e-5 allowed; zones 1-4,
    roulette spread, QJa order, gfa/Aea trunc+1 all match). Decision trace:
    12/12 `Jab`/fk=6 end-to-end (slot→V1→roulette). Two transcription bugs
    caught by the harness itself (discarded lead jf; o1/q1 feed) — fixed.
  - [x] `mq()` semantic fix: o1/q1 = ABSOLUTE gd (not ratio — HealthFactor
    1/3 + EnemyHealthFactor -1/-3 authored for absolute; `gd/Zn` ratio exists
    separately in JS); xY = enemy move frame (kJ-vs-Xh residual OPEN).
  - [ ] Coverage gaps (honest, not gate-blocking): YAa/XAa/Gea record paths
    need real `tactics/*.dat` — files EXIST locally
    (`reference/www/res/tactics/*.dat`, gitignored) + `_.atf` in assets/.
    Next: C++ record-path smoke with real `_.dat` + Standard/Sensei.
    fk=3,4,7,8 have NO static assignment in JS — nothing to cover.
  - [ ] Phase 6 gate OPEN until: full build + `ai_golden` re-run green
    (done above this line) + record-path smoke + `--fight`/`--headless-loop`
    regression (below).
  - KNOWN stream divergences (documented, not silent): `eval_random`
    (conditions.cpp) uses private mt19937, not `Da.pg` (+ missing `a>b`
    shortcut) — thread DaPrng through FightContext later; `xaa` Ju-horizon
    (`b=Fl+b`) OPEN; `iwb` `eh=1` reset OPEN; demo/demos injecting mt19937
    keep override path (their outputs WILL shift vs old baselines — re-run
    13/13 + pose dump when toolchain exists).
  - (старое: наблюдаемые `fk`: -2,-1,0,1,2,5,6,9,10,11; 3,4,7,8 OPEN;
    harness-пины `mulberry32`/frozen `Date`; real-fight trace = Phase 8.)
- Phase 7 — Secondary: HUD/sound/effects/`MOa`. Gate: чек-лист.
- Phase 8 — Final regression: 500+ кадров, pixel-diff ≥ PRE-FIXED %%, suite 3-7.

- Phase 7b - Save system 1:1 (DONE 2026-09-04, build + unit + loop green):
  - `WarriorSave` += battles (+has/record), fights (Wins OPEN), quests,
    variables (+story_step/_$StoryTutorialStep accessor), map_focus,
    currencies (Count OPEN), resistances (attrs, certain); load+save all.
  - Envelope: base64 codec + zstd_compress + dual-format load() +
    export_sf2 (join separator OPEN). save() stays plain (compat).
  - Stream 3 symbols: story_step/quest_vars(variables)/battles/map_focus
    present; disciple = a fight name (Dojo_Disciple), covered by
    fights/battles records (not a save field).
  - Scratch crash diagnosis (CLOSED): save_unit_test 0xC0000409 was a TEST
    bug (asserted decode(export()) round-trip; export wraps raw XML while
    decode expects compressed payloads — asymmetric by spec). Fixed test:
    SAVE_UNIT PASS (all new fields + envelope + export shape).
- Phase 7f - ta.Ut loop param + Ju-horizon (DONE 2026-09-04, build +
  goldens + 13/13 green):
  - `play_music(track, loop=true)` honors the flag (was hardcoded looping).
  - Ju-horizon WIRED (was OPEN): Hu-frame-indexed outcomes — `TacticRow`
    gains `rda`/`hu_frames` (Rda was parsed-then-discarded), `TacticOutcome`
    gains `hu_index`; `xaa` picks `k=ju_frame_index(Fl,rda,count)` per row
    and drops waits beyond `Fl+Aea` (`r<=b`, L611). Aea draw position
    verified (for-init, pre-record-search — matches JS order).
  - Golden: ai_golden Ju vectors (k=3/0/-1/-1/-1, horizon T/F/edge) Node==C++
    0 divs. Wea-per-label target still OPEN (needs enemy bone feed).
- Phase 7e - Exact bzb/lXa + SHOW_BLOCK note (DONE 2026-09-04, build +
  goldens + 13/13 green):
  - Replaced the disproven additive model with verbatim `Fh.lXa`
    (L2054-2056): Rva/PY/OY/P3/ep/Ui/DZ/Ub/m6/mOa, `Vk(a,b=0)=
    ceil(a/10^(kq-b))` (kq absent -> 0), pk EAa order; Fh fresh per battle
    (`v.kD(new Fh,...)`); c6/e6 via strike flags (first-hit->cvb/p1a,
    shock->yvb/P1a); d6=0 at reward time (gXa only at victory-after);
    jU never set (0); b6 style untracked (0). S13 retired, S14 golden:
    fresh=70, c6=2->350, e6=1->280, kq=1->7 — Node==C++ 0 divs.
  - Verified live: Bosses loss computes total=350 (c6=2 enemy first-hits ->
    ep=280, JS-faithful) but applies nothing; Training win applies 0
    (base 0). No money drift (425 stable across the run).
  - SHOW_BLOCK (Stream 2 resolved): `EStoryTutorialShowBlock` is a quest
    EVENT class (Fo), zero variable writes in JS — scene needs nothing;
    `block_lesson` must derive from quest-step position (Stream 3).
- Phase 7d - Prize completion + music (DONE 2026-09-04, build + goldens
  + 13/13 green):
  - Prize: `gems_bonus` (`hj.Uo`) field flows end-to-end (always 0 — no
    fight gem source evidenced; applied to Bonus); `prize_coins_bonus()`
    free fn + S13 golden (Node prizeCoins vs C++, 0 divs); style stays 0
    (Gr stats unported); base scaling xya/bm OPEN (D0 tables unparsed —
    port applies factors onto battle base reward).
  - Music streaming: `AudioEngine::play_music/stop_music` (disk-streamed
    mp3, looped, same-track no-op, headless-safe counting); Dojo -> `menu`,
    fights -> stages.xml Battle Music (`fight1_samurai_spirit` verified
    live on Bosses), battles without Music keep current (Training dummy),
    Results stops (no stinger files on disk — documented approximation).
    www/res ogg/m4a NOT wired (no AAC decoder). Loop log: menu, fight1,
    stop, music=2, zero load failures.
  - Ju-horizon cost note (NOT started): parser flattens Hu frames into
    per-row outcome lists (ai.cpp); the `b=Fl+b` horizon needs
    Hu-frame-indexed outcomes (Il/Ju/Gu model) = parser + TacticRow surgery
    + xaa rewire + re-golden. Half-turn job, queued behind Gr-style work.
- Phase 7c - Rewards after battle (DONE 2026-09-04):
  - Prize stats in apply_hit (combo/shocks/first-striker),
    `BattlePrize::prize()` (Perfect 5 / FirstStrike 2 / Combo 1 / Shock 3 /
    style 0; combination arithmetic OPEN), FightScreen bonus + log,
    ResultsScreen battle-win + fight-wins records. OLa stays 100xp
    (character_progress thresholds OPEN).
  - Runtime: prize lines live; loop fights enemy-won (pre-existing
    dynamics), so win-records verified by unit test instead of live win.
  - Debt Wave A also done: `eval_random` threaded
    (`FightContext::roll01`, 5 fight sites + AI v1/nwa owned stream).

- Phase 7d - Win-recording live + quest feeds scoped (DONE 2026-09-04):
  - Battles/iF win recording VERIFIED LIVE: loop Training win
    (winner=Player, prize combo=1 bonus=1) -> `[result] battle record:
    Training` -> save.xml shows `<Battle Name="Training" />` +
    `<Fight Name="Training" Wins="3" />`, money 422->423. WDa stub can go
    live on `has_battle` (Stream 3 map side).
  - Quest feeds: scene/data scope has NONE (verified by grep — only
    false-positive `requested`). Feeds live in app scope: quest_panel.hpp
    structures them (`story_step` passed through; `boss_focus`/
    `block_lesson` fields exist), Dojo feeds `story_step` (screens.cpp).
    EXACT missing piece (Stream 3): extend `quest_state_for()` with
    `(map_focus, battles, variables)` and set `boss_focus` (MapFocus
    contains BOSS_LYNX) / `block_lesson` (quest-var name for the
    SHOW_BLOCK beat still UNCONFIRMED — needs quest research, do NOT
    guess); update the 2 screens.cpp call sites. All save fields they
    need already land (`map_focus`, `battles`, `variables`).
  - Regression this wave: loop 13/13 PASS + save/load PASS (post-change
    tree was already green; no repo files touched this wave except this
    file).


## FINAL CONSOLIDATION (2026-09-04, Stream 1 scene/data duty)

Wave hashes (all local, main branch):
- `926652e5` Gr-style meter + Ma perk actions + S15/S16
- `57408389` Bl.strike physics + TriggerBus + equip + S17/S18
- `418b4af1` REVIEW_PHYS_TRIG (docs)
- `43964804` review HIGH/MED/LOW fixes + S17b/S18 (kp, ModFlag, Bz)
- `0c5e6fb6` ranged/magic + perk leftovers + S19
- `74a48cdd` modes/export + S20 (amended from df288814)

Regression table (final tree, Release):
| Check | Verdict | Numbers |
|---|---|---|
| clean Release build (all targets) | PASS, 0 errors | warnings only (pre-existing demo-TU notes) |
| Node combat_golden | GREEN | 99 asserts, 0 fail |
| combat_diff (Node==C++) | PASS | 0 divergences (S5-S20 incl. loader ElementTree checks) |
| Node ai_golden + ai diff | PASS | 0 divergences |
| Node spatial_golden | GREEN | 17 asserts |
| --headless-loop | PASS | 13/13 + save/load (money 425, items 7, WEAPON_KNIVES) |
| --fight --headless 700 --dump-pose 600 | healthy | 600 frames, phase 1->2, timer 99->91, clean shutdown |
| pose compare | recorded-mismatch class, deterministic | 232 matched, 38.7%/66.3%, dx 384.790 (identical to baseline) |

FINAL acceptance (pre-fixed criteria) — honest marks:
1. 500+ frame AI-fight trace frame-to-frame, 0 divergences — NOT MET (unpaired scenarios by construction; oracle tutorial vs port Training; intro aligns 232 frames, behavior diverges after; recorded in MASTER_TODO Phase 4 gate as PARTIAL PASS with directional numbers 5.02/5.95).
2. Pixel-diff fighters vs oracle >=90% — NOT MEASURED (no pixel harness exists; R8 resolved Wins/Count absents only).
3. Dojo->Fight->Results playable happy path — PASS (loop 13/13 covers it end to end + save/load).

Remaining OPENs (with reasons):
- Vc.sk ragdoll integrator (gravity/friction/MG/NG/cA/bFa/s2a) — needs runtime trace; knockback approximated via offsets+decay.
- Magic visuals/projectile flight, yd/yp event plumbing, U4 pulse, buttons (wKa/b5), darkness overlay, MoveModel lerp, StealMagicMod (no magic/bullet systems beyond bh/dO/my).
- Qa full engine (only kp comparisons + constants implemented); expression Frames/Value fall back.
- Enemy Template ur stat-merge, AttributesAlign application, Eclipse Cea setter.
- Map/UI side: entry energy (qZa), repeat caps, intros (hCa), PF lists (vJa), quest counters (Fsb), stages rule plumbing beyond Fight.
- Dpb string-length vs ke(compressed) latent game inconsistency (ours uses compressed).
- Mass-absent weight divergence (JS inf vs 1.0 guard) — shipped .dat presence unverified.
- Phase-6 AI leads (enemy phase-2 march, phase-1 attacks gated by Je==2 in JS).


## UI-GATE (Dojo wave, 2026-09-04)

Harness: `reference/tools/ui_diff.py` (% pixels over thr 12 + red overlay +
side-by-side into `reference/traces/ui/`); port `--ui-tour` (9 states,
`reference/traces/ui/port_<name>.png`); oracle `shot` verb + tour scripts
(`reference/tools/ui_tour_oracle.txt`) — ORACLE UNRUNNABLE HERE (WinForms
form closes instantly, zero output; no interactive desktop/WebView2).
Only pre-existing oracle pixels available: `boot.png` (tutorial fight).

Baselines (% over thr12; port post-Dojo-fix, fresh save):
| Screen | Oracle counterpart | Baseline | Note |
|---|---|---|---|
| dojo | NONE (oracle boots to tutorial fight, not a hub) | n/a (structural gate) | pre-fix viewed: 3 hint rows mutually overlapped, labels full-width |
| tut_fight (oracle boot) vs port fight | different mode, informative only | 90.07 | phase-1 tutorial vs phase-2 training; joystick vs key bar |
| map/shop/profile/results/pause/settings | none captured | n/a | need interactive oracle session |

Thresholds (proposed BEFORE remaining fixes; no post-fitting):
- Dojo: STRUCTURAL 100% — every label through `draw_ui_label` with its
  visible rect (grep: zero `draw_text(...,1.0f-scale)` left in hub paths);
  hint/modal exclusive (modal wins); verified by capture read. Pixel gate
  PENDING oracle hub (no counterpart exists at boot).
- Fight/map/shop/profile/results/pause/settings: pixel threshold proposed
  AFTER first oracle run, BEFORE fixes: 25% for design-close screens
  (fight HUD), 40% elsewhere; no screen may regress its own baseline.
- Quest-step matrix: open (fresh-profile states only this wave).

Dojo verdict: PASS (structural) — hint rows non-overlapping + readable,
buttons/disciple/setup fitted+centered, modal fitted, atlas telemetry
(`btn_back` the only miss; Dojo/Map/Shop/Profile frames resolve).
Pushed per-screen per user order.
Dojo fidelity wave (2026-09-04 Stream 1 Waves 0-6): PASS structural - tour 13-13 loop 13-13 golden 0. Miss btn_back only. HUD uses level+Level_bar not star. Pixel gate FIRST MEASURED Wave 6 2026-09-04 (numbers below; 25% fight-design-close bar NOT MET on first measurement — recorded, not fitted).
- Results/Pause/Settings PASS + PUSHED: fitted titles/labels/buttons; pause menu labeled (was flat-only); settings BACK debounce (push-frame held-click race); modal/title/quest-toast fitted.
- Map PASS + PUSHED: tabs/series/nodes/badges/BRACKET/BACK fitted via draw_ui_label; act-overlay/modal exclusive (modal wins); BOSS overlay fitted. - Shop PASS + PUSHED: tabs/cards/markers/wallet/delivery/wielding/BACK fitted; Dojo chrome top-gated (disciple/SETUP no longer leak onto Shop); modal top-gated (single instance on layered stack).
Dense-node label collisions remain (nodes ~60px apart, data-driven; needs leader-line layout, follow-up).
- Profile PASS + PUSHED: header/slots/cards/dline/MOVES/BACK fitted; slot+card labels added (were flat-only). Hint-bar-under-profile is the allowed global overlay (dimmed).
- Wave 6 dojo pixel gate FIRST MEASURED (2026-09-04): oracle_dojo.png present
  (1285x733 dirty screen-skim). Normalized read-only to oracle_dojo_norm.png
  (crop L4/T5/R1/B7 -> 1280x721, LANCZOS to 1280x720; original untouched).
  Fresh --ui-tour port capture (tour 13/13, exit 0, rebuilt HEAD df1e073b).
  ui_diff thr12: dojo = 84.90% over-threshold (pct>40: 69.68, pct>80: 54.04).
  Grid (8x4 pct>12): top half matches closely (y0 means O~76-114 vs P~77-112,
  oracle bright bands x240-720/x1120-1280 reproduced within ~10-15 units);
  residual = port lower half dark (port dark-frac y3-y5 0.80-0.87 vs oracle
  0.11-0.60; oracle full-height interior mean ~95-155 rows 300-570, port ~0-40
  below y~384 — dojo fit framing floor band lands y~295-333, layers below are
  portrait-aspect and never cover the lower half; port_fight.png shows the same
  dark-floor shape, so it is structural, not a dojo-only regression).
  Verdict: 25% fight-design-close bar NOT MET on first measurement; dojo
  STRUCTURAL gate (Waves 0-5) still PASS and unchanged; no fix churn this round
  (fix rounds 0/3 used). Follow-up OPEN (not gate-blocking): full-height dojo
  interior framing (portrait-aspect layer projection below floor band) +
  fight-floor fill; needs layer-projection work in screens.cpp, queued.

## FINAL — 1:1 playable build matching the oracle

Последний пункт всего плана. Acceptance (фиксировано заранее):
1. Детерминированный полный раунд (настоящий бой с AI, 500+ кадров):
   `round_timer_xU`, переходы фаз и event-флаги (`hit/block/crit/disarm`)
   порта совпадают с oracle-трейсом кадр-в-кадр, 0 расхождений.
2. Pixel-diff скриншотов порта vs оракула на фиксированном разрешении ≥ 90%
   (порог зафиксирован здесь, не подгоняется постфактум).
3. Полный цикл Dojo→Fight→Results проходим в порту (playable, без заглушек
   на happy path).
