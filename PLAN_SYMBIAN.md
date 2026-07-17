# План портирования reSF2 на Symbian (Nokia N8)

## 0. Текущее состояние reSF2 (честная оценка)

### Что РАБОТАЕТ (но сыро/криво)
- **Загрузка Dojo**: Парсинг params.xml через примитивный string-based XML (`xml_attr`), работает только для dojo. Криво загружает слои — найдены проблемы с парсингом Layer/Image тэгов. Параллакс работает, но с артефактами.
- **Рендер персонажа**: 82 капсулы + 29 треугольников из body.xml. Силуэт отрисовывается, но есть проблемы с Y-позиционированием (feet clipping, floating при roll).
- **Скелетная анимация**: Загружает все 556 .bin файлов, базовая интерполяция между ключами работает. Root motion NPivot X работает, Y — хак с постоянной.
- **Verlet физика**: Punching bag качается, импульсы от ударов работают.
- **Hit detection**: Использует Attack интервалы из moves.xml, проверяет AttackingParts edges. **НЕ ВСЕГДА корректно** — баг с hit без анимации.
- **Combat system**: Базовый выбор движений из moves.xml. 1key/2key/3key работает частично. Uninterrupt интервалы проверяются.
- **moves.xml парсинг**: Примитивный string-based, находит <Move> тэги, Template строки, Intervals, Attack edges. Пропускает многие поля, **парсит криво** (например Distance условия, Locks секции).

### Что НЕ РАБОТАЕТ / СЛОМАНО
- **DZ type=4 декомпрессия**: Полностью не работает (fallback к pre-extracted файлам).
- **Настоящий AI противника**: Только punching bag (заглушка).
- **Audio**: Нет звука вообще.
- **Magic/ranged**: Только Fists (unarmed).
- **Другие локации**: Только dojo.
- **Сохранение/загрузка**: Заглушка.
- **Переходы между сценами**: Сцены есть (9 штук), но Map/Shop/Settings/Dialogue — пустышки.
- **Combat timing**: Uninterrupt интервалы не совпадают с оригиналом (проверено).
- **Move transitions**: Нет правильных transition frames из moves.xml.
- **Ошибки в moves.xml парсинге**: Interval вырезает через string поиск, теряются ComplexInterval, Conditions с несколькими CurrentAnimation, MoveInside Pivot парсинг неполный.
- **Управление**: GetAsyncKeyState на Windows, GLFW на Linux. Нет touch/тачскрина.

## Этапы портирования

### Этап 1: Подготовка окружения и структуры

#### 1.1 Установка инструментов (требуется от пользователя)
```
1. JRE 5.0 (обязательно 1.5, новее ломает эмулятор)
2. S60 5th Edition SDK (Symbian^1, OS 9.4)
   - Или S60 3rd FP2 SDK с доустановкой 5th Edition
3. GCCE 3.4.3 (ARM compiler, идёт с SDK)
4. Perl 5.6-5.8 (для epocrc.pl, abld.pl, bldmake.pl)
5. EKA2L1 (эмулятор Symbian на Windows 10/11)
6. Carbide.c++ 2.7 (IDE, опционально)
```

