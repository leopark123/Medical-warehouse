# JLink 调试脚本

实战收集的 JLink Commander 脚本，用于现场诊断和救援。

## 使用前提
- JLink V9 + JLink V7.84f / 更新版本
- 主板: `-device STM32F103VE`
- 屏幕板: `-device GD32F303RC` **（绝不能用 STM32F303RC，会触发 mass erase + RDP 锁死，详见 memory/feedback_jlink_gd32.md）**
- 默认 JLink 路径: `D:\Program Files\SEGGER\JLink\JLink.exe`

## main/ - 主板脚本

| 脚本 | 用途 |
|------|------|
| `read_board.bat` + `.jlink` | 双击运行的版本指纹读取（CPUID/AFIO/IWDG/Flash dump 512KB）|
| `read_quick.jlink` | 轻量诊断（只读关键寄存器，不 dump）|
| `dump_code.jlink` | dump 前 32KB 代码区，用于 firmware 对比 |
| `go.jlink` | reset + go（让卡死的板子重启运行）|

## screen/ - 屏幕板脚本

| 脚本 | 用途 |
|------|------|
| `probe.jlink` | 仅探测 JLink 硬件状态（VTref/电流），不连接目标 |
| `read_screen.jlink` | 屏幕板版本指纹（先用 GD32F303RC 名）|
| `read_screen2.jlink` | 备份脚本（含 STM32F303RC 名 — **不要用，会触发 mass erase**）|
| `check_flash.jlink` | 检查 Flash 是否被锁/擦除 |

## 实战经验

详见 memory 笔记：
- `feedback_jlink_gd32.md` — GD32 + JLink 铁律 + OB 恢复 SWD 序列
- `feedback_serial_debug.md` — JLink live read（不 halt）+ 关键 RAM 地址清单
- `feedback_workflow.md` — 编译烧录流程
