# Standalone oracle runner (`reference/runner/`)

Turns the store build in `reference/www/` into a standalone, browser-runnable
game by swapping the WinUI host bridge for a browser-only `GameInterface`.
No Famobi SDK, auth, ads, or IAP host is required.

## What this is

- `reference/www/` holds the complete ingested web game (gitignored):
  `sf2.502f0946.js` (Haxe→JS engine), `fflate.4d6ec944.js`,
  `imageloader.665d5d2b.js`, `res/` (assets), and `index.html` +
  `windows-game-interface.js` (WinUI host bridge — needs the store host, NOT
  usable standalone).
- `reference/runner/` is the standalone variant:
  - `microsite-game-interface.js` — a browser-only GameInterface replacement
    from the recovery repo `dinglenutsxnex-crypto/shadow-fight-2-recovery`,
    proven to run the game standalone on their live site.
  - `index.html` — wires that interface to OUR store files (same boot sequence
    and init array as the store build; only the interface file differs).

## How to serve it

The runner files must live alongside the game files (same origin, sibling
scripts). The shell copies `reference/runner/` into `reference/www/` at
startup. To serve manually:

1. Copy `microsite-game-interface.js` and `index.html` from `reference/runner/`
   into `reference/www/` (back up the store's `index.html` /
   `windows-game-interface.js` if you want to restore the original host build).
2. Serve the directory with a static HTTP server rooted at `reference/www/`:
   - Python: `python -m http.server 8000`
   - Node:   `npx serve .`
3. Open `http://localhost:8000/` in a WebGL2-capable browser.

> `reference/www/` is gitignored — only the runner sources in
> `reference/runner/` are tracked.

## Boot sequence

1. `microsite-game-interface.js` loads first (defines `window.GameInterface`).
2. An inline script checks WebGL2 support and shows a fallback screen if the
   browser cannot create a WebGL2 context.
3. `window.GameInterface.init([...])` fetches the game scripts in order:
   `fflate.4d6ec944.js`, `imageloader.665d5d2b.js`, `sf2.502f0946.js`
   (the same array as the store build's index.html — only the interface file
   differs).
4. `.then(() => SF2.main())` boots the engine.

Note: a missing `famobi.json` is fine — the interface's loader swallows fetch
failures and falls back to built-in defaults (the recovery site also runs
without one).
