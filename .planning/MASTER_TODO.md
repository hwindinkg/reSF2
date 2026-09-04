# reSF2 — Master TODO (web-only, 1:1)

Единственный оракул: reference/www/sf2.502f0946.js + shell/OracleShell.
Всё из .planning/archive/ — недействительно как референс.

## Phase 0 — Hygiene
- [x] grep-очистка старых референсов (2026-09-03; gate ниже)
- [x] .planning/archive/ создан, баннеры добавлены (2026-09-03)
- [x] PROJECT.md переписан (2026-09-03)
- [x] MASTER_TODO.md создан (этот файл, 2026-09-03)
- Gate: `git grep -E 'Marmalade|ShadowFight2\.s86|libil2cpp|IL2CPP|\.dz\b|DZ archive|ARM:LE|GHIDRA_MCP' -- .`
  пуст вне `.planning/archive/` и самого оракула `reference/www/*.js` (там `.dz`
  — имя минифицированного поля). PASS 2026-09-03.
- Notes 2026-09-03:
  - `core/scene/ai_controller.cpp`: убран комментарий со ссылкой на ARM-бинарник
    (заменён на пометку "pending oracle-trace verification, Phase 6").
  - `core/scene/fight.cpp`: обрывков старого подхода нет (ARM-адреса/DZ/engine/
    инклюды отсутствуют); слово "native" там = "наш C++ порт" (vs oracle),
    не Marmalade-билд. Терминологическая чистка — опционально, не блокер.
  - `.gitignore`: удалены stale-записи `tools/Marmalade-Modding/`,
    `tools/S3ELoader/`, `tools/dzip.exe`, `tools/extract_dz.dcl`, `tools/il2cpp/`
    (каталога `tools/` в репо нет).
  - `.planning/STATE.md`: вычищены 4 записи native/Unity-RE истории 2026-07-30
    (сохранены в git-истории).
  - Проверенные ложные срабатывания (не требуют действий): `MA_ARM`/`__arm__`
    в `core/data/third_party/miniaudio.h`, `stb_image.h` (архитектурные макросы
    вендорной аудиобиблиотеки); `ARMOR` в `core/app/screens.cpp` (игровой термин
    "броня"); `0x1000`-подобные hex-константы third-party кода.
  - `.planning/headless-integration-test-*.md` оставлены на месте (вне скоупа
    брифа; содержат ссылки на удалённый `engine/`, но ни одного совпадения
    gate-паттерна). Перепроверить/архивировать при первой зацепке.
  - Игнорируемые (untracked) каталоги `.codebase/`, `.codegraph/`, `.qoder/`,
    `backup/`, `assets/`, `reference/www/res/` содержат артефакты старой эпохи,
    но не входят в коммитимый `main` (code-only, 152 файла) — gate проверен
    через `git grep` по трекаемым файлам.

## Phase 1 — Oracle instrumentation
- [ ] AI/timer/input/camera трейсинг добавлен в OracleShell
  (`reference/tools/trace_oracle.*`, формат в `reference/traces/README.md`)
  - Wave 1 (2026-09-04): `trace_oracle.js` + `input_phase1.txt` написаны; shell:
    инъекция через AddScriptToExecuteOnDocumentCreated, fresh profile/ран,
    auto-close по __oracleDone/150s timeout. Runs → Wave 2.
  - Wave 2 (2026-09-04): infra PASS (0 tracer ERRs; `extract_oracle.py` работает;
    page-side `atframe`-стимул кадр-в-кадр; хук-отчёт в header честный).
    Gate: FAIL — см. BLOCKED ниже. Коммит wave-2 (infra + evidence, без gate).
- [ ] Детерминизм подтверждён (2 прогона одного `--input-script` идентичны побитово)
  - PARTIAL 2026-09-04: два прогона drench-скрипта идентичны побитово на строках
    0..366 (367 записей: фазы, cf, camera-floats, chances, block states).
    Первое расхождение на строке 367 (`f≈366`): лишний button-tap `N0a`
    (control 6) в одном прогоне. Стик-drags регистрируются 1:1; button-taps
    gated/flaky (зависит от wall-clock/tutorial-gating, не от кадров).
  - run3 `75348487f8f139fc8b93c7353e21ba4f6e7ba9cafdd022b8c9ebf97037b52ec`
  - run4 `a7a11c1b165fae2ee6d731b2209fb224085dad823781e83259a98043ae356e95`
