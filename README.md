# reSF2 — Ренерация движка Shadow Fight 2

Чистая реинженерия движка Shadow Fight 2 по результатам реверс-инжиниринга
бинарников (Windows PE32 `ShadowFight2.s86`, Android ARM `ShadowFight2_android.bin`)
и обфусцированного JS оригинальной Unity-версии (`sf2_pc/www/sf2_beautified.js`, 80205 строк).

Цель — **1:1 воспроизведение геймплея, логики и загрузки ассетов** оригинальной
мобильной версии (APK v1.9.21, Marmalade SDK + Cocos2d-x 2.x).

**⚠️ ОЧЕНЬ СЫРОЙ ДВИЖОК. НЕ ИГРА, А ТЕХНИЧЕСКОЕ ДЕМО.**

Полный план с адресами бинарника и открытыми вопросами — в `PORT_PLAN.md` (1266 строк).

## Статус (июль 2026)

### Завершено

| Фаза | Что сделано | Проверка |
|------|-------------|----------|
| Phase 0 | DZ-контейнер + оба кодера (0x004, 0x008) | 120/120 files.dz, 557/557 animations.dz — байт-в-байт |
| Phase 1 | Мировая геометрия (F1), камера, debug world | 41 чек в test_world_geometry |
| Phase 2 | Бойцы на полу (убрано проседание 131-138 ед.), приоритеты анимаций, `<Align>` pivot, SimpleEffect/Transparency кривые, слой локации (parallax sign, кроп атласа, rotated frame unrolling, masking boxes) | 49 чек в test_effect_curve, 4316+ в test_moves_semantics |
| Phase 3 | Double-tap (300 мс из idle), SemiUninterrupt/SelfUninterrupt, condition system (CurrentAnimation, Interval), step cooldown (200 мс), dialog scroll (Roll_/Paper_/портрет), typewriter text, UTF-8, карта (zone sheets, battle nodes), shop (силуэт/пергамент/детали/категории), scroll overlay (M key), Fight HUD (HP bar + trail, round dots), rounds (best-of-N + timer), enemy AI (tactic roulette из tacticSettings.xml), attack intervals (265/316 moves), hit detection (shared transform), bag collision (3 причины) | 27 тестов проходят |

### В работе / Открыто

| Категория | Что осталось |
|-----------|--------------|
| Бои | DamageFactor / BlockDamageFactor, skeleton hitbox, block mechanics, .atf парсер, полный tactic-driven AI, vertical gameplay (knockback/knockdown), magic/ranged снаряды |
| Аудио | MP3-декодер, ActionSound / ActionRandomSound — заглушки |
| Локации | Реализована только `dojo` из 56 |
| Тач-управление | Виртуальный джойстик + кнопки атаки — вёрстка готова, не подключено |
| Прогрессия | Quest engine — заглушка, save system — базовый |
| Экраны | Results / Settings — заглушки |
| Скриншоты | Harness сравнения с оригиналом не реализован |

## Структура проекта

```
reSF2/
├── engine/
│   ├── game/
│   │   ├── clean.hpp              # Типы + объявление Game
│   │   ├── combat.hpp/cpp         # Hit detection, AI, таймеры
│   │   ├── input_handler.hpp/cpp  # Ввод, double-tap, step frames
│   │   ├── condition_system.hpp/cpp  # Gate-условия для moves
│   │   ├── shop.hpp/cpp           # Каталог предметов, транзакции
│   │   ├── player.hpp/cpp         # Профиль, валюта, инвентарь
│   │   ├── inventory.hpp/cpp      # Экипировка, предметы
│   │   ├── animation_player.hpp/cpp  # Воспроизведение, приоритеты
│   │   ├── location_manager.hpp/cpp  # Загрузка локаций
│   │   ├── asset_manager.hpp/cpp  # Текстурные атласы, анимации
│   │   ├── tactic_settings.hpp/cpp  # AI весовые кривые, рулетка
│   │   ├── quest_engine.hpp       # Обработка квест-действий
│   │   └── save.hpp/cpp           # JSON-персистентность
│   ├── scene/                     # Scene manager, 9 сцен
│   ├── format/                    # Парсеры (.s3e, .plist, .atf, .fnt)
│   ├── reverse/                   # RE-форматы (DZ decoder)
│   ├── renderer/                  # OpenGL 2.1 / GLES2
│   ├── physics/                   # Verlet
│   ├── platform/                  # GLFW, Win32
│   ├── audio/                     # Заглушки
│   ├── core/                      # Math, utils
│   └── tools/                     # Debug
├── tests/                         # 27 тестов, 27 проходят
├── assets/                        # Извлечённые из .dz ассеты
├── reverse/binaries/              # ShadowFight2.s86 (30630 функций), .android.bin
├── sf2_pc/www/                    # sf2_beautified.js (80205 строк)
├── PORT_PLAN.md                   # Мастер-план (1266 строк)
├── CMakeLists.txt
└── build.bat
```

## Сборка

```
build.bat
```

Требования: CMake >= 3.24, MSVC, C++23, Windows SDK.

Linux — только compile check: `bash scripts/verify_main_compile.sh`.

## Запуск

```
resf2_app.exe --assets E:\reSF2
```

| Флаг | Назначение |
|------|------------|
| `--assets <path>` | Корень ассетов |
| `--scene <name>[:N]` | Перейти к конкретному экрану |
| `--debug-world` | World geometry overlay (F1) |
| `--input-script <file>` | Детерминированный прогон (вместе с `--max-frames`) |
| `--dump-state` | Трассировка состояния в stdout |

## Тесты

27 тестов, все проходят. Ключевые:

| Тест | Проверки |
|------|----------|
| test_world_geometry | 41 чек — координатная система |
| test_moves_semantics | 4316+ чек — семантика moves.xml |
| test_effect_curve | 49 чек — SimpleEffect/Transparency |
| test_input_trace | Интеграционный — трассировка ввода |
| test_input_handler | 8 double-tap кейсов |
| test_step_cooldown | Интеграционный — 200 мс gate |
| test_dz_archive | 120 + 557 файлов байт-в-байт |

Детерминированный прогон: `resf2_test.exe --input-script replay.txt --max-frames 1000`.

## Соглашения

- Все константы помечены `[ORIGINAL]` (адрес бинарника) или `[HEURISTIC-TODO]`.
- Стиль: `trailing_underscore_` для members, `kPascalCase` для констант, `camelCase` для функций.
- Язык: C++23, CMake, GLFW + OpenGL 2.1 / GLES2.
- Коммиты — компилирующимися шагами, без тихих фоллбэков без `TODO` + warning-лога.

## Внешние ссылки

- S3ELoader (Ghidra loader): https://github.com/knot126/S3ELoader
- Marmalade-Modding: https://github.com/knot126/Marmalade-Modding
