# reSF2 — План доработки до 1:1 соответствия оригиналу (Shadow Fight 2, APK v1.9.21)

> Документ подготовлен по результатам полного аудита репозитория
> `https://github.com/hwindinkg/reSF2/` (HEAD на момент аудита).
> Цель: движок должен **побайтово/потактово** повторять ВЕСЬ геймплей, логику и
> загрузку ассетов оригинальной мобильной версии (Marmalade SDK + Cocos2d-x 2.x).

---

## 0. Резюме аудита (что нашли)

### 0.1. Три версии в репозитории

| Папка | Что это | Формат ассетов | Цель C++ движка |
|-------|---------|----------------|-----------------|
| `sf2/` | **Оригинальная мобильная APK** v1.9.21 (Android, ARMv7, Marmalade + Cocos2d-x) | DTRZ `.dz` архивы (type-4 custom + type-8 gzip), plist-атласы, `.atf` zlib-тактики, `.bin` LE-анимации, PNG, WAV/MP3 | ✅ ИМЕННО ЕЁ должен грузить движок |
| `sf2_pc/` | **Новая ПК-версия** от Famobi v1.0.6 (HTML5/WebView2 + .NET 8 WinUI, x64) | Zstandard `.dat`, JSON-атласы, KTX/DDS/AVIF/WebP, Ogg/M4A | ❌ Совершенно другие форматы; используется ТОЛЬКО как читаемый JS-референс (`sf2_beautified.js`) |
| `sf2_symbian/` | Скелет порта под Symbian Nokia N8 (C++03, GCCE, GLES2) | — | Отложен |

### 0.2. Как разработчики переносили механики

Разработчики вели реверс-инжиниринг **трёх параллельных источников**:

1. **Декомпиляция оригинальных бинарников** (`reverse/binaries/`):
   - `ShadowFight2.s86` (6.95 МБ, PE32 i386, Windows Phone 8.1) — основной код игры, легче всего декомпилировать (objdump/Ghidra/IDA).
   - `ShadowFight2_android.bin` (8.29 МБ, XE3U/S3E, x86_64 PIE) — Android-код (LZMA, нужен S3ELoader для Ghidra).
   - `libs3e_android.so` (800 КБ, ARMv7) — **Marmalade-лоадер, содержит DZ-декомпрессор type-4 по смещению 0x389f8**.
   - `s3e_native.dll` (1.13 МБ, PE32 i386) — Marmalade-рантайм Windows (ввод, звук, GL→D3D11).
2. **48 декомпилированных функций** в `scripts/dz_*_decompiled.c` (15 782 строк radare2-вывода): Model::step, playInfo, key handling, DZ read path, fight update и т.д.
3. **PC JS-версия** (`sf2_pc/www/sf2_beautified.js`) как читаемый референс для проверки бинарных находок (классы `ue/Db/O/Pg/L/K/Pf/G/Qg/Ss`).

### 0.3. Что такое «отсебятина» в текущем движке

«Отсебятина» — это места, где вместо **проверенной по бинарнику логики `[ORIGINAL]`** стоит **эвристика `[HEURISTIC-TODO]`** или вообще придуманный код:

