# reSF2 — Project

**Tech stack:** C++17, OpenGL ES 2.0. Modules: `core/scene` + `core/app` + `core/data` + `core/render`; demo/probe apps in `app/`; oracle shell in `shell/OracleShell` (WinForms + WebView2).

**Goal:** 1:1 gameplay port of the **web** release of Shadow Fight 2, whose sole
reference implementation is `reference/www/sf2.502f0946.js`.

**Constraints:**
- Sole source of truth: web JS (`reference/www/sf2.502f0946.js`) + live
  OracleShell traces over `reference/www/`. Every gameplay fact must cite a JS
  line number or an oracle trace field.
- No native/mobile binaries (the Symbian-era executable, the Unity mobile
  library, proprietary mobile asset archives, ARM dumps) as reference.
  `engine/` is deleted.
- Everything under `.planning/archive/` is stale native/Unity RE and is
  INVALID as reference (kept for history only).
- Gate thresholds are fixed BEFORE their wave; blockers are reported as
  `BLOCKED: reason`, criteria are never lowered silently.
- Commit + push to `origin main` after every wave; update
  `.planning/MASTER_TODO.md` with each wave.

**Key contacts:** (fill in)
