# reSF2 Testing Strategy

**Last verified**: 2026-07-22

## 1. Test Framework: Custom CHECK/CHECK_EQ Macros

reSF2 does **not** use an external test framework (no GTest, Catch2, doctest, etc.). Instead, each test executable defines its own minimal macros at the top of the source file:

```cpp
// Defined in every test file (identical pattern)
static int g_failures = 0;
static int g_tests    = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        ++g_tests;                                                      \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  CHECK(%s)\n",             \
                         __FILE__, __LINE__, #cond);                    \
        }                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                  \
    do {                                                                \
        ++g_tests;                                                      \
        if (!((a) == (b))) {                                            \
            ++g_failures;                                               \
            std::fprintf(stderr, "FAIL %s:%d  CHECK_EQ(%s, %s)\n",      \
                         __FILE__, __LINE__, #a, #b);                   \
        }                                                               \
    } while (0)
```

**Source**: `tests/test_platform_loop.cpp` lines 16-37, `tests/test_asset_loaders.cpp` lines 32-53, `tests/test_s3e_container.cpp` lines 28-49, `tests/test_xml_parsers.cpp` lines 10-41 (also defines `CHECK_MSG` variant).

**Exit pattern**: every test executable returns `g_failures == 0 ? 0 : 1` from `main()`, which is the CTest pass/fail signal.

### Two Test Styles

| Style | Files | Approach |
|-------|-------|----------|
| **Unit-test style** (CHECK/EQ macros) | `test_s3e_container`, `test_asset_loaders`, `test_asset_manager`, `test_platform_loop`, `test_xml_parsers`, `test_dz_decoder_util`, `test_dz_range_settings` | Runs many small test functions, each calling CHECK/CHECK_EQ. Prints summary: `"N tests, M failures"`. |
| **Integration/analysis style** (manual checks) | `test_stage_parser`, `test_list_parser`, `test_moves_parser`, `test_asset_pipeline`, `test_dz_decode`, `test_dz_first_byte`, `test_dz_prob_layout`, `test_json_atlas` | Loads real game files, prints parsed data with `std::printf`, does manual `fprintf` + `return 1` on failure. Functions as both test and RE analysis tool. |

## 2. Test Discovery via CTest

Tests are registered in `tests/CMakeLists.txt` (179 lines) using standard CMake `add_test()`. Tests are gated by the `RESF2_BUILD_TESTS` option (default ON):

