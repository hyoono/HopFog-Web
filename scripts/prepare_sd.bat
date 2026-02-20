@echo off
REM ──────────────────────────────────────────────────────────────────
REM prepare_sd.bat — Copy web UI files to an SD card for HopFog-Web
REM ──────────────────────────────────────────────────────────────────
REM
REM Usage:
REM   scripts\prepare_sd.bat E:
REM   scripts\prepare_sd.bat D:\
REM
REM This copies data\sd\www to the SD card and creates the db\ folder.
REM ──────────────────────────────────────────────────────────────────

setlocal enabledelayedexpansion

if "%~1"=="" (
    echo [ERROR] Please provide the SD card drive letter.
    echo.
    echo Usage: %~nx0 E:
    echo        %~nx0 D:\
    exit /b 1
)

set "SD_DRIVE=%~1"

REM ── Resolve project root (parent of scripts\) ────────────────────
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%.."
set "PROJECT_ROOT=%CD%"
popd

set "SD_SOURCE=%PROJECT_ROOT%\data\sd"

REM ── Validate ──────────────────────────────────────────────────────
if not exist "%SD_DRIVE%\" (
    echo [ERROR] Drive "%SD_DRIVE%" not found.
    echo Make sure the SD card is inserted and assigned that drive letter.
    exit /b 1
)

if not exist "%SD_SOURCE%\www" (
    echo [ERROR] Source directory "%SD_SOURCE%\www" not found.
    echo Are you running this from the project directory?
    exit /b 1
)

REM ── Confirm ───────────────────────────────────────────────────────
echo HopFog-Web — SD Card Preparation
echo ──────────────────────────────────
echo Source:      %SD_SOURCE%
echo Destination: %SD_DRIVE%\
echo.
echo This will copy web files to the SD card.
set /p "CONFIRM=Continue? [y/N] "
if /i not "%CONFIRM%"=="y" (
    echo Aborted.
    exit /b 0
)

REM ── Copy files ────────────────────────────────────────────────────
echo.
echo Copying web files...
xcopy "%SD_SOURCE%\www" "%SD_DRIVE%\www\" /E /I /Y
if errorlevel 1 (
    echo [ERROR] Failed to copy files.
    exit /b 1
)

echo.
echo Creating db\ directory...
if not exist "%SD_DRIVE%\db" mkdir "%SD_DRIVE%\db"

REM ── Summary ───────────────────────────────────────────────────────
echo.
echo [OK] SD card prepared successfully!
echo.
echo SD card contents:
dir "%SD_DRIVE%\www" /S /B 2>nul
echo.
echo Next steps:
echo   1. Safely eject the SD card (right-click drive in Explorer ^> Eject)
echo   2. Insert it into the ESP32-CAM SD card slot
echo   3. Flash the firmware: scripts\deploy.bat all esp32cam