#### 1.2 Структура symbian-проекта
```
sf2_symbian/
├── group/
│   ├── bld.inf              — Build info (GCCE + WINSCW)
│   └── resf2.mmp             — Project definition
├── src/
│   ├── clib_stubs.cpp        — C runtime stubs (math, string, stdio)
│   ├── stl_stubs/            — WINSCV header shims
│   │   ├── stdarg.h
│   │   ├── stddef.h
│   │   ├── stdint.h
│   │   ├── stdlib.h
│   │   ├── stdio.h
│   │   ├── math.h
│   │   ├── string.h
│   │   └── assert.h
│   ├── engine/
│   │   ├── platform_symbian.cpp / .hpp
│   │   ├── renderer_symbian.cpp / .hpp  (OpenGL ES 2.0)
│   │   └── input_symbian.cpp / .hpp     (touch + keyboard)
│   ├── game/
│   │   ├── game.cpp / .hpp              (C++03 rewrite of main.cpp Game class)
│   │   ├── location.cpp / .hpp          (location loading/rendering)
│   │   ├── skeleton.cpp / .hpp          (skeleton + body model)
│   │   ├── animation.cpp / .hpp         (.bin animation loader + player)
│   │   ├── moves.cpp / .hpp             (moves.xml parser)
│   │   ├── combat.cpp / .hpp            (hit detection, move selection)
│   │   └── hud.cpp / .hpp               (HUD rendering)
│   ├── containers/
│   │   ├── hash_map.hpp                 (custom hash map, no std::unordered_map)
│   │   ├── string.hpp                   (custom string, no std::string)
│   │   └── vector.hpp                   (custom vector, no std::vector)
│   ├── reverse/
│   │   ├── dz_reader.cpp / .hpp
│   │   ├── dz_decoder.cpp / .hpp
│   │   ├── plist_atlas.cpp / .hpp
│   │   ├── bitmap_font.cpp / .hpp
│   │   └── atf_tactics.cpp / .hpp
│   └── main.cpp                         — Entry point (Symbian app)
├── data/
│   ├── resf2.rss                        — App resources
│   └── resf2_reg.rss                    — Registration resource
└── sis/
    └── resf2.pkg                        — Installer package
```

### Этап 2: Адаптация кода под C++03 (GCCE 3.4.3)

#### Запрещённые конструкции (заменить)
| C++23 | C++03 замена |
|-------|-------------|
| `auto x = ...` | явный тип |
| `nullptr` | `NULL` (определён как `0`) |
| `override` | убрать |
| `constexpr` | `#define` или `const` |
| `noexcept` | `throw()` или убрать |
| `range-for` | обычный `for` с итератором |
| `std::unordered_map` | `RHashMap<K,V>` самописный |
| `std::string` | `char*` + `RString` или Symbian `TBuf`/`HBufC` |
| `std::vector` | `RArray<T>` или `RPointerArray<T>` |
| `std::filesystem` | `RFs` + `RFile` + `TParse` |
| `std::unique_ptr` | сырой указатель |
| `std::function` | указатель на функцию |
| `std::string_view` | `const char*` + длина |
| `std::span` | `const char*` + длина |
| lambda | отдельная функция или struct operator() |
| variadic templates | varargs C-style |
| initializer_list | массивы + count |
| `auto&&` | явная ссылка |
| `= default` | реализовать руками |
| `= delete` | private: не объявлять |
| `[[nodiscard]]` | убрать |
| `std::optional` | указатель + bool |
| `std::expected` | отдельный enum error + T* |

#### 🔥 Критические изменения в main.cpp (4190 строк Game class)
Game class использует:
- `std::unordered_map` для: `atlases_`, `skeleton_nodes_`, `skeleton_edges_`, `body_model_->nodes/edges`, `animations_`, `moves_`, `anim_node_pos_`, `hud_textures_`, `menu_textures_`, `scroll_textures_`, `bag_verlet_`
- `std::vector` для: `ordered_node_names_`, `loading_images_`, `bag_constraints_`, `dialogue_lines_`, `completed_levels_`
- `std::string` везде
- `std::filesystem::path` для поиска путей
- `std::unique_ptr` для текстур, рендерера, location, body_model, bag_model
- Range-for в десятках мест
- Auto в десятках мест
- Lambda в `w2s` и подобных

#### Контейнеры для замены
Нужно создать легковесные аналоги:
```cpp
template<typename K, typename V>
struct RHashMap {
    struct Entry { K key; V val; bool used; };
    Entry* entries;
    int count, capacity;
    // find(), insert(), erase(), operator[]
};
```
`RString` — обёртка над `char*` с copy-on-write или `HBufC*`.

