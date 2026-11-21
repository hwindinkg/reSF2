@echo off
rem ============================================================================
rem  play_debug.cmd - the SAME launch as play.cmd, but boots the FIRST BOSS
rem  fight and writes EVERY trace the port can produce into
rem  .harness\debug_logs\<timestamp>\ :
rem
rem    fight_log.txt        - stdout/stderr: every [anchor]/[pos]/[align]/[cap]/
rem                           [ragdoll]/[hit]/[rdoll] line: model positions,
rem                           drawn-pose COM, clip placement, ragdoll state,
rem                           hits, wall reactions, stretched triangles
rem    native_pose.jsonl    - EVERY bone of BOTH fighters, EVERY fight frame
rem    native_state.jsonl   - the fight state (rules, callouts, capsules, AI)
rem
rem  Play the fight as usual (walk, hit the enemy, THROW him, push him into a
rem  wall, let him knock you down), then CLOSE the game window. The script then
rem  copies the dumps next to the log and prints the folder to send back.
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
rem --- per-frame JSON state (the same rows the oracle diff uses) -----------
set SF2_DEBUG_LOG=1

echo.
echo   Fight: %BATTLE%  location: %LOC%   (full trace on)
echo   Log folder: %OUT%
echo   Play the fight, then close the window.
echo.

"%ROOT%build\app\game\Release\game.exe" "%ROOT%reference\www\res" "%ROOT%reference\saves\save.xml" --windowed --fight --battle %BATTLE% --zone ZONE_1 --fight-ordinal 0 --location %LOC% --dump-pose 6000 --dump-state 6000 > "%OUT%\fight_log.txt" 2>&1

copy /y "%ROOT%reference\traces\native_pose.jsonl" "%OUT%\" >nul 2>&1
copy /y "%ROOT%reference\traces\native_state.jsonl" "%OUT%\" >nul 2>&1
copy /y "%ROOT%reference\traces\native_audio.jsonl" "%OUT%\" >nul 2>&1

echo.
echo   Done. Send the whole folder:  %OUT%
dir /b "%OUT%"
echo.
pause
