# UI screenshot harness — oracle side (run on YOUR machine)

The port captures `port_<name>.png` here via `game.exe --ui-tour`.
Oracle counterparts (`oracle_<name>.png`) need an interactive Windows
session with WebView2 (the harness env has no desktop — OracleShell
closes instantly there, verified). Produce them as follows:

## 1. Build the shell

```powershell
dotnet build shell/OracleShell.csproj -c Release
```

## 2. Run the UI tour (fresh profile every run — deterministic states)

```powershell
shell/bin/Release/net9.0-windows/OracleShell.exe `
  --input-script reference/tools/ui_tour_oracle.txt `
  --timeout-ms 600000
```

Script verbs (`shell/InputScript.cs`): `tap x y`, `move x y`, `key code`,
`wait ms`, `atframe …` (fight-exact), `shot <name>` (saves
`reference/traces/ui/oracle_<name>.png` at 1280x720 — the window is
fixed 1280x720, same coords as the port).

Current tour (`ui_tour_oracle.txt`): tutorial fight → move/punch/kick
probes → hub-button spot probes. Map/Shop/Profile/Results/Settings need
a completed tutorial (blind taps won't advance quest steps reliably);
extend the script per state once the tour flow is known, keeping one
`shot <state>` per state with the state names in `ui_diff.py` STATES.

## 3. Diff

```powershell
python reference/tools/ui_diff.py --all --dir reference/traces/ui
# single pair with different port name:
python reference/tools/ui_diff.py --pairs tut_fight:fight --dir reference/traces/ui
```

Output per pair: `%` pixels over threshold 12 + `diff_<name>.png`
(red overlay) + `side_<name>.png` (side-by-side).

## Thresholds (MASTER_TODO UI-GATE, proposed pre-fix, no post-fitting)

- Fight HUD (design-close): 25%. All other screens: 40% after the first
  oracle run. No screen may regress its own baseline. Quest-step matrix
  and modal states: open (fresh-profile captures only so far).