```cmake
# Root CMakeLists.txt, line 116-119
if(RESF2_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

**Source**: `CMakeLists.txt` lines 116-119.

### WORKING_DIRECTORY Convention

Tests that load files from disk use `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` so that relative paths like `assets/stages.xml` resolve from the project root:

- `test_moves_parser` — line 62
- `test_asset_pipeline` — line 77
- `test_stage_parser` — line 88
- `test_list_parser` — line 99
- `test_xml_parsers` — line 110
- `test_dz_decode` — line 165
- `test_json_atlas` — line 179

Tests that use **only synthetic data** (no file I/O) omit `WORKING_DIRECTORY`:
- `test_s3e_container`, `test_asset_loaders`, `test_dz_decoder_util`, `test_dz_first_byte`, `test_dz_prob_layout`, `test_dz_range_settings`

## 3. Complete Test Inventory (15 executables)

### DZ Decoder Tests (6)

| Test | Source | Linked Libs | File I/O | Description |
|------|--------|-------------|----------|-------------|
| `test_dz_decoder_util` | `test_dz_decoder_util.cpp` | resf2_reverse, resf2_warnings | No | Bit tree decode, CRC32 window verification |
| `test_dz_first_byte` | `test_dz_first_byte.cpp` | resf2_reverse, resf2_warnings | `assets/files.dz` | First-byte decode tracer — **fails** without .dz file |
| `test_dz_prob_layout` | `test_dz_prob_layout.cpp` | resf2_reverse, resf2_warnings | No | Probability table layout analysis (LZMA-style) |
| `test_dz_range_settings` | `test_dz_range_settings.cpp` | resf2_reverse, resf2_warnings | No | RangeSettings header parser (synthetic header bytes) |
| `test_dz_decode` | `test_dz_decode.cpp` | resf2_reverse, resf2_warnings | `assets/files/*` | Integration test: decodes real DZ files, verifies ground truth |
| `test_s3e_container` | `test_s3e_container.cpp` | resf2_reverse, resf2_warnings | No | S3E container format parser (empty/too-small/bad-magic/truncated) |

### Asset Loaders (2)

| Test | Source | Linked Libs | File I/O | Description |
|------|--------|-------------|----------|-------------|
| `test_asset_loaders` | `test_asset_loaders.cpp` | resf2_reverse, resf2_warnings | `assets/assets/*` (optional) | plist_atlas / atf_tactics / bitmap_font — synthetic + real file tests |
| `test_asset_manager` | `test_asset_manager.cpp` | resf2_runtime, resf2_reverse, resf2_warnings, Threads | Temp dir (fixtures) | AssetManager lifecycle: load/cache/invalidate |

### Platform + Runtime (1)

| Test | Source | Linked Libs | File I/O | Description |
|------|--------|-------------|----------|-------------|
| `test_platform_loop` | `test_platform_loop.cpp` | resf2_platform, resf2_runtime, resf2_warnings, Threads | No | NullPlatform init/shutdown/quit, Loop start/stop/sleep timing |

### Parser Tests (5)

| Test | Source | Linked Libs | File I/O | Description |
|------|--------|-------------|----------|-------------|
| `test_stage_parser` | `test_stage_parser.cpp` | resf2_format, resf2_warnings | `assets/stages.xml` | Campaign structure: zones → battles → fights |
| `test_list_parser` | `test_list_parser.cpp` | resf2_format, resf2_warnings | `assets/list.xml` | Item catalog: types, upgrades, item sets |
| `test_xml_parsers` | `test_xml_parsers.cpp` | resf2_format, resf2_warnings | `assets/models/body.xml`, etc. | XML DOM parsers: body, skeleton, loading screen |
| `test_moves_parser` | `test_moves_parser.cpp` | resf2_fight, resf2_format, resf2_core, resf2_warnings | `assets/animations/moves.xml` | Move database: types, attack intervals, combos |
| `test_json_atlas` | `test_json_atlas.cpp` | resf2_format, resf2_core, resf2_reverse, zlibstatic, resf2_warnings | `assets/*` (game files) | JSON Atlas parser (sf2_pc format) |

### Asset Pipeline (1)

| Test | Source | Linked Libs | File I/O | Description |
|------|--------|-------------|----------|-------------|
| `test_asset_pipeline` | `test_asset_pipeline.cpp` | resf2_fight, resf2_format, resf2_core, resf2_reverse, resf2_warnings, zlibstatic | `assets/files.dz`, `assets/animations/moves.xml` | Full pipeline: DZ archive → XML → Moves/Location parsers |

## 4. Test Dependencies and Build

### Library Dependencies per Test

```
resf2_warnings    — all 15 tests (INTERFACE: warning flags / C++23)
resf2_reverse     — 10 tests (DZ decoder, S3E, assets, pipeline)
resf2_format      — 6 tests (stage/list/XML/moves/atlas parsers)
resf2_core        — 3 tests (moves, pipeline, atlas)
resf2_runtime     — 2 tests (asset_manager, platform_loop)
resf2_fight       — 2 tests (moves, pipeline)
resf2_platform    — 1 test  (platform_loop)
zlibstatic        — 2 tests (pipeline, atlas)
Threads::Threads  — 2 tests (asset_manager, platform_loop)
```

### Build Configuration

| Option | Default | Effect |
|--------|---------|--------|
| `RESF2_BUILD_TESTS` | ON | Builds all test executables |
| `RESF2_WERROR` | OFF | `-Werror` / `/WX` |
| `RESF2_ENABLE_SAN` | OFF | Address + UB sanitizers (Clang/GCC) |

**Source**: `CMakeLists.txt` lines 20-26.

## 5. No Mocking Framework

The project has **no mocking library** (no GMock, FakeIt, trompeloeil). Instead:

- **NullPlatform** (`engine/platform/platform.hpp`, line 240) provides a headless platform stub implementing all virtual methods with no-op or simple state tracking. Used in `test_platform_loop.cpp`.
- Tests use **synthetic in-memory data** (string literals, byte arrays from RE analysis) to avoid file I/O for unit tests. Example: `test_asset_loaders.cpp` has inline `kSamplePlist` and `kSampleFont` XML strings.
- Real-file tests **gracefully skip** when fixture files are absent, printing `SKIP test_xxx (no fixtures found)` instead of failing.

### NullPlatform Test Helpers

```cpp
// engine/platform/platform.hpp lines 270-278
void inject_quit_request() noexcept;
void inject_pause() noexcept;
void inject_resume() noexcept;
void inject_key_down(Key k) noexcept;
void inject_key_up(Key k) noexcept;
void inject_pointer_down(int32_t id, float x, float y) noexcept;
void inject_pointer_up(int32_t id) noexcept;
void inject_pointer_move(int32_t id, float x, float y) noexcept;
```

## 6. Pre-Existing Issues

### test_dz_first_byte fails

**Root cause**: `test_dz_first_byte.cpp` opens `assets/files.dz` using `fopen_s(&f, "assets/files.dz", "rb")` (line 11). Unlike the other file-backed tests, it does **not** set `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` in CMake. When CTest runs, the working directory is the build directory, so `assets/files.dz` is not found and the test returns 1 (failure).

**Scaffold**: `tests/CMakeLists.txt` line 132:
```cmake
add_test(NAME test_dz_first_byte COMMAND test_dz_first_byte)
# Missing: WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
```

This is a **pre-existing** issue (the .dz file exists at `E:\reSF2\assets\files.dz`).

## 7. Code Coverage

No code coverage infrastructure is configured. There is no `--coverage`, `gcov`, `llvm-cov`, or `OpenCppCoverage` integration in the CMake files.

## 8. Summary

| Aspect | Status |
|--------|--------|
| Test framework | Custom CHECK/CHECK_EQ macros (no external dependency) |
| Test runner | CTest (`ctest`) |
| Total tests | 15 executables |
| Test file location | `tests/` directory, one `.cpp` per executable |
| Main pattern | `int main()` returns `g_failures == 0 ? 0 : 1` |
| File I/O pattern | `WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"` for file-backed tests |
| Mocking | NullPlatform + synthetic in-memory fixtures |
| Coverage | Not configured |
| Failing tests | `test_dz_first_byte` — missing `WORKING_DIRECTORY` |
| C++ standard | C++23 throughout |
| External test deps | None (zlib for pipeline/atlas tests only) |
