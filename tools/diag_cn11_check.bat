@echo off
REM CN11 link diagnostic - one-click run
REM Connect JLink V9 to MAIN BOARD SWD before running

set SCRIPT_DIR=%~dp0
set OUTPUT=%SCRIPT_DIR%diag_cn11_result.log

REM Find JLink.exe
set JLINK="D:\Program Files\SEGGER\JLink\JLink.exe"
if not exist %JLINK% set JLINK="C:\Program Files\SEGGER\JLink\JLink.exe"
if not exist %JLINK% set JLINK="C:\Program Files (x86)\SEGGER\JLink\JLink.exe"
if not exist %JLINK% (
    echo [ERROR] JLink.exe not found.
    echo Please edit this .bat file and set JLINK= path manually.
    pause
    exit /b 1
)

echo.
echo ==============================================
echo CN11 Link Diagnostic - Main Board SCREEN UART
echo ==============================================
echo.
echo Checklist:
echo   1. JLink V9 connected to MAIN BOARD SWD
echo      (PA13/SWDIO, PA14/SWCLK, 3V3, GND)
echo   2. Main board powered on
echo   3. Screen board powered on and running
echo.
echo This test takes ~10 seconds, does NOT halt CPU.
echo.
echo Press any key to start...
pause >nul

%JLINK% -CommanderScript "%SCRIPT_DIR%diag_cn11_check.jlink" -log "%OUTPUT%"

echo.
echo ==============================================
echo Test done! Result saved to:
echo   %OUTPUT%
echo ==============================================
echo.
echo Please send diag_cn11_result.log content back to firmware engineer.
echo.
pause
