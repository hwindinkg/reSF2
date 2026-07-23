# reSF2 — Clean-room reimplementation of Shadow Fight 2

A reverse-engineered recreation of the Shadow Fight 2 game engine,
built from analysis of the original ARM (Android) and x86 (PC) binaries.

**⚠️ ОЧЕНЬ СЫРОЙ ДВИЖОК. НЕ ИГРА, А ТЕХНИЧЕСКОЕ ДЕМО.**

Текущая кодовая база — proof-of-concept с множеством известных и неизвестных дефектов.
Цель проекта — **1:1 повторение ВСЕГО геймплея, логики и загрузки ассетов оригинальной
мобильной версии** (APK v1.9.21, Marmalade SDK + Cocos2d-x 2.x).

## Статус

### ✅ Завершено
- **Phase 1** — input handler refactor: 14 accessors добавлены, 12 дублирующих полей удалены, 30+ ссылок заменены, 4 мёртвых HUD/renderer файла удалены
- **Формат парсеры**: S3E container, plist atlas, ATF tactics, bitmap font, DZ (type 1/2/8)
- **Verlet physics**: punching bag
- **Scene manager**: 9 сцен (Boot → Loading → MainMenu → Battle → Results → Shop → Map → Settings → Dialogue)
- **Компиляция**: MSVC C++23, 24/24 executables (22 test + 2 app), 0 errors, 21/22 tests pass

### 🟡 В работе
- **Phase 2** — Binary-Level RE Verification: Ghidra-based верификация функций против бинарника
  - Target A (FUN_101661d0 — ModelAnimation::playInfo): кандидат готов, VERDICT GREEN ✅
  - Targets B/C: ConditionInterval / ConditionCurrentAnimation pipeline — очередь
- **DZ type-4 decoder**: Известно что сломан (декомпрессия даёт 0x3B вместо 0x3C). Ожидает исследования алгоритма.

### ❌ Ещё не сделано
- AI противника (только punching bag)
- Audio (engine/audio/ — заглушки)
- Magic/ranged оружие (только Fists)
- 55 из 56 локаций (только dojo)
- Move transitions / MidFrames / FirstFrame
- Touch/тачскрин управление

## Структура репозитория (только исходники — всё для сборки)

```
reSF2/                         # корень репозитория
├── engine/                    # C++23 реконструкция движка
│   ├── game/                  # Game logic, input handler, scene host
│   ├── scene/                 # Scene manager, 9 сцен
│   ├── format/                # Парсеры (.s3e, .plist, .atf, .fnt)
│   ├── reverse/               # RE-форматы (DZ decoder, asset readers)
│   ├── core/                  # Math, utils, memory
│   ├── fight/                 # Combat/move system
│   ├── animation/             # Animation player
│   ├── platform/              # Platform abstraction (GLFW, Win32)
│   ├── renderer/              # OpenGL 2.1 / GLES2 + backend
│   ├── runtime/               # Runtime helpers
│   ├── audio/                 # Audio (заглушки)
│   ├── ui/                    # UI helpers
│   ├── network/               # Network (пусто)
│   ├── physics/               # Verlet physics
│   └── tools/                 # Debug tools
├── tests/                     # Юнит-тесты (22 теста, 21 проходит)
├── assets/                    # Игровые ассеты (pre-extracted из .dz)
│   ├── models/                # 72 model XML
│   ├── animations/            # moves.xml + 556 .bin
│   └── locations/             # 56 location директорий
├── scripts/                   # Build/debug скрипты
├── reverse/                   # Внешние RE-инструменты
├── tools/                     # Вспомогательные утилиты
├── CMakeLists.txt             # CMake build system
├── build.bat                  # Windows сборка
├── BUILD.md                   # Инструкция по сборке
└── README.md                  # Этот файл
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

## Инварианты разработки (НЕ нарушать)

1. **Не смешивать** Win32 `GetAsyncKeyState` с GLFW-колбэками для игрового ввода.
2. **Никаких тихих фоллбэков** без TODO-комментария и warning-лога в коде.
3. **Каждое изменение** помечать `[ORIGINAL]` (с адресом/символом бинарника) или `[HEURISTIC-TODO]`.
4. **Коммитить компилирующимися шагами** — не оставлять репо сломанным.

## External references

- S3ELoader (Ghidra loader): https://github.com/knot126/S3ELoader
- Marmalade-Modding: https://github.com/knot126/Marmalade-Modding
