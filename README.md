# reSF2 — Clean-room reimplementation of Shadow Fight 2

A reverse-engineered recreation of the Shadow Fight 2 game engine,
built from analysis of the original Android (ARM) and Windows (x86) binaries.

**⚠️ ОЧЕНЬ СЫРОЙ ДВИЖОК. НЕ ИГРА, А ТЕХНИЧЕСКОЕ ДЕМО.**

Текущая кодовая база — proof-of-concept с множеством известных и неизвестных дефектов.
См. [PLAN_SYMBIAN.md](PLAN_SYMBIAN.md) для плана портирования на Symbian (Nokia N8).

## Текущее состояние (честно)

### Работает, но сыро/криво ⚠️
- **Загрузка локации Dojo**: Парсинг params.xml через самодельный string-based XML парсер (`xml_attr`). **Некорректно загружает слои** — Layer/Image тэги парсятся с ошибками позиционирования. Параллакс работает с артефактами.
- **Рендер персонажа**: 82 капсулы + 29 треугольников из body.xml. Тёмный силуэт. Y-позиционирование — набор хаков (feet clipping, floating при roll).
- **Скелетная анимация**: Загружает 556 .bin файлов. Корневая анимация NPivot X работает. NPivot Y — **сломан** (MoveInside alignment не доделан).
- **Verlet физика**: Punching bag качается, импульсы работают.
- **Hit detection**: Использует Attack интервалы из moves.xml. **Не всегда корректно** — баг с "hit без анимации".
- **Combat**: 1key/2key/3key работает частично. Uninterrupt проверяется.
- **moves.xml парсинг**: Примитивная XML-резка строк. **Криво парсит** — теряет ComplexInterval, некорректно парсит Distance условия, Locks секции, MoveInside Pivot.
- **DZ архивы**: gzip (type=8) работает. type=4 декомпрессия **полностью сломана** — использует fallback к pre-extracted файлам.
- **Scene manager**: 9 сцен есть, но Map/Shop/Settings/Dialogue — **пустышки** (только заглушки).

### Не сделано ❌
- **Настоящий AI противника**: Только punching bag
- **Audio**: Нет звука
- **Magic/ranged**: Только Fists (unarmed)
- **Другие локации**: Только dojo
- **Сохранение/загрузка**: Заглушка (JSON в temp)
- **Combat timing**: Uninterrupt интервалы не совпадают с оригиналом
- **Move transitions**: Нет transition frames
- **Управление**: Только клавиатура. Нет touch/тачскрина

## Архитектура

### Engine Structure
```
main.cpp                    — Game logic + SceneHost (~4190 lines, МОНОЛИТ)
engine/scene/               — Scene/state manager (9 scenes, заглушки)
engine/platform/            — Platform abstraction (GLFW на Windows/Linux)
engine/renderer/            — OpenGL 2.1 / GLES2 рендерер
engine/reverse/             — Парсеры форматов:
  - s3e_container.cpp       — Marmalade S3E container
  - plist_atlas.cpp         — Cocos2d TexturePacker v2
  - atf_tactics.cpp         — zlib-compressed tactics blob
  - bitmap_font.cpp         — AngelCode BMFont
  - dz_reader.cpp           — DZ archive reader (DTRZ)
  - dz_decoder.cpp          — DZ type=4 range coder decoder (СЛОМАН)
assets/models/              — 72 model XML файла
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

## Symbian порт (Nokia N8)

Ведётся работа в ветке `symbian`. См. [PLAN_SYMBIAN.md](PLAN_SYMBIAN.md) для:
- Подробный план портирования
- Инструкция по сборке
- Анализ отличий от PvZ-N95-Port

## Next Steps

1. **Сначала Symbian порт**: C++23→C++03, SymbianPlatform, GLES2, touch input
2. **Потом фиксы багов**: DZ decoder, moves.xml парсер, dojo location
3. **Потом контент**: AI, audio, weapons

## Reverse Engineering Documentation

See `docs/` for detailed RE notes.
