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
- Phase 3 — Core loop & timing: 3.1 fixed-tick 1/60 (`Us`, L135 ✓ статика);
  3.2 целочисленный таймер (`Sf.iPa`, L2036); 3.3 `WC`/`llb` буфер фазы 1
  (L426-429); 3.4 баннер между раундами. Gate: раунд 500+ кадров,
  `round_timer_xU` + фазы кадр-в-кадр (нужен real-fight трейс).
- Phase 4 — Spatial: 4.1 spawn X (phase=1,cf=0: Me x≈972.95, Enemy x≈690 —
  из oracle_pose); 4.2 per-bone mirror; 4.3 camera zoom. Gate: bone mean<10,
  facing 0%, dc<5.
- Phase 5 — Combat: 5.1 блок (`yD(5)`, L514); 5.2 комбо (`HZa`, L500-501);
  5.3 крит/шок/дизарм. Gate: event-trace match.
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
  - [ ] Phase 6 gate OPEN: build+run `ai_golden`, diff vs Node (PRNG exact,
    Gb ≤1e-5), full-tree branch coverage. BLOCKED: no msbuild/cl here.
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

## FINAL — 1:1 playable build matching the oracle

Последний пункт всего плана. Acceptance (фиксировано заранее):
1. Детерминированный полный раунд (настоящий бой с AI, 500+ кадров):
   `round_timer_xU`, переходы фаз и event-флаги (`hit/block/crit/disarm`)
   порта совпадают с oracle-трейсом кадр-в-кадр, 0 расхождений.
2. Pixel-diff скриншотов порта vs оракула на фиксированном разрешении ≥ 90%
   (порог зафиксирован здесь, не подгоняется постфактум).
3. Полный цикл Dojo→Fight→Results проходим в порту (playable, без заглушек
   на happy path).
