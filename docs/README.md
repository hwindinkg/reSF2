# reSF2 — Documentation index

This directory holds all engineering documentation for reSF2.

## Stage 1 — APK investigation (complete)

| File                          | Topic                                                  |
| ----------------------------- | ------------------------------------------------------ |
| [`00_stage1_summary.md`](00_stage1_summary.md)             | Executive summary, key numbers, what Stage 1 proved |
| [`01_apk_structure.md`](01_apk_structure.md)               | APK layout, file inventory, manifest summary         |
| [`02_native_libraries.md`](02_native_libraries.md)         | 30 `.so` files, ABIs, dependencies, SmartFox2X API   |
| [`03_java_layer.md`](03_java_layer.md)                     | Java packages, components, permissions               |
| [`04_assets_inventory.md`](04_assets_inventory.md)         | 1 866 assets: textures, audio, XML, tactics          |
| [`05_resource_formats.md`](05_resource_formats.md)         | `.s3e`, `.dz`, `.atf`, `.plist`, `.fnt`, `.icf`, etc. |
| [`06_engine_marmalade.md`](06_engine_marmalade.md)         | Marmalade SDK v8.2.1, subsystems, main-loop model    |
| [`07_jni_and_loader.md`](07_jni_and_loader.md)             | JNI surface, boot sequence, reSF2 target boot        |
| [`08_reverse_engineering_log.md`](08_reverse_engineering_log.md) | Chronological RE log, dead ends, Stage 2 wishlist |

## Stage 2 — `.s3e` binary & JNI map (complete)

- `09_s3e_binary_format.md` — `XE3U` section table, relocations,
  entry-point resolution.
- `10_jni_registration_map.md` — full `RegisterNatives()` table for
  `libs3e_android.so` and key extensions.

## Stage 3 — Engine architecture recovery (complete)

- `11_engine_architecture.md` — recovered architecture from `.s3e`
  symbol analysis.
- `12_main_loop.md` — frame timing, update vs render paths.

## Stage 4+ (planned)

- `13_animation_format.md` — full `.dz` + `moves.xml` schema.
- `14_tactics_format.md` — full `.atf` byte layout.
- `15_renderer.md`, `16_physics.md`, `17_audio.md`, `18_ai.md`,
  `19_network.md`, `20_save_system.md`, `21_ui_cocogui.md`.
- `coding_style.md`, `testing.md`, `legal.md`, `threat_model.md`,
  `performance_budgets.md`.

## Cross-cutting

Documents that apply across all stages live in this same directory.

## Conventions

- File names: `NN_topic.md` where `NN` is a 2-digit zero-padded index.
- Internal cross-references: relative paths (`./01_apk_structure.md`,
  `../TODO.md`).
- Code blocks: tagged with language where applicable.
- Hex dumps: `xxd`-style (`offset  bytes  ascii`).
- All sizes are decimal unless suffixed with `0x` (hex) or `KiB`/`MiB`.
- All offsets are file offsets in bytes, decimal unless `0x`-prefixed.