- BLOCKED 2026-09-04 (gate Phase 1 не закрыт, критерии не занижены):
  причина — автодостигаемый dojo-tutorial бой НЕ СОДЕРЖИТ AI-решений вообще:
  `trace_stats {ia:0, Pqb:0}` на ~1600 суммарных fight-кадрах, `deCount:1`
  (BFS всего fight-графа — скрытых de нет), `chosen_move` null в 601/601,
  `ai_branch`/`ai_zone` заморожены на init (-1/1), `iPa` не найден даже
  BFS-depth-4 (в tutorial-HUD нет таймера раунда — `round_timer_xU` null
  в 601/601), игрок — `PhysicalDummy` (неподвижен под 41 стимулом:
  N0a-события доходят, эффекта нет; урок-1 «движения» не завершается —
  вероятно нужен длинный hold, а не 7-8-кадровые blips).
  Следующий путь (не в Phase 1): (a) long-hold стимул (`hold X Y LEN`) для
  завершения lesson-1; (b) навигация меню→настоящий бой с AI (flow неизвестен);
  (c) drag-only скрипт для чистого детерминизм-прогона без flaky taps.
  Evidence: `reference/traces/oracle_run3.jsonl` / `oracle_run4.jsonl`
  (ignored, на диске) + `console.run*.log`; формат: `reference/traces/README.md`.
- Prep wave (2026-09-04, перед ручным прохождением tutorial пользователем):
  - Track A (replay-readiness): PASS (механика). Резолв input-пути по JS:
    `Df{control,index}` + `Gfa()`-тип (L453), `Za` гейты `DEa` (L459),
    `Nc.Lh` роутинг (L461), стик-сектора `ze.GBa` 1-8 (L463-471) — координат
    после мэппинга нет, записи (control,index,type) достаточно для точного
    реплея. Добавлены: `O0a`-враппер, `[INPUT-REC]`-запись эффективных
    вводов, `atframe press/release` + page-side прямой реплей через
    `fight.N0a/O0a` (минуя DOM и `DEa`, детерминированно),
    `record_inputs.py`, `input_smoke.txt`. Smoke PASS: 4/4 press/release
    в точных кадрах (150/160/200/210), 0 ERRs, round-trip побайтово точен.
    Статика input-пути: `reference/AI_STATIC.md` §6.
  - Track B (static AI): `reference/AI_STATIC.md` написан — полное дерево
    `de.Pqb` (зоны/fk/шансы), `Md`/`cc.Gb`-формула, PRNG `Da/Xx` + все точки
    сидирования, файлы значений (`res/tactic_settings.xml` +
    `tactics/*.dat`), input/tick/таймер линии. OPEN-пункты помечены
    (нужен runtime trace): fk=3/4/7/8, ResponseDelay-консумер, часть
    `RJa`-контекстов, id→file для `Ja.ki(272/1314)`.
  - READY FOR USER PLAYTHROUGH: инструкция в `reference/traces/README.md`
    (запуск exe → играть → `[INPUT-REC]` в console.log →
    `record_inputs.py` → replay дважды → сравнение sha).
- Takeover fix (2026-09-04): запуск С `--input-script` инжектил 41 стимул
  из `input_phase1.txt` («играла сама») + auto-close на f=600 — для ручной
  игры непригоден. Manual record mode: запуск БЕЗ `--input-script` —
  ничего не инжектит, auto-close отключён (окно закрывает только пользователь),
  физическая клавиатура/мышь идут в игру 1:1 (WebView2 форвардит нативно,
  shell не ставит DOM-перехватчиков), трейсинг + `[INPUT-REC]` активны.
  Проверено: 35с headless-запуск без скрипта → 601 oracle-запись (f 0..600),
  0 инжектов, 0 INPUT-REC, процесс жив до kill. Команда пользователю:
  `& ./shell/bin/Release/net9.0-windows/OracleShell.exe` (кликнуть по окну
  игры для фокуса). Невоспроизведённые нажатия ранее — гейты уроков `Za.DEa`,
  не перехват с нашей стороны.
