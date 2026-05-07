@echo off
REM ============================================================
REM  T4 / T8  Fog and Disinfect diagnostics  (CORRECTED OFFSETS)
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

set SCRIPT=%TEMP%\jlink_dump_fog_v2.txt
(
    echo si SWD
    echo speed 4000
    echo device STM32F103VE
    echo connect
    echo mem 0x20000096 2
    echo mem 0x20000098 2
    echo mem 0x200000A8 2
    echo mem 0x200000AA 2
    echo mem 0x200000AE 2
    echo q
) > "%SCRIPT%"

set OUTLOG=%TEMP%\dump_fog_v2.log

echo.
echo === T4 / T8 Fog/Disinfect  (CORRECTED offsets) ===
echo.
echo Steps before pressing key:
echo   1. JLink V9 connected to main board SWD
echo   2. Main board powered on
echo   3. Screen board: click encoder to SET_FOG page
echo   4. Rotate encoder +1 click  (sets 60 sec fog)
echo   5. Press any key NOW
echo.
pause

"%JLINK%" -if SWD -speed 4000 -device STM32F103VE -CommanderScript "%SCRIPT%" -log "%OUTLOG%"

copy "%OUTLOG%" "%~dp0dump_fog_v2.log" >nul 2>&1

echo.
echo Done. Output saved to dump_fog_v2.log
echo.
echo Read these 3 fields (CORRECTED addresses):
echo   0x20000096 = setpoint.fog_time      (expect 0x003C=60 if fixed, 0x0000 if Stage 8 redo bug)
echo   0x200000A8 = fog_remaining          (expect counting down from 60)
echo   0x200000AE = relay_status           (bit 8 = PB4 fogger, second byte bit 0)
echo.
echo === LOG CONTENT ===
type "%OUTLOG%"
echo.
pause
