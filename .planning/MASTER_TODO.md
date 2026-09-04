# reSF2 — Master TODO (web-only, 1:1)

Единственный оракул: reference/www/sf2.502f0946.js + shell/OracleShell.
Всё из .planning/archive/ — недействительно как референс.
Политика пуша: только локальные коммиты до слова пользователя (2026-09-04).

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

- Phase 2 — Gap inventory (UNBLOCKED, можно делать сейчас):
  - [x] AI-таблицы в web-ассетах — explicit вывод: ЕСТЬ (`AI_STATIC.md` §1:
    `res/tactic_settings.xml` в `xml.dat` + `tactics/<name>.dat`,
    `tactics/<a>_<b>.dat`; тактики `Sensei`/`Beginner`/…; лоадер `Si`,
    парсер `P.Bmb`/`Md`, таблицы `sb`). Старый вывод «0 файлов» был по
    нативным ассетам и неприменим.
  - [ ] §9-таблица `JS_GAMEPLAY.md` → задачи с acceptance-критериями
    (acceptance = поле oracle-трейса; AI/timer-поля — после разблокировки
    gate: помечать `pending-real-fight`).
  - [~] Ссылки на строки JS в `core/` перепроверены против текущего JS
    (старт 2026-09-04, механо-скан `lineref_check.py`, temp/вне репо):
    424 цитаты; ДВЕ конвенции — номера строк (core/scene AI/fight, валидны)
    и СМЕЩЕНИЯ В СИМВОЛАХ (core/app + часть scene/README, напр. `Pa.iwa`
    @629626 сходится; часть нет — перепроверить): 23 offset-цитаты
    стандартизировать на строки по ходу Phase 3-6 (не чёрнить сейчас).
    8 in-range подозрений на ревью: `v1`@L601-602 (вероятно опечатка `V1`),
    `bn`@L436, `v.gya.p8a`@L605, `v.Lcb`@L604 (настоящий `Lcb` на L1204 —
    похоже STALE), `yu`/`Lnb`@L803 (×2: hpp + README), + `physics.*`
    дубликаты. Чинить по мере работы с файлами, не отдельным churn-коммитом.
- Phase 3 — Core loop & timing: 3.1 fixed-tick 1/60 (`Us`, L135 ✓ статика);
  3.2 целочисленный таймер (`Sf.iPa`, L2036); 3.3 `WC`/`llb` буфер фазы 1
  (L426-429); 3.4 баннер между раундами. Gate: раунд 500+ кадров,
  `round_timer_xU` + фазы кадр-в-кадр (нужен real-fight трейс).
- Phase 4 — Spatial: 4.1 spawn X (phase=1,cf=0: Me x≈972.95, Enemy x≈690 —
  из oracle_pose); 4.2 per-bone mirror; 4.3 camera zoom. Gate: bone mean<10,
  facing 0%, dc<5.
- Phase 5 — Combat: 5.1 блок (`yD(5)`, L514); 5.2 комбо (`HZa`, L500-501);
  5.3 крит/шок/дизарм. Gate: event-trace match.
- Phase 6 — AI 1:1: дерево `de.Pqb` (наблюдаемые `fk`: -2,-1,0,1,2,5,6,9,10,11;
  3,4,7,8 OPEN), таблицы из Phase 2, PRNG `Xx` (L2366, glibc-LCG, портируем) +
  harness-пины (`mulberry32`, frozen `Date`). Gate: branch/decision match.
  Разблокируется real-fight трейсом с `ia>0`.
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