- Gate (фиксирован заранее): два прогона одного скрипта дают идентичный JSONL;
  в трейсе реально присутствуют все поля
  (`frame, phase, cf, ai_branch, ai_zone, chosen_move, chances{...},
  input_buffer_state, round_timer_xU, block_state, camera{cx,cy,zoom}`) — не заглушки.

## Phase 2 — Gap inventory
- [ ] §9-таблица `reference/JS_GAMEPLAY.md` разбита на задачи с acceptance-критериями
  (acceptance = какое поле oracle-трейса из Фазы 1 должно совпасть)
- [ ] Ссылки на строки JS в `core/` перепроверены против текущего `sf2.502f0946.js`
- [ ] AI-таблицы в web-ассетах: найдены/не найдены — explicit вывод
  (старый вывод "0 файлов" был по нативным ассетам и неприменим; веб грузит
  через `xml.dat`/`animations.*.dat`, см. `core/data/README.md`)
- Gate: issue-лист с acceptance-критерием на каждую строку + explicit ответ по AI-таблицам.

## Phase 3 — Core loop & timing
- [ ] 3.1 fixed-tick 1/60 (`Us`: `Gy=.0166667`; не путать со старым "16ms integer step" из ARM)
- [ ] 3.2 целочисленный таймер раунда (`xU`, `xU/60|0`, гейт `Ar.PEа`/`NF<=0`)
- [ ] 3.3 input buffer фазы 1 (`Wc/llb`-аналог)
- [ ] 3.4 баннер/пауза между раундами (`EndStance` → баннер → кнопка Next, `vbh` case 1/2/3/5)
- Gate: полный раунд (500+ кадров) под `--input-script`; `round_timer_xU` и переходы
  фаз совпадают с oracle-трейсом кадр-в-кадр (0 расхождений на этих полях).

## Phase 4 — Spatial
- [ ] 4.1 spawn X (из oracle-трейса на `phase=1, cf=0`)
- [ ] 4.2 per-bone mirror (`x = -x` по кости для `fx=-1` перед мировым смещением)
- [ ] 4.3 camera zoom-by-distance (`xCa() = min(nC/(ECa+300), 1)` + интро-панорама/виньетка если есть в JS)
- Gate: `compare_pose.py --coord-transform center` на полном 500-кадровом раунде:
  bone mean < 10 юнитов, facing mismatch = 0%, `|dcx|`/`|dcy|` mean < 5.

## Phase 5 — Combat
- [ ] 5.1 block/parry (`yD(5)`, `wd.LAa`, разрывы интервалов блока, `hT(5)` → `Gc.DK`)
- [ ] 5.2 combos (`HZa/tKa`, `yD(4)`, `yD(6)`, `jga/iga`, `SZa`)
- [ ] 5.3 crit/shock/disarm (`R8a`/`v.Ub.threshold`, `sr`, `Wqb` + `kwb/Wx`)
- Gate per wave: заскриптованная последовательность вводов даёт идентичные события
  (`hit/block/crit/disarm`) в трейсах порта и оракула (сравнение флагов событий, не поз).

## Phase 6 — AI 1:1
- [ ] дерево решений `de.Pqb`: зоны `dqb/aqa` (1-4), ветки `fk` (0-11),
  `ResponseDelay/EnemyResponseDelay`, шансы, `Memory/Strikes`
- [ ] реальные AI-таблицы подключены (по результату Фазы 2.3); убрать рандомные веса
  (`ai_controller.cpp` — приближения помечены "pending oracle-trace verification")
- [ ] PRNG `Da.jf()` сверен (или задокументирован фиксированный сид + ассерт на распределение)
- Gate: при одинаковом фиксированном сиде `ai_branch`/`chosen_move` совпадают
  кадр-в-кадр на прогоне из Фазы 1.

## Phase 7 — Secondary systems
- [ ] HUD-полосы статов (с утечкой/decay как в JS), таблицы звуков,
  effect-контейнеры (`cv`, `magic/*.json`), реген спецприёмов (`MOa`)
- Gate: чек-лист присутствия/поведения каждой системы, сверенный с оракулом.

## Phase 8 — Final regression
- [ ] 500+ кадров полного раунда (реального, не idle/интро)
- [ ] pixel-diff против скриншотов оракула на фиксированном разрешении ≥ __% (порог
  зафиксировать ДО прогона, не подгонять постфактум)
- [ ] regression-suite Фаз 3-7 разом