| Тип отсебятины | Где | Примеры |
|----------------|-----|---------|
| **Эвристики вместо RE-логики** (20 тегов `[HEURISTIC-TODO]`) | main.cpp:265, 2352, 3361; dz_decoder.hpp:5; HANDOFF.md (баги #1–7); docs | MoveInside Y consumption, root-motion whitelist, denylist в `in_basic_attack` |
| **Магические числа** вместо data-driven | main.cpp (7 мест) | `FEET_FLOOR_OFFSET=4.0`, `STANCE_NPIVOT_Y_BASELINE=106.0`, NPivot rest `169.48`, floor_y `-193`, render_y `-89`, `X_OFFSET=983.0` |
| **Придуманные тайминги** (нет в оригинале) | main.cpp | `step_min_played=400ms`, `fwd_held_ms_=200ms`, sticky-буфер `150ms` — оригинал использует потактовые триггеры из moves.xml |
| **Строковый XML-парсинг** вместо настоящего | main.cpp:83-87, 1687-1755, 2835-3121 | `xml.find("<Move ")` + `xml_attr()` — теряет ComplexInterval, MidFrames, FirstFrame, Distance, Locks, MoveInside Pivot. При этом **настоящий парсер `engine/format/xml_doc.cpp` УЖЕ существует**, но shipping-бинарник `resf2_app` его не использует! |
| **Придуманные флаги** | main.cpp | `bag_hit_` — оригинал использует `Invulnerable`-интервал на цели |
| **Silent fallback** на предизвлечённые файлы | dz_reader | DZ type-4 сломан → `DzRegistry::read_from_fallback()` перебирает 8 хардкод-подпутей к `assets/models/*.xml` |
| **Пустые заглушки подсистем** | engine/audio, engine/network, engine/physics, engine/tools, engine/animation | только `# Placeholder` |
| **Хардкод путей/контента** | main.cpp | `load_location("dojo")` — только одна локация; `model_paths()` — 4 хардкод-вложенных пути |

### 0.4. Главный блокер

**DZ type-4 декодер сломан.** Без него движок **не может читать `files.dz`** (1.97 МБ), а там — ВСЕ игровые XML (moves.xml, params.xml, list.xml, forge.xml, settings.xml, devices.xml), ВСЕ модели (72 файла), скелет, body. Сейчас работает только `animations.dz` (type-8 gzip). Все ассеты в `assets/` — это **предизвлечённые вручную** файлы, не загруженные из оригинального архива.

---

## 1. Фазы работ

Принцип: **каждый коммит помечать `[ORIGINAL]` (с адресом/символом бинарника, который лично проверен) или `[HEURISTIC-TODO]` (с тем, что осталось реверснуть).** Никаких тихих фоллбэков без TODO-комментария и warning-лога в коде.

### Фаза A. Снять главный блокер: DZ type-4 декодер (КРИТИЧНО)

**Цель:** движок читает `files.dz` напрямую, без предизвлечённых файлов.

**Что известно:**
- Декомпрессор в `libs3e_android.so` по смещению **0x389f8** (~250 ARM-инструкций).
- Алгоритм: **арифметическое/диапазонное кодирование + 5-байтный контекст + CRC32-хэш + LZ77**.
- Таблица функций-диспетчеров по смещению **0xc3000** (8 записей), таблица кодеров **0xc8514** (4 слота × 0x88 байт) — по `scripts/dz_check_table.py`.
- Range-кодер по **0x37adc**, bit-tree по **0x3751c**.
- Стартовые точки для трассировки: `engine/reverse/dz/dz_arm_emu*.py` (6 файлов) и `scripts/dz_arm_emu*.py` (4 файла) + `scripts/dz_unicorn_v7.py` (Unicorn-эмуляция).

**Задачи:**
- **A1.** Установить/использовать **Unicorn Engine** (ARM) + **capstone** для потактового трейсинга 0x389f8..0x38d00. Декомпилировать функцию в человекочитаемый C.
- **A2.** Верифицировать range-кодер 0x37adc и bit-tree 0x3751c: формат контекста (5 байт), CRC32-полином, вероятностная таблица.
- **A3.** Воссоздать алгоритм в `engine/reverse/dz/dz_decoder.cpp` (заменить спекулятивный LZMA-вариант). Покрыть юнит-тестом: декодировать `files.dz` и сверить контрольные суммы/контент с предизвлечёнными `assets/models/*.xml`.
- **A4.** Убрать `DzRegistry::read_from_fallback()` (8 хардкод-подпутей). Все запросы ассетов должны идти через `files.dz`.
- **A5.** После A3–A4: удалить предизвлечённые `assets/models/`, `assets/list.xml`, `assets/forge.xml` и т.д. из репо (или пометить как тест-фикстуры). Движок должен работать только с оригинальной `sf2/assets/`.

**Критерий приёмки:** `resf2_app --assets sf2/` (путь к оригинальной APK-распаковке) грузит Dojo и персонажа **без** папки `assets/models/` в репозитории.

---

### Фаза B. Воспроизвести конвейер анимации и Y-позиционирования 1:1 (КРИТИЧНО)

**Цель:** прыжки/перекаты/сальто/атаки в воздухе ведут себя побайтово как в оригинале. Убрать все «interim fix» и хардкод-константы.

**Что известно:**
- 3-шаговый пайплайн MoveInside **полностью оттречен** в бинарнике:
  - Шаг 1: `0x10165c10` (updateNodes) — обновление позиций узлов скелета.
  - Шаг 2: `0x10164c20` (applyInterpolation) — `fcn.10103690(node_owner, axis=2, &out)` + `fcn.10103e80(animInfo, result)`.
  - Шаг 3: `0x101661d0` (finalizePosition) — формула известна:
    ```
    Model+0x80.x = (float)Model+0x54 * moveInside+0xb4 + [ebp-0x28].x
    Model+0x80.y = moveInside+0xb8 + [ebp-0x28].y
    ```
    `fcn.1028e890` = Vec3 subtract.
- **moveInside+0x68 (mode selector) ВЫЧИСЛЯЕТСЯ В РАНТАЙМЕ** (не из moves.xml — у всех jump/flip/roll одинаковый Align). Write-site **НЕ найден** — нужен Ghidra typed-struct анализ конструктора `0x10101b00` (RTTI vtable `0x105ac8e8`).
- **Формула MoveInside ПОЛНОСТЬЮ ДЕКОДИРОВАНА из PC JS** (`sf2_beautified.js:17190-17228`): флаги осей `cI/dI/MY` из `Axis="X|Z"`, константы `ShiftX/ShiftY`, функция `Gla()` со сдвигом и per-axis-выбором.

**Задачи:**
- **B1.** Реализовать **проверенную Step 3 формулу** (Model+0x80/0x98, fcn.1028e890) в main.cpp, заменив interim NPivot Y displacement (`y_adjust_smoothed_` хаки в main.cpp:265, 2352, 3361). Все `[HEURISTIC-TODO]` → `[ORIGINAL]`.
- **B2.** Найти **write-site moveInside+0x68** через Ghidra typed-struct анализ (конструктор `0x10101b00`, подструктура по указателям +0x88/+0x94/+0xa0). Реализовать per-axis alignment (X|Z из атрибута `Axis`) вместо хардкод-режима.
- **B3.** Перенести JS-формулу `Gla()/cI/dI/MY` (`sf2_beautified.js:17190-17228`) как референс-контроль и сверить с бинарной Step 3.
- **B4.** Убрать магические числа: `FEET_FLOOR_OFFSET=4.0`, `STANCE_NPIVOT_Y_BASELINE=106.0`, `169.48`, `-193`, `-89`, `-93`. Заменить на data-driven значения из moves.xml/skeleton.xml (baseline = позиция NHeel_X в stance-анимации, floor_y = из params.xml локации).
- **B5.** Реализовать **transition frames / MidFrames / FirstFrame** в `.bin`-анимации (сейчас отсутствует, `[HEURISTIC-TODO]` в HANDOFF баг skeletal 70%).

**Критерий приёмки:** покадровое сравнение `render_y`/`player_x`/`npivot_x`/`npivot_y` движка с эталонным дампом оригинала (снять через Unicorn-эмуляцию playInfo) для анимаций jump/flip/roll/air-attack — совпадение ±0.5px.

---

### Фаза C. Настоящий парсер moves.xml и логика боя (КРИТИЧНО)

**Цель:** полный moves.xml со всеми интервалами/условиями/локами; убирать придуманные тайминги и флаги.

**Задачи:**
- **C1.** Перевести shipping-бинарник `resf2_app` на **существующий** `engine/format/xml_doc.cpp` + `engine/format/moves.cpp`. Удалить строковый парсинг `xml.find("<Move ")`/`xml_attr()` в main.cpp:83-87, 2835-3121.
- **C2.** Добавить парсинг **ComplexInterval, MidFrames, FirstFrame, Distance-условий, Locks-секций, MoveInside Pivot** (всё, что сейчас теряется). Сверить с `dz_moves_proc_decompiled.c` (реальная обработка) и `dz_condition_interval_decompiled.c`, `dz_condition_keys_decompiled.c`, `dz_condition_curranim_decompiled.c`.
- **C3.** **Убрать придуманные тайминги:** `step_min_played=400ms`, `fwd_held_ms_=200ms`, sticky-буфер `150ms`. Заменить на **потактовые триггеры из moves.xml** (Move → Trigger → Condition), как в `dz_move_trigger_decompiled.c` + `dz_move_selector_decompiled.c` + `dz_keypad_update_decompiled.c` + `dz_eventkeypressed_decompiled.c`.
- **C4.** **Убрать `bag_hit_` флаг.** Реализовать `Invulnerable`-интервал на цели (из moves.xml) — как в оригинале. Сверить с `dz_fight_update_decompiled.c` + `dz_physics_frame_decompiled.c`.
- **C5.** **Uninterrupt-интервалы:** сверить с `dz_check_interval_decompiled.c` + `dz_interval_lookup_decompiled.c` + `dz_ck_check_decompiled.c`. Сейчас «не совпадает с оригиналом» (HANDOFF баг #5).
- **C6.** **Hit detection:** правильно смаппить AttackingParts edges на skeleton.xml (сейчас баг «hit без анимации», HANDOFF баг #4). Использовать `IntervalAttack::getFactors` (0x10115921) для расчёта урона.

**Критерий приёмки:** 7 input-script сценариев из `Testing/scenarios/` (double_o_uninterrupt, duck_during_punch, bag_reaction, combo_bag_full, air_attack_chain, op_block_during_attack, walk_punch_walk) проходят с **нулевыми** расхождениями от эталонных дампов.

---

### Фаза D. Реальный противник и боевая система (ВЫСОКИЙ)

**Цель:** два бойца, здоровье, урон, ИИ — не только punching bag.

**Задачи:**
- **D1.** Реализовать `engine/fight/fighter.cpp` (сейчас только .hpp): state machine idle/walk/attack/hit/block/dead по образцу оригинала (`dz_fight_update_decompiled.c`).
- **D2.** **Расчёт урона** через `IntervalAttack::getFactors` (0x10115921): оружие + броня + перки. Парсить `forge.xml` (уже есть в assets).
- **D3.** **ИИ противника** из `.atf`-тактик (`engine/reverse/atf_tactics.cpp` уже парсит zlib-blob). Заполнить `engine/fight/ai.cpp` (сейчас 40 строк без .cpp). Тактики в `assets/tactics/` + `sf2/assets/assets/tactics/`.
- **D4.** Реализовать **magic/ranged** (сейчас только Fists/unarmed). Парсить соответствующие секции moves.xml + модели из `assets/models/magic_*.xml`, `ranged_*.xml`.
- **D5.** **Verlet-физика** вынести из main.cpp в `engine/physics/` (сейчас пусто). punching bag 15 узлов / 23 ограничения.

**Критерий приёмки:** бой игрок vs ИИ на Dojo, 5 минут без крашей, ИИ атакует/блокируется/получает урон корректно.

---

### Фаза E. Загрузка ВСЕХ ассетов оригинальной мобильной версии (ВЫСОКИЙ)

**Цель:** `resf2_app --assets sf2/` грузит **всё** содержимое оригинальной APK без ручного предизвлечения.

**Задачи:**
- **E1.** **Де-хардкод путей:** `load_location()` должен грузить **все 56 локаций** из `sf2/assets/assets/locations/`, а не только `dojo`. Перечисление через `list.xml` (после Фазы A).
- **E2.** **KTX/DDS-текстуры** (renderer.cpp:79-80 TODO): мобильная версия использует PNG (OK), но добавить поддержку на будущее. Главное — убедиться, что PNG/plist-атласы грузятся из DZ-архивов.
- **E3.** **Аудио (КРИТИЧНО для геймплея):** `engine/audio/` пусто. Оригинал — WAV/MP3 через Marmalade `s3eAudio`. Реализовать: WAV (просто), MP3 (через minimp3 или libmpg123). Звуки в `sf2/assets/assets/sounds/`, музыка в `sf2/assets/assets/music/`.
- **E4.** **Видео** (`sf2/assets/assets/video/`, MP4 через ffmpeg-libs из APK): интро/катсцены. Опционально (Phases позже).
- **E5.** **Шрифты:** AngelCode BMFont `.fnt` уже парсится (`engine/reverse/bitmap_font.cpp`). Проверить TTF из `sf2/assets/Fonts/`.
- **E6.** **.s3e-контейнер** (`sf2/assets/ShadowFight2.s3e`, LZMA, 8.69 МБ распакованный): уже парсится `engine/reverse/s3e_container.cpp`. Убедиться, что грузится напрямую.
- **E7.** **.icf-конфиги** (`app_android.icf`, `app_android_obb.icf`): Marmalade config. Парсить или игнорировать с TODO.

**Критерий приёмки:** `resf2_app --assets /path/to/sf2/` (чистая распаковка APK) запускается, играет музыка, слышны звуки ударов, доступны все локации из списка.

---

### Фаза F. Завершить сцен-менеджер и игровой цикл (СРЕДНИЙ)

**Цель:** полный игровой цикл Boot → Menu → Map → Fight → Results → Map.

**Задачи:**
- **F1.** Заполнить заглушки сцен (`engine/scene/scenes.cpp:233,253,273,416` — сейчас `(void)ctx;`): Map, Shop, Settings, Dialogue, Results.
- **F2.** **Shop** из `list.xml` (предметы: оружие/броня/шлемы/ranged/magic). IAP-заглушки.
- **F3.** **Player profile:** XP, уровень, золото, геммы, инвентарь. Save/load (сейчас temp-JSON-заглушка).
- **F4.** **Quests/Perks** из `quests.xml`/`perks.xml` (есть в DZ после Фазы A).
- **F5.** **Прогрессия:** зона → бои → награды.

**Критерий приёмки:** полный цикл игры работает: запуск → меню → выбор зоны → бой → результат → возврат в меню.

---

### Фаза G. Убрать ВСЕ оставшиеся HEURISTIC-TODO (СРЕДНИЙ)

**Цель:** ни одного `[HEURISTIC-TODO]` без плана; каждое — либо `[ORIGINAL]`-верифицировано, либо задокументировано как «невозможно без исходников».

**Полный список (20 тегов):**

| # | Файл:строка | Что | Действие |
|---|-------------|-----|----------|
| 1 | main.cpp:265 | y_adjust NPivot Y displacement | Фаза B1 |
| 2 | main.cpp:2352 | MoveInside render-Y consumption | Фаза B1 |
| 3 | main.cpp:3361 | y_adjust formula | Фаза B1 |
| 4 | dz_decoder.hpp:5 | type-4 decoder speculative | Фаза A3 |
| 5 | HANDOFF баг #1 | denylist в in_basic_attack | Фаза C (template-check из оригинала) |
| 6 | HANDOFF баг #2 | jump/flip Y interim | Фаза B1 |
| 7 | HANDOFF баг #3 | moveInside+0x68 mode selector | Фаза B2 |
| 8 | HANDOFF баг #4 | hit without animation (Windows) | Фаза C6 |
| 9 | HANDOFF баг #5 | grounded roll Y flat | Фаза B1 (зависит от B2) |
| 10 | HANDOFF баг #6 | DZ type-4 decoder | Фаза A |
| 11 | HANDOFF баг #7 | asset path hardcoding | Фаза E1 |
| 12 | HANDOFF | root-motion whitelist | data-driven MoveDef lookup |
| 13–26 | docs/s3e_reverse_engineering.md | 14 RE-пунктов | по мере Фаз A–F |

---

## 2. Порядок выполнения и зависимости

```
Фаза A (DZ type-4) ──┐
                     ├──► Фаза C (moves.xml + бой) ──► Фаза D (ИИ/противник)
Фаза B (анимация/Y) ─┘                                        │
                                                              ▼
Фаза E (ассеты) ◄── зависит от A ─────► Фаза F (сцены) ──► Фаза G (очистка TODO)
```

**Параллелизм:** Фазы A и B можно вести параллельно (разные подсистемы). Фаза C частично параллельна B. Фаза E ждёт A. Фаза F ждёт C+D. Фаза G — финальная.

**Приоритет для пользователя («грузить старые мобильные ассеты + 1:1 геймплей»):**
1. **A** (снять блокер ассетов) — без этого ничего по-настоящему не грузится.
2. **B + C** (1:1 анимация/бой) — без этого геймплей не повторяет оригинал.
3. **E** (полные ассеты) — без этого нет звука/локаций.
4. **D, F, G** — доводка.

---

## 3. Инструменты и методика

- **Unicorn Engine** (ARM) + **capstone** — потактовый трейсинг `libs3e_android.so` (DZ type-4, MoveInside). Уже есть `scripts/dz_unicorn_v7.py`, `scripts/dz_arm_emu*.py`.
- **Ghidra** + **S3ELoader** (https://github.com/knot126/S3ELoader) — для `ShadowFight2_android.bin` (XE3U-формат). Typed-struct анализ для moveInside+0x68 write-site.
- **objdump** — для `ShadowFight2.s86` (PE32, самый лёгкий).
- **IDA Pro / Hex-Rays** — если доступен, самый чистый C-вывод для s86.
- **PC JS `sf2_beautified.js`** — читаемый референс для проверки бинарных находок.
- **Тестирование:** `--input-script`/`--max-frames` (детерминированный replay) + `[INPUT_DECISION]`/`[HIT_CHECK]`/`[ROOT]` логи + Unicorn-дампы оригинала как эталон.

---

## 4. Инварианты (НЕ нарушать — из HANDOFF.md)

1. **Не смешивать** Win32 `GetAsyncKeyState` с GLFW-колбэками для игрового ввода (re-trigger held keys). Windows = только GetAsyncKeyState; Linux = только GLFW-колбэки.
2. **Никаких тихих фоллбэков** без TODO-комментария и warning-лога в самом коде (не только в docs).
3. **Каждое изменение** помечать `[ORIGINAL]` (с адресом/символом бинарника, лично проверенным) или `[HEURISTIC-TODO]` (с тем, что осталось реверснуть).
4. **Коммитить компилирующимися шагами** — не оставлять репо сломанным между коммитами.

---

## 5. Оценка текущего соответствия оригиналу

| Подсистема | Сейчас | Цель |
|------------|--------|------|
| Загрузка ассетов (DZ type-4) | 40% (type-1/2/8 OK, type-4 broken) | 100% |
| Анимация/Y | 42–70% (interim fixes) | 100% |
| Бой/moves.xml | 42–50% (строковый парсинг, придуманные тайминги) | 100% |
| Hit detection | 42% (bag_hit_ флаг) | 100% |
| ИИ/противник | 0% (только punching bag) | 100% |
| Аудио | 0% (пусто) | 100% |
| Сцены | 30% (5/9 заглушки) | 100% |
| Контент (локации/оружие/magic) | 10% (только dojo, только Fists) | 100% |
| **ИТОГО** | **~40–50%** | **100%** |

Текущий движок — **честный техдемо Dojo с punching bag**, а не 1:1 реплика Shadow Fight 2. После Фаз A–C соответствие геймплея поднимется до ~85%, после D–G — к 100%.
