@echo off
REM ============================================================
REM  reSF2 .dz extraction script for Windows
REM  Usage: extract_dz.bat <path_to_assets_folder>
REM  Example: extract_dz.bat "C:\sf2_assets\assets"
REM ============================================================

setlocal

set ASSETS_DIR=%~1
if "%ASSETS_DIR%"=="" (
    echo Usage: extract_dz.bat ^<path_to_assets_folder^>
    echo Example: extract_dz.bat "C:\sf2_assets\assets"
    exit /b 1
)

set DZIP=%~dp0dzip.exe
if not exist "%DZIP%" (
    echo ERROR: dzip.exe not found next to this script.
    echo Place dzip.exe in the same folder as this .bat file.
    exit /b 1
)

echo === Extracting files.dz ===
"%DZIP%" -d "%ASSETS_DIR%\files.dz"
if errorlevel 1 (
    echo WARNING: files.dz extraction had errors
) else (
    echo files.dz extracted successfully
)

echo.
echo === Extracting animations.dz ===
"%DZIP%" -d "%ASSETS_DIR%\animations.dz"
if errorlevel 1 (
    echo WARNING: animations.dz extraction had errors
) else (
    echo animations.dz extracted successfully
)

echo.
echo === Done ===
echo Extracted files are in subdirectories next to the .dz files.
echo Copy the extracted XML and .bin files back to your reSF2 assets folder.
pause
