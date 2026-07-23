## 2026-07-21 — DZ Derbh range decoder reverse engineering — UltraWork session
**Severity:** high
**Mistake:** Assumed uniform LZMA-style prob init (0x400 for all) would work for DZ format
**Lesson:** DZ uses non-uniform probability table initialization. The init function is proprietary (passed by function pointer in ARM binary). Uniform init changes fix first byte but cascade errors through full decode. Need to dump real init table via ARM emulation or find source code.
## 2026-07-21 — Building C++ dzip.exe wrapper for DZ archive decompression
**Severity:** medium
**Mistake:** Overcomplicated ARM emulation instead of using the working dzip.exe subprocess approach
**Lesson:** When reverse engineering a proprietary algorithm (Derbh/DZ), first check if the original tool (dzip.exe) can be used as a subprocess wrapper. This is faster, more reliable, and produces correct output immediately. The ARM emulation approach is useful for understanding the algorithm but not necessary for a working implementation.
## 2026-07-21 — reSF2 engine: show_enemy_ default was true, meaning the dojo rendered an enemy fighter instead of the punching bag. The bag Verlet physics code was complete but never displayed because the render path showed the enemy. Default should be bag (training mode) in dojo, enemy in real battles.
**Severity:** medium
**Mistake:** show_enemy_ defaulted to true, hiding the punching bag with all its Verlet physics. User reported "bag doesn't react to hits" but the bag was never rendered.
**Lesson:** In training/dojo mode, default to punching bag (show_enemy_=false). Reserve enemy fighter toggle for testing. The render path at host_render_scene branches on this flag — ensure the default matches the expected experience.
## 2026-07-21 — reSF2 engine: Added set_clear_color(0.05,0.05,0.1) in SceneManager::apply_transition() to fix a brown screen on Map entry. This caused ALL scene transitions to render with a dark blue background, breaking dojo, battle, and dialogue backgrounds.
**Severity:** high
**Mistake:** Added a global clear_color override in the SceneManager transition method, ignoring that each scene already sets its own clear_color via on_enter.
**Lesson:** Scene transitions must not override clear_color. Each scene sets its own clear_color in on_enter. Injecting a global clear_color in apply_transition breaks every scene's background color.
## 2026-07-22 — Security audit of reSF2 Phase 1 — local desktop game engine
**Severity:** low
**Mistake:** Initial secret scan returned many false-positive "token" matches (VP8 bitstream parser, BMFont tokenizer, HDR image parser) — these are legitimate parsing usage, not security tokens.
**Lesson:** When searching for secrets in C++ game engine code, exclude "token" unless context is credential-related. VP8 decoders, BMFont parsers, and HDR image loaders all use "token" as a parsing term.
