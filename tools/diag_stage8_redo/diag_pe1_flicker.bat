@echo off
REM ============================================================
REM  T3  PE1 flicker diagnostics  (CORRECTED OFFSETS for Stage 8 redo)
REM  ARM GCC -fshort-enums: enum is 1 byte, struct layout shifted
REM  10 samples at ~1 sec interval
REM ============================================================

cd /d "%~dp0"

set JLINK=D:\Program Files\SEGGER\JLink\JLink.exe
if not exist "%JLINK%" set JLINK=C:\Program Files\SEGGER\JLink\JLink.exe
if not exist "%JLINK%" set JLINK=C:\Program Files (x86)\SEGGER\JLink\JLink.exe
if not exist "%JLINK%" (
    echo [ERR] JLink.exe not found.
    pause
    exit /b 1
)

REM Generate JLink command script - CORRECTED ADDRESSES
set SCRIPT=%TEMP%\jlink_dump_v2.txt
(
    echo si SWD
    echo speed 4000
    echo device STM32F103VE
    echo connect
    echo mem 0x20000074 2
    echo mem 0x20000078 2
    echo mem 0x2000007E 1
    echo mem 0x20000087 1
    echo mem 0x2000008E 2
    echo mem 0x20000096 2
    echo mem 0x20000098 2
    echo mem 0x2000009E 1
    echo mem 0x200000A0 1
    echo mem 0x200000A1 1
    echo mem 0x200000A2 1
    echo mem 0x200000A4 1
    echo mem 0x200000A5 1
    echo mem 0x200000A6 1
    echo mem 0x200000A8 2
    echo mem 0x200000AA 2
    echo mem 0x200000AC 2
    echo mem 0x200000AE 2
    echo mem 0x200000B0 1
    echo mem 0x200000B1 1
    echo mem 0x200000B4 1
    echo mem 0x200000B5 1
    echo mem 0x200000B6 2
    echo mem 0x200000B8 1
    echo mem 0x200000B9 1
    echo q
) > "%SCRIPT%"

echo.
echo === T3 PE1 flicker  (10 samples, CORRECTED offsets) ===
echo.
echo Steps before pressing key:
echo   1. JLink V9 connected to main board SWD
echo   2. Main board powered on
echo   3. Screen board: enter SET_TEMP page, rotate +5 clicks
echo   4. Wait until you SEE PE1 LED actually flickering
echo   5. Press any key NOW
echo.
pause

del "%~dp0dump_v2_*.log" 2>nul

for /L %%i in (1,1,10) do (
    echo [sample %%i / 10]
    "%JLINK%" -if SWD -speed 4000 -device STM32F103VE -CommanderScript "%SCRIPT%" -log "%~dp0dump_v2_%%i.log" >nul 2>&1
    timeout /t 1 /nobreak >nul
)

echo.
echo Done. 10 log files: dump_v2_1.log ~ dump_v2_10.log
echo.
echo Key fields with CORRECTED addresses:
echo   0x200000A4  temp_state         (1 byte: 0=IDLE, 1=COOLING, 2=HEATING)
echo   0x200000A0  enable_temp_ctrl   (1 byte, should be 1 if rotated)
echo   0x200000AE  relay_status       (2 bytes - REAL ONE NOW!)
echo                  bit 0 = PE1 (PTC heater) -- watch this for flickering
echo                  bit 8 = PB4 (fogger) -- second byte bit 0
echo   0x200000A8  fog_remaining      (2 bytes seconds)
echo   0x200000B6  alarm_flags        (2 bytes)
echo.
pause
