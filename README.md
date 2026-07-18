# reSF2 — Clean-room reimplementation of Shadow Fight 2

A reverse-engineered recreation of the Shadow Fight 2 game engine,
built from analysis of the original Android (ARM) and Windows (x86) binaries.

**⚠️ ОЧЕНЬ СЫРОЙ ДВИЖОК. НЕ ИГРА, А ТЕХНИЧЕСКОЕ ДЕМО.**

Текущая кодовая база — proof-of-concept с множеством известных и неизвестных дефектов.
Цель проекта — **1:1 повторение ВСЕГО геймплея, логики и загрузки ассетов оригинальной
мобильной версии** (APK v1.9.21, Marmalade SDK + Cocos2d-x 2.x).

- 📋 **План доработки до 1:1**: [PLAN_1TO1.md](PLAN_1TO1.md) (7 фаз, текущее соответствие ~40–50%)
- 📋 План портирования на Symbian: [PLAN_SYMBIAN.md](PLAN_SYMBIAN.md)
- 📋 План портирования PC-форматов: [PLAN_PORT.md](PLAN_PORT.md)
- 📝 Журнал разработки и статус: [HANDOFF.md](HANDOFF.md), [worklog.md](worklog.md)

## Текущее состояние (честно)

### Работает, но сыро/криво ⚠️
- **Загрузка локации Dojo**: Парсинг params.xml. **Некорректно загружает слои** — Layer/Image тэги парсятся с ошибками позиционирования. Параллакс работает с артефактами.
- **Рендер персонажа**: 82 капсулы + 29 треугольников из body.xml. Тёмный силуэт. Y-позиционирование — набор хаков (feet clipping, floating при roll).
- **Скелетная анимация**: Загружает 556 .bin файлов. Корневая анимация NPivot X работает. NPivot Y — **сломан** (MoveInside alignment не доделан; интерим-формула с хардкод-константами).
- **Verlet физика**: Punching bag качается, импульсы работают.
- **Hit detection**: Использует Attack интервалы из moves.xml. **Не всегда корректно** — баг с "hit без анимации".
- **Combat**: 1key/2key/3key работает частично. Uninterrupt проверяется.
- **moves.xml парсинг**: Примитивная XML-резка строк в main.cpp. **Криво парсит** — теряет ComplexInterval, некорректно парсит Distance условия, Locks секции, MoveInside Pivot. *(Настоящий парсер уже есть в `engine/format/`, но shipping-бинарник его не использует.)*
- **DZ архивы**: gzip (type=8) работает. type=4 декомпрессия **полностью сломана** — использует fallback к pre-extracted файлам. ⚠️ **Главный блокер**: без type-4 движок не может читать `files.dz` (1.97 МБ — все игровые XML, модели, скелет).
- **Scene manager**: 9 сцен есть, но Map/Shop/Settings/Dialogue — **пустышки** (только заглушки).

### Не сделано ❌
- **Настоящий AI противника**: Только punching bag
- **Audio**: Нет звука (`engine/audio/` пусто)
- **Magic/ranged**: Только Fists (unarmed)
- **Другие локации**: Только dojo (из 56)
- **Сохранение/загрузка**: Заглушка (JSON в temp)
- **Combat timing**: Uninterrupt интервалы не совпадают с оригиналом
- **Move transitions**: Нет transition frames / MidFrames / FirstFrame
- **Управление**: Только клавиатура. Нет touch/тачскрина

### Отсебятина (где отошли от оригинала)
Движок НЕ является 1:1 копией. Места с эвристиками вместо проверенной RE-логики помечены
тегом `[HEURISTIC-TODO]` (20 штук — см. [HANDOFF.md](HANDOFF.md) и [PLAN_1TO1.md](PLAN_1TO1.md)).
Основные категории:
- **Магические числа**: `FEET_FLOOR_OFFSET=4.0`, `169.48`, `106`, `-193`, `-89`, `X_OFFSET=983` (в 7 местах)
- **Придуманные тайминги** (нет в оригинале): `step_min_played=400ms`, `fwd_held_ms_=200ms`, sticky-буфер `150ms`
- **Строковый XML-парсинг** `xml.find("<Move ")` вместо настоящего парсера
- **Придуманный флаг** `bag_hit_` вместо оригинального `Invulnerable`-интервала
- **Silent fallback** на предизвлечённые файлы (DZ type-4 сломан)
- **Хардкод путей/контента**: только dojo, только Fists, 4 хардкод-пути к моделям

## Что есть в репозитории (три версии + RE-материалы)

