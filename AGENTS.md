# AGENTS.md — правила работы в reSF2

Нативный C++17-порт Shadow Fight 2. **Эталон поведения — JS**
(`reference/www/sf2.readable.js`; шипнутые XML — только данные). Любое
утверждение о поведении подкрепляется `sf2.readable.js:СТРОКА`, а не «так
логичнее».

## 1. RULE 0 — железные инварианты прогонов

1. Гейты запускаются **скрыто** (`CREATE_NO_WINDOW`, driver-режимы), на
   переднем плане. **В одном рабочем дереве — строго последовательно**: раннер
   берёт OS-lock `.harness/lock`, а прогоны делят `.harness/artifacts/`,
   `reference/traces/*`, `reference/www` и сейв. **Параллельно — только в
   РАЗНЫХ worktree** (у каждого свой `.harness`, свои traces и сейв; лок тоже
   свой). Разрешение человека на параллель получено 24.10.
2. Никакого OS-ввода: только `App::inject_click` / `inject_key` (порт) и
   console/trace-хуки (оракул). Клавиатура/мышь машины не используются.
3. В каждом argv — `--watchdog N`. Внешний таймаут раннера убивает **только
   своё** дерево процессов (`taskkill /PID <наш> /T /F`). После прогона не
   остаётся ни одного процесса — это проверяется глазами и `Get-Process game`.
4. `reference/saves/save.xml` — сейв игрока. Раннер сам делает бэкап
   (`.harness/save.gates.bak`) и восстанавливает его в `finally`; изменение
   сейва гейтом = строка `SAVE GUARD` в отчёте.
5. `reference/www/` — эталон: раннер снимает слепок трёх файлов и
   восстанавливает их после прогона (OracleShell копирует туда `trace.js`).

## 2. Порог не понижается

`harness/gates.baseline.json` — зафиксированные числа. `now > baseline` =
**REGRESSION** (exit 1). Понижать baseline молча запрещено: только
`--update-baseline --only <id> --reason "почему" --yes`. Улучшение
(`now < baseline`) показывается отдельно и exit не ломает.

**Стоячее разрешение человека (24.10):** обновлять baseline после
подтверждённого улучшения можно самому, если в `--reason` указаны число
«было → стало» и чем измерено; `--yes` в этом случае ставит агент. Каждое
понижение — отдельный коммит с причиной.

## 3. Билд (проверено 2026-10-20)

`cmake --build build --config Release` **НЕ работает**: кэш указывает на
`D:/MicrosoftVisualStudio/2022/Community`, инстанс не зарегистрирован в VS
Installer. Рабочий рецепт (его и использует раннер, cmake пробуется первым и
падает за 1.5 с):

```powershell
& "D:\MicrosoftVisualStudio\2022\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" `
  build\sf2_native.sln /p:Configuration=Release /m /nologo
```

Время: ~19 с без изменений, ~3 мин на пересборку `app/game/main.cpp` + линк.
Раннер пропускает билд, если `game.exe` новее всех `core/**` и `app/**`
(mtime-шорткат; `--force-build` — принудительно). Если `shell/*.cs` новее
`OracleShell.exe`, раннер пишет предупреждение — пересобрать шелл:
`dotnet build shell/OracleShell.csproj -c Release`.

## 4. Гейты — единственный вход

```powershell
python harness/gates.py --fast     # билд + verify-input, flow-verify,
                                   # save-fields-probe, settings-profile-shop-probe
python harness/gates.py --full     # 16 флаг-гейтов + 6 diff-сценариев (~13 мин)
python harness/gates.py --only diff:survival
python harness/gates.py --list
```

Exit: `0` всё ок (или только улучшения), `1` регресс/ложный PASS/таймаут/
нет входа/гейт без baseline, `2` ошибка окружения (нет exe, нет трейса оракула).

Диффы (`diff:<scenario>`) — оракул ↔ порт, кадр в кадр:
`dojo-demo`, `boss-lynx`, `tournament`, `survival`, `challenge`, `dojo-move`.
Оракул запускается `OracleShell.exe --query … --input-script …`, извлекается
`reference/tools/extract_state.py` (+ `extract_layer.py ui`), порт —
`game.exe … --dump-state/--dump-ui`; артефакты складываются в
`.harness/artifacts/<scenario>/`. Читается **число** (`total divergences: N`),
а не exit-код «лгущих» компараторов; отсутствие ожидаемой строки = FAIL.

Позиции: абсолютный `x` оракула — это поза тела со Shift локации, у порта —
логический `x` (смещение константно и одинаково для обоих бойцов; измерено:
dojo 0.000, moon 0.598, arena/autumn 22.211). Считается инвариант
`fighters.sep_x` (расстояние по x); `fighters.*.x` — INFORMATIONAL, и только
для стоящих бойцов (`rag == 0`): у рэгдолла кости сравнивает `compare_pose.py`.

## 5. Дисциплина

- **Один баг = одна ветка (worktree) = один прогон `--full` = один коммит.**
- **Суб-агенты обязательны там, где работа делится.** Исследование (чтение
  эталона JS, карта порта, поиск причины) — параллельно через `subagent`
  (`explore`, 2–4 задачи за раз): это дешевле и быстрее, чем последовательные
  grep/read. Сборки и гейты — **только последовательно** самим агентом (RULE 0,
  п.1): суб-агенту их не отдавать. Результат суб-агента — гипотеза: перед
  правкой проверить строку в `sf2.readable.js` или замером.
- Правишь `core/`/`app/` — плагин `.opencode/plugins/gate.ts` сам прогонит
  `--fast` и вернёт `GATE FAIL:` в том же ходе. Зелёный — тишина. `/gate` —
  полный набор.
- Не трогать `reference/www/*` (эталон) и `reference/saves/save.xml`.
- Нашёл баг по пути — записать в `BUGS.md` (колонка `Gate id`), не править.
- `.harness/`, `reference/traces/`, `build/` — генерируемое, в git не идёт.

## 6. Stale-документы (НЕ источник истины)

`.codebase/*` (там описан несуществующий `tests/` с 15 exe), `.qoder/*`,
`.planning/archive/*`, `docs/*`, `worklog.md`. `.planning/STATE.md` полезен как
история, но билд-рецепт в нём противоречив (:32 cmake vs :188-190 MSBuild) —
верить этому файлу и разделу 3 выше.

## 7. Наблюдаемость (стоячее указание человека, 24.10)

- Дампы оригинала делать **сколько нужно**: каждый новый факт поведения
  сначала измеряется оракулом (`reference/tools/trace_oracle.js`, поля
  `[ORACLE]`), затем тем же полем в порте (`[dbg]` / `--dump-state`), и только
  потом правится код.
- Любая проверка должна быть **применима к обеим сторонам**: одно и то же имя
  поля в оракуле и в порте + скрипт сверки в `reference/tools/`.
- **На диск C: ничего не писать** — только в каталог проекта (`.harness/`,
  `reference/traces/`); временные профили/логи — `.harness/tmp/`.
- За собой чистить процессы: после прогонов не должно оставаться `game.exe`,
  `python`, `node`, `msedgewebview2`. Проверка:
  `Get-Process | ? { $_.ProcessName -match 'game|python|node|WebView2' }`.
