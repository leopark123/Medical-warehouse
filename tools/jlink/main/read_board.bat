@echo off
REM 读取主控板版本指纹脚本
REM 双击运行即可。需要 J-Link 已连上板子并且板子上电。
REM 输出落到当前目录的 read_board.log 和 board_dump.bin

cd /d "%~dp0"

set JLINK_EXE=D:\Program Files\SEGGER\JLink\JLink.exe
if not exist "%JLINK_EXE%" (
  echo [ERR] 找不到 JLink.exe at: %JLINK_EXE%
  echo 请修改本文件里的 JLINK_EXE 路径或确认 J-Link 已安装。
  pause
  exit /b 1
)

echo === Running J-Link script (输出会同时显示和写入 read_board.log) ===
echo.

"%JLINK_EXE%" -device STM32F103VE -if SWD -speed 4000 -autoconnect 1 -CommanderScript read_board.jlink -log read_board.log

echo.
echo === Done ===
echo 输出文件:
echo   read_board.log    (J-Link 命令日志，含寄存器值)
echo   board_dump.bin    (整片 Flash 512KB)
echo.
pause
