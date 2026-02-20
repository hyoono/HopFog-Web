@echo off
REM ──────────────────────────────────────────────────────────────────
REM deploy.bat — Build and flash HopFog-Web firmware to ESP32
REM ──────────────────────────────────────────────────────────────────
REM
REM Usage:
REM   scripts\deploy.bat                    Build + flash + monitor
REM   scripts\deploy.bat build              Build only
REM   scripts\deploy.bat flash              Flash only
REM   scripts\deploy.bat monitor            Serial monitor only
REM   scripts\deploy.bat build esp32cam     Build for ESP32-CAM
REM   scripts\deploy.bat all esp32cam       Full deploy for ESP32-CAM
REM
REM Prerequisites:
REM   - PlatformIO CLI: pip install platformio
REM   - ESP32 connected via USB
REM ──────────────────────────────────────────────────────────────────

setlocal enabledelayedexpansion

set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=all"

set "BOARD_ENV=%~2"
if "%BOARD_ENV%"=="" set "BOARD_ENV=esp32dev"

REM ── Check PlatformIO ──────────────────────────────────────────────
where pio >nul 2>nul
if errorlevel 1 (
    echo [ERROR] PlatformIO CLI not found.
    echo.
    echo Install it with:
    echo   pip install platformio
    echo.
    echo Or install the VS Code PlatformIO extension.
    exit /b 1
)
echo [OK] PlatformIO found

REM ── Route to action ───────────────────────────────────────────────
if /i "%ACTION%"=="build"   goto :do_build
if /i "%ACTION%"=="flash"   goto :do_flash
if /i "%ACTION%"=="monitor" goto :do_monitor
if /i "%ACTION%"=="all"     goto :do_all

echo [ERROR] Unknown action: %ACTION%
echo Usage: %~nx0 [build^|flash^|monitor^|all] [esp32dev^|esp32cam]
exit /b 1

REM ── Build ─────────────────────────────────────────────────────────
:do_build
echo.
echo === Building firmware (env: %BOARD_ENV%) ===
pio run -e %BOARD_ENV%
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)
echo [OK] Build complete
goto :eof

REM ── Flash ─────────────────────────────────────────────────────────
:do_flash
echo.
echo === Flashing to ESP32 (env: %BOARD_ENV%) ===
pio run -e %BOARD_ENV% --target upload
if errorlevel 1 (
    echo [ERROR] Flash failed.
    exit /b 1
)
echo [OK] Flash complete
goto :eof

REM ── Monitor ───────────────────────────────────────────────────────
:do_monitor
echo.
echo === Serial Monitor (115200 baud) ===
echo Press Ctrl+C to exit
pio device monitor --baud 115200
goto :eof

REM ── All ───────────────────────────────────────────────────────────
:do_all
call :do_build
if errorlevel 1 exit /b 1
call :do_flash
if errorlevel 1 exit /b 1
echo.
echo === Deployment complete! ===
echo Opening serial monitor...
echo Watch for the ESP32's IP address in the output.
echo.
call :do_monitor
goto :eof
