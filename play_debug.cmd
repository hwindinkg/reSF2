@echo off
rem ============================================================================
rem  play_debug.cmd - the SAME launch as play.cmd, but boots a fight (FIRST BOSS
rem  by default) with EVERY trace on, so the developer can analyse YOUR fight:
rem
rem    fight_log.txt        - per frame: [anchor] (model position, clip
rem                           placement, drawn-pose COM, solver state), [pos],
rem                           [align], [cap], [ragdoll]/[rdseed], hits, wall
rem                           reactions, stretched triangles, cloth edges
rem    native_pose.jsonl    - EVERY bone of BOTH fighters, EVERY fight frame
rem    native_state.jsonl   - fight state (rules, callouts, capsules, AI)
rem    native_audio.jsonl   - the audio event stream for the same frames
rem    summary.txt          - a first-pass COM-jump table (so you can see the
rem                           event was captured) - optional, needs python
rem
rem  HOW TO USE: run it, play the fight (hit, THROW, push into a wall, get
rem  knocked down), close the window. The script then zips the folder and
rem  prints its path - send that zip.
rem
rem  Usage:
rem    play_debug.cmd                     first boss (BOSS_LYNX, bamboo grove)
rem    play_debug.cmd BOSS_BUTCHER        another battle id (stages.xml name)
rem    play_debug.cmd BOSS_LYNX arena     ... and a different location
rem ============================================================================
setlocal
set "ROOT=%~dp0"
set "BATTLE=%~1"
if "%BATTLE%"=="" set "BATTLE=BOSS_LYNX"
set "LOC=%~2"
if "%LOC%"=="" set "LOC=bamboo_grove"
set "OUT=%ROOT%.harness\debug_logs\%DATE:~-4%%DATE:~3,2%%DATE:~0,2%_%TIME:~0,2%%TIME:~3,2%%TIME:~6,2%"
set "OUT=%OUT: =0%"
if not exist "%OUT%" mkdir "%OUT%"

rem --- model / pose / placement -------------------------------------------
set SF2_TRACE_ANCHOR=1
set SF2_TRACE_ALIGN=1
set SF2_TRACE_CAP=1
set SF2_TRACE_OFFSET=1
set SF2_TRACE_POS=1
set SF2_TRACE_STANCE=1
rem --- mesh / cloth -------------------------------------------------------
set SF2_TRACE_BADTRI=1
set SF2_TRACE_EDGES=1
set SF2_TRACE_CLOTHSPACE=1
set SF2_TRACE_CLOTH_FRAME=1
rem --- ragdoll ------------------------------------------------------------
set SF2_RD_SEED_PROBE=1
set SF2_RAGDOLL_VERBOSE=1
rem --- combat / hits / walls ----------------------------------------------
set SF2_TRACE_HITLOG=1
set SF2_TRACE_DMG=1
set SF2_TRACE_WALLS=1
set SF2_TRACE_WEAPON=1
set SF2_TRACE_CAM=1
rem [needed for the root-vs-mesh check] `[sepd] root=/dpiv=/drawn=` per frame, so a
rem gap between the drawn body and its root can be measured from this log. Without
rem it the "mesh sticks in the opponent" item cannot be verified on a player run.
set SF2_TRACE_SEP=1
rem --- per-frame JSON state (the same rows the oracle diff uses) -----------
set SF2_DEBUG_LOG=1

echo.
echo   Fight: %BATTLE%  location: %LOC%   (full trace on)
echo   Log folder: %OUT%
echo.
echo   Play the fight: hit the enemy, THROW him, push him into a wall,
echo   let him knock you down. Then CLOSE the window.
echo.
echo   WHAT TO DO (the three things that are still unverified):
echo     1. walk right INTO the enemy, and separately into the BAG
echo        (they must not slide with you; the mesh must not lag the bones)
echo     2. land 2-3 KNOCKDOWNS (the hits that throw the body away)
echo     3. jump off the LEFT wall and off the RIGHT wall
echo.

"%ROOT%build\app\game\Release\game.exe" "%ROOT%reference\www\res" "%ROOT%reference\saves\save.xml" --windowed --fight --battle %BATTLE% --zone ZONE_1 --fight-ordinal 0 --location %LOC% --dump-pose 6000 --dump-state 6000 > "%OUT%\fight_log.txt" 2>&1

copy /y "%ROOT%reference\traces\native_pose.jsonl" "%OUT%\" >nul 2>&1
copy /y "%ROOT%reference\traces\native_state.jsonl" "%OUT%\" >nul 2>&1
copy /y "%ROOT%reference\traces\native_audio.jsonl" "%OUT%\" >nul 2>&1

rem --- first-pass analysis: did the fight capture any drawn-pose jumps? ----
if exist "%ROOT%reference\tools\pose_com_jumps.py" (
    python "%ROOT%reference\tools\pose_com_jumps.py" "%OUT%\fight_log.txt" > "%OUT%\summary.txt" 2>&1
    echo.
    type "%OUT%\summary.txt"
)

rem --- one file to send ----------------------------------------------------
for %%F in ("%OUT%") do set "OUTNAME=%%~nxF"
where tar >nul 2>&1 && tar -a -c -f "%OUT%.zip" -C "%ROOT%.harness\debug_logs" "%OUTNAME%" >nul 2>&1
if exist "%OUT%.zip" (
    echo.
    echo   Done. Send this file:  %OUT%.zip
) else (
    echo.
    echo   Done. Send this folder:  %OUT%
)
echo.
pause
