# TRACE.md — как прогнать трассу JS и C++ одним скриптом (Этап 1)

Цель: прогнать **живой JS** (`reference/www/sf2.readable.js` через `OracleShell`
+ `reference/runner/`) и **порт** (`build/app/game/Release/game.exe`) в headless
с ОДНИМ seed-состоянием и ОДНИМ скриптом ввода, получить по одному JSON-кадру
на кадр боя с обеих сторон и показать **первый кадр расхождения**.

## 1. Общий скрипт ввода

Формат (`shell/InputScript.cs`, читают ОБЕ стороны):

```
# комментарий
wait <мс>                        # wall-clock пауза (навигация тапами)
tap <x> <y>                      # тап по экрану (навигация, wall-clock)
atframe <f> keydown <keyCode>    # клавиша ВНИЗ на кадре боя f
atframe <f> keyup   <keyCode>    # клавиша ВВЕРХ на кадре боя f
```

* `keyCode` — браузерный `keyCode` (`68`=D, `65`=A, `87`=W, `83`=S, `75`=K…),
  он же GLFW-код в порте (`FightScreen::key_type_for_glfw`).
* `f` — кадр **боя** (phase-local, `FightController::frame_` / JS `fight.frame`)
  — то же поле `f`, что несут обе трассы.
* Порт читает те же строки через `--replay <файл>`
  (`parse_replay_file`: `keydown/keyup` → `ReplayEdge{frame, code, down,
  glfw=true}` → `FightScreen::on_key`), страница — через
  `window.__oracleStimulus` (`trace.js` `beforeFrame`: DOM `KeyboardEvent`
  с `code`+`keyCode`+`which`, ровно как живое нажатие).
* Одна и та же граница: ребро с кадром `f` применяется ПЕРЕД тиком, который
  уводит бой за кадр `f` (порт: `edges[ei].frame <= fight_frames` перед
  `run_one_frame()`; страница: `item.f <= fight.frame` в pre-tick хуке).

Пример: `reference/tools/dojo_move.txt`.

## 2. Прогон оракула (JS)

```
shell\bin\Release\net9.0-windows\OracleShell.exe ^
  --query "postTutorial=0&maxFrames=1200" ^
  --input-script reference/tools/dojo_move.txt --timeout-ms 90000
python reference\tools\extract_state.py      # -> reference/traces/oracle_state.jsonl
python reference\tools\extract_layer.py ui   # -> reference/traces/oracle_ui.jsonl
```

`postTutorial=0` — стоковый туториал (= бой с грушей Training/Punchbag, тот же,
что и нативный дефолтный `--fight`); `postTutorial=1&battle=<Name>` — запись в
хаб/на-карту (см. `reference/tools/state_*.txt`).

## 3. Прогон порта (C++)

```
build\app\game\Release\game.exe --replay reference/tools/dojo_move.txt ^
  --dump-ui --headless 900 --dump-state 900 --watchdog 900 ^
  reference/www/res reference/saves/save.xml
```

`--replay` бутит тот же бой (Training, location dojo) и ведёт рёбра через
клавиатурный путь; `--dump-state N` пишет
`reference/traces/native_state.jsonl` (первые N живых кадров боя),
`--dump-ui` — `reference/traces/native_ui.jsonl`.

## 4. Дифф

```
python reference\tools\compare_state.py --label dojo-move ^
  --out reference\traces\state_gap_dojo-move.txt
python reference\tools\compare_ui.py --label dojo-move
```

`compare_state.py` выравнивает кадры и печатает таблицу
`field | first divergence (native f / oracle f, values) | mismatches / compared`.

## 5. Что несёт трасса

`{"t":"state"}` (одна строка на живой кадр боя, обе стороны, одни имена полей):

| поле | смысл |
|------|-------|
| `f`, `phase`, `round`, `timer` (`nf`) | кадр боя, фаза, номер раунда, таймер |
| `fighters[i].id` | `Me` / `Enemy` (в оракуле роль-мапа: `Me` = сторона врага) |
| `fighters[i].x`, `.y` | мировая позиция тела (`f.oa.Fe().ma` / `Fighter::world_x/y`) |
| `fighters[i].w` | экипированное оружие слота Weapon (`parameters.Hd.name` / `FightFighter::weapon`) |
| `fighters[i].hp`, `.mhp` | здоровье / максимум |
| `fighters[i].move`, `.af` | имя текущего мува и его кадр |
| `fighters[i].shock/block/invuln/rag/ragf/iv` | состояние (шок, блок, неуязвимость, рэгдолл, интервалы) |
| `cam`, `counters`, `rules`, `callouts` | камера, счётчики, правила раунда |

UI-слой: `{"t":"ui","stack":[{"id","name","state","active"}...]}` — стек экранов
снизу вверх (последний = живой экран) при каждом изменении; `compare_ui.py`
сравнивает эти события (`--dump-ui` против `extract_layer.py ui`).
«Какие кнопки активны» на уровне отдельных кнопок в трассе пока НЕТ — только
состояние экрана (`state`/`active`) и поведенческие зонды `--ui-tour`.

## 6. Самопроверка стимула

`trace.js` вешает окно-слушатель (`[STIMCHK]`) и печатает, что видит движок:
`code`, `keyCode`, `which`, `repeat`, кадр. Если строка `[STIM] keydown …`
есть, а `[STIMCHK]` нет — событие не дошло до игры; если `keyCode=0` —
стимул собран неверно.
