# Промпт для следующей сессии

## Что было сделано

1. Создана ветка `symbian` от `main`
2. Составлен подробный план портирования: `PLAN_SYMBIAN.md` (текущее состояние, этапы, риски, сборка)
3. Обновлён `README.md` с честным состоянием проекта
4. Создан скелет Symbian-проекта:
   - `sf2_symbian/group/bld.inf`
   - `sf2_symbian/group/resf2.mmp`
   - `sf2_symbian/sis/resf2.pkg`
   - `sf2_symbian/data/resf2.rss` + `resf2_reg.rss`
   - `sf2_symbian/src/clib_stubs.cpp` (C runtime stubs)
   - `sf2_symbian/src/stl_stubs/` (STL header shims for WINSCW)
   - `sf2_symbian/src/main.cpp` (Symbian app entry)
5. Созданы заглушки для SymbianPlatform, renderer, input

## Что нужно сделать в следующей сессии

### Приоритет 1: Заставить скомпилироваться базовый проект

1. Скопировать реверсинженерные модули (dz_reader, plist_atlas, atf_tactics, bitmap_font, s3e_container) из основного engine/reverse/ в sf2_symbian/src/reverse/
2. Адаптировать их под C++03 (убрать auto, range-for, std::vector/string → массивы)
3. Собрать `bldmake bldfiles` + `abld build gcce urel`, отловить ошибки компиляции

### Приоритет 2: Минимальный EGL init + загрузка dojo

1. Реализовать EGL инициализацию (дисплей, контекст, поверхность)
2. Загрузить dojo params.xml + plist атласы (Symbian RFs вместо std::filesystem)
3. Отобразить dojo background на экране

### Приоритет 3: Анимация персонажа

1. Портировать .bin загрузчик
2. Адаптировать skeleton.xml парсер
3. Отобразить персонажа в стойке

### Известные проблемы движка (требуют фикса в процессе):

- **moves.xml парсер**: `main.cpp:2834-3120` — парсинг через `xml.find("<Move ")` и `xml_attr()`. Криво находит Interval, теряет ComplexInterval, Distance условия, Locks. Нужен SAX/ручной парсер.
- **Dojo location загрузка**: `main.cpp:1660-1754` — `parse_location()` — парсинг Layer/Image через примитивный string search. Пропускает SimpleEffect, ModelsViewer парсится криво.
- **Y-позиционирование**: `main.cpp:3393-3407` — y_adjust = 0 через MoveInside формулу. Визуально персонаж летает над полом или проваливается. Нужно отладить.
- **Hit detection**: `main.cpp:1224-1449` — проверка AttackingParts edges. Не все эджи правильно маппятся на skeleton.xml. Баг "hit без анимации".
- **Uninterrupt интервалы**: `main.cpp:1184-1212` — проверка только для attack анимаций, не совпадает с оригиналом.

## Инструкция для следующей сессии

1. `git checkout symbian` — уже в ветке
2. Начать с `sf2_symbian/`
3. Сначала скомпилировать пустой проект (просто main.cpp с EGL init)
4. Потом добавлять модули по одному
5. Проверять сборку через `abld build gcce urel` (не забыть проверять по .exe, не по errorlevel!)
6. Тестировать на EKA2L1 (скопировать .sis + ассеты)
