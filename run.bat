@echo off
REM Build and run reSF2 in Release mode
REM Usage: run.bat [--debug]

setlocal enabledelayedexpansion

cd /d "%~dp0"

set CONFIG=Release
if "%~1"=="--debug" set CONFIG=Debug

echo Building %CONFIG%...
cmake --build build --config %CONFIG% --target resf2_app
if %errorlevel% neq 0 (
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Starting reSF2...
echo.
start "" "build\bin\%CONFIG%\resf2_app.exe" --assets "%~dp0assets"
