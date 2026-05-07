@echo off
REM ============================================================
REM 屏幕板一键 编译 + 烧录 + 复位运行
REM 双击运行即可. 需要 JLink 已连屏幕板, 板子上电.
REM
REM !! 警告: device 写 GD32F303RC (国产兆易创新), 绝不能写 STM32F303RC !!
REM    用错 device 名会触发 mass erase + RDP 锁死, 板子变砖
REM ============================================================

cd /d "%~dp0"

set JLINK_EXE=D:\Program Files\SEGGER\JLink\JLink.exe
if not exist "%JLINK_EXE%" (
  echo [ERR] 找不到 JLink.exe at: %JLINK_EXE%
  echo 请修改本文件中的 JLINK_EXE 路径
  pause
  exit /b 1
)

echo === Step 1/2: 编译屏幕板固件 ===
make
if errorlevel 1 (
  echo [ERR] 编译失败, 请查看上方错误信息
  pause
  exit /b 1
)

echo.
echo === Step 2/2: JLink 烧录 (device=GD32F303RC) ===
echo [WARNING] 确认 device 名是 GD32F303RC, 不是 STM32F303RC!
"%JLINK_EXE%" -device GD32F303RC -if SWD -speed 4000 -autoconnect 1 -CommanderScript flash.jlink

if errorlevel 1 (
  echo [ERR] 烧录失败, 检查 JLink 接线和板子供电
  pause
  exit /b 1
)

echo.
echo === Done. 屏幕板已复位运行 ===
pause