**Альтернатива проще**: Использовать Symbian `RPointerArray` + `TBuf<256>`, и массивы структур для малого числа элементов. Для maps — написать минимальную implementation с linear probing.

### Этап 3: Symbian Platform Implementation

#### SymbianPlatform : Platform
```cpp
class SymbianPlatform : public Platform {
    // Window: fullscreen 640x360 (N8 native)
    // EGL: OpenGL ES 2.0 context (N8 GPU Broadcom BCM2763)
    // Input: touch events via CCoeControl / AknTouch
    // Files: RFs + RFile for asset reading
    // No threads — single-threaded with active objects
    // Time: User::TickCount() or RTimer for timing
};
```

**Отличия от PvZ-N95-Port (N95)**:
- N8 имеет **OpenGL ES 2.0** (не 1.1) — можно GLSL shaders
- N8 640×360 (N95 320×240) — больше экран
- N8 **ёмкостный** touch (N95 резистивный)
- N8 ARM Cortex-A8 @ 680 МГц (N95 ARM11 @ 332 МГц) — быстрее
- N8 имеет аппаратный акселерометр
- N8 Symbian^1 (OS 9.4) vs N95 S60 3rd FP1 (OS 9.2)

#### OpenGL ES 2.0 настройка
```cpp
// N8 GPU requires:
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
// Use VBOs for capsule/triangle rendering
// POT textures, non-POT on N8 works but slower
// Shaders: simple vertex+ fragment for silhouette rendering
```

**Важно**: Текущий десктопный рендерер использует OpenGL 2.1 compatibility profile с `glBegin/glEnd`? Нет, он использует шейдеры через `gl_loader.hpp`. Нужно проверить совместимость с GLES2.

### Этап 4: Портирование рендерера

Текущий `engine/renderer/`:
- `renderer.cpp` — draw_textured_quad, draw_filled_triangle_world, draw_filled_rect_screen и т.д.
- `gl_loader.cpp` — GL function pointers
- `software_renderer.cpp` — headless software renderer

Для GLES2 нужно:
1. Убрать `glBegin/glEnd` если есть (их нет в GLES2)
2. VBO для геометрии (можно immediate-mode через vertex arrays)
3. GLSL shaders вместо fixed-function
4. `egl` вместо `glfw` для window/context

**Стратегия**: Сделать `EglRenderer` на основе `renderer.hpp` интерфейса:
```cpp
class SymbianRenderer : public renderer::Renderer {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    GLuint program;  // simple 2D shader
    GLuint vbo;      // vertex buffer
    
    void draw_filled_triangle_world(float,float,float,float,float,float, Color4B);
    void draw_textured_quad(Texture2D&, float,float,float,float, ...);
    // ...
};
```

### Этап 5: Система управления (Touch + Клавиатура)

Nokia N8 имеет:
- `AknTouch` / `AknKeypad` — для ввода
- Ёмкостный touch (поддерживает мультитач до 2 пальцев)
- Физическая QWERTY клавиатура (слайдер)
- Аппаратные кнопки: Menu, Send, End, Volume, Camera

**Touch mapping для SF2:**
- Тач слева — двигаться влево
- Тач справа — двигаться вправо  
- Тач+свайп вверх — прыжок
- Тач+свайп вниз — присесть
- Кнопка "О" (экранная) — удар
- Кнопка "Р" (экранная) — пинок
- Направление удара = позиция тача

Или более простая схема:
- Левая половина экрана — 4 кнопки направлений (стрелки)
- Правая половина — кнопки О и Р
- Свайпы для спецприёмов (S+D, S+A и т.д.)

### Этап 6: Asset path resolution

Текущий код:
```cpp
for (const auto& dir : {root/"assets"/"1536"/"locations"/loc, ...}) { ... }
```

На Symbian нет `std::filesystem`. Нужно:
```cpp
_LIT(KAssetsDir, "C:\\Data\\reSF2\\");
_LIT(KLocDir, "C:\\Data\\reSF2\\locations\\");
// RFile::Open + проверка KErrNone
```

