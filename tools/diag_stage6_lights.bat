@echo off
REM Stage 6 lights "always on" diagnostic
REM Connect JLink V9 to MAIN BOARD SWD before running

set SCRIPT_DIR=%~dp0
set OUTPUT=%SCRIPT_DIR%diag_stage6_lights_result.log

set JLINK="D:\Program Files\SEGGER\JLink\JLink.exe"
if not exist %JLINK% set JLINK="C:\Program Files\SEGGER\JLink\JLink.exe"
if not exist %JLINK% set JLINK="C:\Program Files (x86)\SEGGER\JLink\JLink.exe"
if not exist %JLINK% (
    echo [ERROR] JLink.exe not found.
    pause
    exit /b 1
)

echo.
echo Stage 6 Lights Diagnostic - Read AFIO/GPIOE/TIM1/AppData
echo.
echo JLink should be on MAIN BOARD SWD (PA13/PA14/3V3/GND).
echo Test takes ~3 seconds, no CPU halt.
echo.
pause

%JLINK% -CommanderScript "%SCRIPT_DIR%diag_stage6_lights.jlink" -log "%OUTPUT%"

echo.
echo Done! Result: %OUTPUT%
echo Send the log file content back to firmware engineer.
echo.
pause
