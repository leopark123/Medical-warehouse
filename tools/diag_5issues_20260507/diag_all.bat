@echo off
REM =====================================================================
REM  HDICU 5-Issue Joint Diagnostic  (2026-05-07)
REM  Single SWD connection per checkpoint, reads ALL 24 fields at once.
REM  6 checkpoints (T0~T5) -> dump_T?.log
REM  Then parse_dumps.ps1 generates summary.txt
REM =====================================================================

cd /d "%~dp0"

REM --- Locate JLink.exe ---
set JLINK=D:\Program Files\SEGGER\JLink\JLink.exe
if not exist "%JLINK%" set JLINK=C:\Program Files\SEGGER\JLink\JLink.exe
if not exist "%JLINK%" set JLINK=C:\Program Files (x86)\SEGGER\JLink\JLink.exe
if not exist "%JLINK%" (
    echo [ERR] JLink.exe not found in standard paths.
    pause
    exit /b 1
)

REM --- Build JLink command script in TEMP (avoid path encoding issues) ---
set SCRIPT=%TEMP%\hdicu_dump_all.txt
(
    echo si SWD
    echo speed 4000
    echo device STM32F103VE
    echo connect
    echo mem 0x20000074 2
    echo mem 0x20000078 2
    echo mem 0x2000007E 1
    echo mem 0x20000080 2
    echo mem 0x20000087 1
    echo mem 0x2000008E 2
    echo mem 0x20000096 2
    echo mem 0x20000098 2
    echo mem 0x2000009A 1
    echo mem 0x2000009B 1
    echo mem 0x2000009C 1
    echo mem 0x2000009D 1
    echo mem 0x2000009E 1
    echo mem 0x2000009F 1
    echo mem 0x200000A4 1
    echo mem 0x200000A8 2
    echo mem 0x200000AA 2
    echo mem 0x200000AC 2
    echo mem 0x200000AE 2
    echo mem 0x200000B0 1
    echo mem 0x200000B1 1
    echo mem 0x200000B4 1
    echo mem 0x200000B5 1
    echo mem 0x200000B6 2
    echo q
) > "%SCRIPT%"

del "%~dp0dump_T*.log" 2>nul
del "%~dp0summary.txt" 2>nul

cls
echo =====================================================================
echo   HDICU 5-Issue Joint Diagnostic
echo   24 fields per checkpoint x 6 checkpoints
echo =====================================================================
echo.
echo Prerequisites:
echo   [1] JLink V9 connected to MAIN BOARD SWD pads
echo   [2] Main board powered ON (Stage 8 redo v2 firmware)
echo   [3] Screen board powered ON, on LIVE page
echo.
echo Press any key to start checkpoint T0.
echo.
pause >nul

call :DO_DUMP T0 "BOOT BASELINE - no buttons pressed yet" "Wait 5 seconds after power-on, do NOT press any button."
call :DO_DUMP T1 "AFTER KEY7 (FRESH AIR) - 1st press" "Press KEY7 (CN6) ONCE briefly. Note 'supply-O2 time' display change."
call :DO_DUMP T2 "AFTER KEY7 - 2nd press (toggle off)" "Press KEY7 ONCE again."
call :DO_DUMP T3 "AFTER KEY5 LONG-PRESS (open-O2 ON)" "Long-press KEY5 (CN2) for 2+ seconds until open-O2 LED comes on."
call :DO_WAIT_DUMP T4 "WAIT 30 SECONDS - verify counters + O2 sensor live" 30
call :DO_DUMP T5 "AFTER KEY1 x5 (nursing cycle)" "Press KEY1 (CN1) FIVE times, ~1 second between presses."

REM --- Generate summary.txt via PowerShell ---
echo.
echo Generating summary.txt ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0parse_dumps.ps1"
if errorlevel 1 (
    echo [WARN] PowerShell parsing failed.  Raw dump_T?.log files are still valid.
)

cls
echo =====================================================================
echo   ALL 6 CHECKPOINTS COMPLETE
echo =====================================================================
echo.
echo Files generated:
dir /b "%~dp0dump_T*.log" 2>nul
if exist "%~dp0summary.txt" echo summary.txt
echo.
echo Send summary.txt back for analysis.
pause
exit /b 0