### Этап 7: Оптимизации для Nokia N8

- RAM: 256 MB — хватит для SF2 (оригинал ~100MB assets)
- CPU: 680 МГц Cortex-A8 — достаточно для 2D
- GPU: Broadcom BCM2763 — OpenGL ES 2.0 с VBO
- Display: 640x360 — нужно scaling или адаптация UI
- Assets: ~100MB нужно копировать на карту памяти

## Сборка проекта (инструкция)

### Предварительные требования

1. Установить JRE 5.0 (http://www.oldversion.com/windows/java-platform-runtime-5-0-update-5)
2. Установить S60 5th Edition SDK или S60 3rd FP2 SDK + 5th Ed add-on
3. Установить Perl 5.6-5.8 (например Strawberry Perl)
4. Добавить в PATH:
   ```
   C:\Symbian\9.4\S60_5th_Edition_SDK_v1.0\Epoc32\tools\
   C:\Program Files\Common Files\Symbian\Tools\
   ```
5. Скачать EKA2L1 (https://github.com/EKA2L1/EKA2L1/releases)
6. Установить прошивку Nokia N8 в EKA2L1

### Сборка

```bat
cd sf2_symbian\group\
call bldmake bldfiles
call abld build gcce urel
```

**ВАЖНО**: `abld.pl` возвращает ненулевой код даже при успехе. Проверять по наличию `.exe`:
```bat
if not exist "%EPOCROOT%Epoc32\release\gcce\urel\resf2.exe" (
    echo BUILD FAILED
    exit /b 1
)
```

### Упаковка SIS

```bat
cd sis\
makesis resf2.pkg
signsis resf2.sis resf2.sisx mycert.cer mykey.key
```

### Установка на устройство/EKA2L1

1. Скопировать `.sisx` на телефон (или установить через EKA2L1)
2. Скопировать ассеты на карту памяти: `E:\reSF2\` или `C:\Data\reSF2\`
3. Запустить приложение

## Связь с PvZ-N95-Port

Аналогии с https://github.com/hwindinkg/PvZ-N95-Port:
- Подход: портирование десктопного/кроссплатформенного движка на Symbian
- Решение тех же проблем: C++11→C++03, STL→Symbian API, GL→GLES
- **Отличия**: PvZ-N95-Port базируется на PvZ-Portable (C, OpenGL 1.1). Наш движок — C++23 с открытым кодом.
- PvZ-N95-Port использует CFbsBitmap для рендера (software), мы можем использовать OpenGL ES 2.0 (аппаратное ускорение).
- N95: PowerVR MBX (GLES 1.1, 320x240). N8: Broadcom BCM2763 (GLES 2.0, 640x360).

## Риски

1. **Размер проекта**: >4000 строк в main.cpp потребуют разбивки на модули
2. **C++23 → C++03**: Масштабная механическая замена
3. **Отсутствие STL**: Писать свои контейнеры
4. **Память**: Утечки памяти (нет unique_ptr) нужно тщательно отслеживать
5. **Исключения**: Symbian leaves vs C++ exceptions
6. **Эмуляция**: EKA2L1 может не поддерживать все системные вызовы GLES2
7. **Производительность**: 60fps на Cortex-A8 с 256MB — нужно оптимизировать

## Немедленные шаги

1. ✅ Создать ветку `symbian`
2. ⬜ Создать скелет проекта (MMP, bld.inf, PKG)
3. ⬜ Написать C runtime stubs (math, string, printf)
4. ⬜ Написать STL shims (string, vector, map)
5. ⬜ Создать SymbianPlatform (EGL init, RFs, touch input)
6. ⬜ Портировать рендерер (GLES2 shaders)
7. ⬜ Разбить Game class на файлы (code first, compile with GCCE next)
8. ⬜ Адаптировать asset loading под Symbian paths
9. ⬜ Адаптировать управление под touch
10. ⬜ Собрать .sisx и протестировать
