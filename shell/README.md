# OracleShell

Windows shell for the standalone web game (the ORACLE): a minimal WinForms app
with an embedded WebView2 control that serves `reference/www/` over a local
HTTP server and runs the game through the browser `GameInterface`.

## Build

```powershell
dotnet build shell/OracleShell.csproj -c Release
```

## Run

```powershell
dotnet run --project shell/OracleShell.csproj -c Release
# or run the built exe directly:
shell/bin/Release/net9.0-windows/OracleShell.exe
```

## What it does

1. Serves `reference/www/` over `System.Net.HttpListener` on `127.0.0.1` at a
   random free port.
2. At startup copies the runner files into `www/` (idempotent, overwrite):
   - `reference/runner/index.html` → `reference/www/index.html`
   - `reference/runner/microsite-game-interface.js` → `reference/www/microsite-game-interface.js`

   This swaps the WinUI bridge for the browser `GameInterface`.
3. Navigates WebView2 to `http://127.0.0.1:<port>/`.
4. Logs all WebView2 console messages to `reference/traces/console.log`
   (append, timestamped).
5. Captures screenshots to `reference/traces/boot.png` (~8s after navigation)
   and `reference/traces/boot2.png` (~20s after navigation).
6. Window: 1280x720, resizable, title "reSF2 Oracle", dark background.
   Closing the window stops the server and exits.

## Phase 1 oracle instrumentation

- `reference/tools/trace_oracle.js` (master copy, tracked) is injected via
  WebView2 `AddScriptToExecuteOnDocumentCreated`, i.e. it runs BEFORE any page
  script. It pins entropy (frozen `Date`, seeded `Math.random` replacement —
  game code untouched, pins recorded in every trace header), wraps `de.Pqb` /
  `de.ia` / `iPa` / `N0a` (log in/out to `window.__trace`), and emits one
  `[ORACLE] {json}` console line per fight frame (see
  `reference/traces/README.md` for the record format).
- Each run launches with a fresh browser profile (temp dir, clean
  localStorage/saves) so repeated runs start from identical game state.
- The shell closes automatically when the oracle trace signals done
  (`window.__oracleDone`, or the v2 tracer's done flag), or after a 150s
  safety timeout. `--input-script` drives taps/keys/waits (see
  `reference/tools/input_phase1.txt` for the fixed Phase 1 gate script).

```powershell
dotnet run --project shell/OracleShell.csproj -c Release -- --input-script reference/tools/input_phase1.txt
```

## Requirements

- .NET 9 SDK
- WebView2 Runtime (Evergreen)