REM =====================================================================
REM  :DO_DUMP <tag> <title> <action_text>
REM  Dumps once.  Retries on failure.
REM =====================================================================
:DO_DUMP
set TAG=%~1
set TITLE=%~2
set ACTION=%~3

:DO_DUMP_RETRY
cls
echo === %TAG%   %TITLE% ===
echo.
echo Action: %ACTION%
echo.
echo *** Make sure JLink is connected to MAIN BOARD SWD (Cortex-M3) ***
echo *** NOT to screen board (GD32F303 = Cortex-M4) - dump would be invalid ***
echo.
echo When done, press any key to dump %TAG%.
pause >nul
echo Connecting JLink, reading 24 fields...
"%JLINK%" -if SWD -speed 4000 -device STM32F103VE -CommanderScript "%SCRIPT%" -log "%~dp0dump_%TAG%.log" >nul 2>&1
if not exist "%~dp0dump_%TAG%.log" goto :DO_DUMP_FAIL
findstr /C:"Found Cortex-M3" "%~dp0dump_%TAG%.log" >nul
if errorlevel 1 (
    echo.
    echo [FAIL] JLink did NOT detect Cortex-M3.  Likely connected to screen board (GD32F303 = M4).
    echo        Dump invalid - reconnect JLink SWD to MAIN BOARD pads (PA13/PA14/3V3/GND on main board).
    findstr /C:"Found Cortex" "%~dp0dump_%TAG%.log"
    del "%~dp0dump_%TAG%.log"
    echo.
    echo R = Retry %TAG% after fixing connection   /   Q = Quit
    choice /c RQ /n
    if errorlevel 2 exit /b 1
    goto :DO_DUMP_RETRY
)
findstr /C:"=" "%~dp0dump_%TAG%.log" >nul
if errorlevel 1 goto :DO_DUMP_FAIL
echo [OK] dump_%TAG%.log saved (Cortex-M3 verified).
timeout /t 1 /nobreak >nul
exit /b 0

:DO_DUMP_FAIL
echo.
echo [ERR] %TAG% dump failed (JLink could not connect or read).
echo       Check SWD wires, power, USB.
echo       R = Retry %TAG%   /   Q = Quit
choice /c RQ /n
if errorlevel 2 exit /b 1
goto :DO_DUMP_RETRY


REM =====================================================================
REM  :DO_WAIT_DUMP <tag> <title> <seconds>
REM  Auto-waits N seconds, then dumps.
REM =====================================================================
:DO_WAIT_DUMP
set TAG=%~1
set TITLE=%~2
set SECS=%~3

:DO_WAIT_RETRY
cls
echo === %TAG%   %TITLE% ===
echo.
echo Do NOT press any button.  Auto-waiting %SECS% seconds, then dumping.
echo *** JLink must be on MAIN BOARD SWD (Cortex-M3), not screen board ***
echo Press any key to start the timer.
pause >nul
echo Waiting %SECS% seconds...
timeout /t %SECS% /nobreak
echo Connecting JLink, reading 24 fields...
"%JLINK%" -if SWD -speed 4000 -device STM32F103VE -CommanderScript "%SCRIPT%" -log "%~dp0dump_%TAG%.log" >nul 2>&1
if not exist "%~dp0dump_%TAG%.log" goto :DO_WAIT_FAIL
findstr /C:"Found Cortex-M3" "%~dp0dump_%TAG%.log" >nul
if errorlevel 1 (
    echo.
    echo [FAIL] JLink did NOT detect Cortex-M3.  Likely connected to screen board.
    findstr /C:"Found Cortex" "%~dp0dump_%TAG%.log"
    del "%~dp0dump_%TAG%.log"
    echo.
    echo R = Retry %TAG% after fixing connection   /   Q = Quit
    choice /c RQ /n
    if errorlevel 2 exit /b 1
    goto :DO_WAIT_RETRY
)
findstr /C:"=" "%~dp0dump_%TAG%.log" >nul
if errorlevel 1 goto :DO_WAIT_FAIL
echo [OK] dump_%TAG%.log saved (Cortex-M3 verified).
timeout /t 1 /nobreak >nul
exit /b 0

:DO_WAIT_FAIL
echo.
echo [ERR] %TAG% dump failed.
echo       R = Retry %TAG%   /   Q = Quit
choice /c RQ /n
if errorlevel 2 exit /b 1
goto :DO_WAIT_RETRY