```
reSF2/
├── engine/              # C++23 реконструкция (core/fight/format/platform/renderer/reverse/runtime/scene/ui)
│                        #   audio/network/physics/animation — ПУСТЫЕ заглушки
├── main.cpp             # Game logic + SceneHost (~4190 строк, МОНОЛИТ)
├── main_port.cpp        # PC-порт (заготовка)
├── headless_main.cpp    # Headless-режим для тестов
├── sf2/                 # ОРИГИНАЛЬНАЯ мобильная APK v1.9.21 (Marmalade + Cocos2d-x)
│   ├── assets/          #   files.dz (type-4), animations.dz (type-8), .s3e, locations/, sounds/, music/
│   └── lib/armeabi-v7a/ #   libs3e_android.so (содержит DZ-декомпрессор @ 0x389f8)
├── sf2_pc/              # НОВАЯ ПК-версия (Famobi, HTML5/WebView2 + .NET 8)
│   └── www/             #   sf2_beautified.js — читаемый JS-референс для проверки бинарных находок
│                        #   (форматы ассетов СОВСЕМ другие — НЕ грузится движком)
├── sf2_symbian/         # Скелет порта под Symbian Nokia N8 (C++03, GLES2) — заглушки
├── OriginalWindowsFiles/# Windows Phone 8.1 build (источник s86-бинарника)
├── reverse/binaries/    # Оригинальные бинарники для RE:
│                        #   ShadowFight2.s86 (PE32 i386), s3e_native.dll,
│                        #   ShadowFight2_android.bin (XE3U), libs3e_android.so (ARMv7)
├── scripts/             # 48 декомпилированных функций dz_*_decompiled.c (15 782 строк)
│                        #   + Python-трейсеры (Unicorn/capstone) для DZ type-4
├── docs/                # 26 RE-документов (s3e_reverse_engineering.md — основной)
├── assets/              # Предизвлечённые ассеты (временная мера до починки DZ type-4)
└── tests/               # Юнит-тесты парсеров + input-script сценарии
```

## Архитектура движка

```
main.cpp                    — Game logic + SceneHost (~4190 lines, МОНОЛИТ)
engine/scene/               — Scene/state manager (9 scenes, заглушки)
engine/platform/            — Platform abstraction (GLFW на Linux, Win32 на Windows)
engine/renderer/            — OpenGL 2.1 / GLES2 рендерер
engine/reverse/             — Парсеры форматов:
  - s3e_container.cpp       — Marmalade S3E container
  - plist_atlas.cpp         — Cocos2d TexturePacker v2
  - atf_tactics.cpp         — zlib-compressed tactics blob
  - bitmap_font.cpp         — AngelCode BMFont
  - dz_reader.cpp           — DZ archive reader (DTRZ): type 1/2/8 OK
  - dz_decoder.cpp          — DZ type=4 range coder decoder (СЛОМАН)
assets/models/              — 72 model XML файла (предизвлечено из files.dz)
assets/animations/          — moves.xml + binary/ (556 .bin файлов)
assets/locations/           — 56 location директорий
```

## Сборка (Desktop)

### Windows
```
build.bat
```
Требуется: CMake ≥ 3.24, MSVC, C++23, Windows SDK

### Linux (compile check only)
```
bash scripts/verify_main_compile.sh
```

## Запуск
```
resf2_app.exe --assets E:\reSF2
```

## Дорожная карта до 1:1 (кратко)

| Фаза | Цель | Статус |
|------|------|--------|
| **A** | DZ type-4 декодер (снимает блокер ассетов) | 🔴 Не начата |
| **B** | Анимация/Y 1:1 (MoveInside, убрать магические числа) | 🔴 Interim |
| **C** | Настоящий moves.xml парсер + бой по бинарнику | 🟠 Частично |
| **D** | Реальный противник + ИИ + magic/ranged | ❌ Нет |
| **E** | Все 56 локаций + аудио (WAV/MP3) | ❌ Нет |
| **F** | Сцен-менеджер (Shop/Map/Settings/Dialogue) | ❌ Заглушки |
| **G** | Убрать все 20 `[HEURISTIC-TODO]` → `[ORIGINAL]` | 🟡 В процессе |

Подробности — в [PLAN_1TO1.md](PLAN_1TO1.md).

## Инварианты разработки (НЕ нарушать)

1. **Не смешивать** Win32 `GetAsyncKeyState` с GLFW-колбэками для игрового ввода.
2. **Никаких тихих фоллбэков** без TODO-комментария и warning-лога в коде.
3. **Каждое изменение** помечать `[ORIGINAL]` (с адресом/символом бинарника) или `[HEURISTIC-TODO]`.
4. **Коммитить компилирующимися шагами** — не оставлять репо сломанным.

## External references

- APK: https://chat.chobat.ru/Shadow+Fight+2_1.9.21.apk
- Game data: https://chat.chobat.ru/sf2.7z
- PC version: https://chat.chobat.ru/sf2_pc.7z
- S3ELoader (Ghidra loader): https://github.com/knot126/S3ELoader
- Marmalade-Modding: https://github.com/knot126/Marmalade-Modding

## Reverse Engineering Documentation

See `docs/` for detailed RE notes. Основной документ: `docs/s3e_reverse_engineering.md`.
