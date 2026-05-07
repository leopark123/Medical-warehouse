@echo off
REM ============================================================
REM 主板一键 编译 + 烧录 + 复位运行
REM 双击运行即可. 需要 JLink 已连主板, 板子上电.
REM ============================================================

cd /d "%~dp0"

set JLINK_EXE=D:\Program Files\SEGGER\JLink\JLink.exe
if not exist "%JLINK_EXE%" (
  echo [ERR] 找不到 JLink.exe at: %JLINK_EXE%
  echo 请修改本文件中的 JLINK_EXE 路径
  pause
  exit /b 1
)

echo === Step 1/2: 编译主板固件 ===
make
if errorlevel 1 (
  echo [ERR] 编译失败, 请查看上方错误信息
  pause
  exit /b 1
)

echo.
echo === Step 2/2: JLink 烧录 (device=STM32F103VE) ===
"%JLINK_EXE%" -device STM32F103VE -if SWD -speed 4000 -autoconnect 1 -CommanderScript flash.jlink

if errorlevel 1 (
  echo [ERR] 烧录失败, 检查 JLink 接线和板子供电
  pause
  exit /b 1
)

echo.
echo === Done. 板子已复位运行 ===
pause
