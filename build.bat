@echo off
setlocal enabledelayedexpansion

echo === Configuring (Config: Release) ===
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRESF2_BUILD_TESTS=ON -DRESF2_BUILD_RUNTIME=ON -DRESF2_USE_GLFW=ON
if errorlevel 1 (
    echo CMake configuration failed!
    exit /b 1
)

echo.
echo === Building ===
cmake --build build --config Release
if errorlevel 1 (
    echo Build failed!
    exit /b 1
)

echo.
echo === Build successful ===
echo Executable: build\bin\Release\resf2_app.exe
