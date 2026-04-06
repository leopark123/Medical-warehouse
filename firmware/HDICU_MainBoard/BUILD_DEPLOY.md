# 构建与部署指南

## 一、构建形态

| 形态 | 编译选项 | 用途 | UART调试输出 |
|------|---------|------|-------------|
| **production** | 无额外宏 | 正式部署 | ❌ 无 |
| **debug** | `-DHDICU_DEBUG` | 开发联调 | ✅ UART1每秒状态 |
| **diag** | 独立main_diag.c | 外设诊断 | ✅ UART1逐项测试 |
| **test** | 独立main_test.c | 最小UART测试 | ✅ UART1心跳 |

## 二、工具链

| 工具 | 版本 | 路径 |
|------|------|------|
| arm-none-eabi-gcc | 14.3.1 | STM32CubeIDE内置 |
| STM32CubeIDE | 2.1.0 | F:\ST\STM32CubeIDE_2.1.0 |
| J-Link | V7.84f | D:\Program Files\SEGGER\JLink |
| JFlash Lite | 同上 | 用于烧录和全片擦除 |

GCC完整路径：
```
F:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\plugins\
  com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\
  tools\bin\arm-none-eabi-gcc.exe
```

## 三、依赖库（Lib/目录，不在Git中）

首次编译需要从STM32CubeF1包或现有STM32项目复制到 `firmware/HDICU_MainBoard/Lib/`：

```
Lib/
├── STM32F1xx_HAL_Driver/    ← HAL驱动
│   ├── Inc/
│   └── Src/
├── CMSIS/                    ← ARM CMSIS头文件
│   ├── Include/
│   └── Device/ST/STM32F1xx/Include/
├── FreeRTOS/                 ← FreeRTOS内核
│   └── Source/
│       ├── include/
│       ├── portable/GCC/ARM_CM3/
│       └── portable/MemMang/heap_1.c
└── system_stm32f1xx.c       ← 系统初始化（自定义版本）
```

> FreeRTOS注意：当前使用的是从其他项目复制的修改版，已清理uart_printf.h/ticks_cpu.h依赖。

## 四、编译命令

### 方法A：STM32CubeIDE（推荐）

1. 打开STM32CubeIDE，工作空间选不含中文的路径
2. `File` → `Import` → `C/C++` → `Existing Code as Makefile Project`
3. 选择 `firmware/HDICU_MainBoard`，Toolchain选 `MCU ARM GCC`
4. `Project` → `Build Project` (Ctrl+B)

### 方法B：命令行

```bash
# 设置GCC路径
export PATH="F:/ST/STM32CubeIDE_2.1.0/.../tools/bin:$PATH"

# Production版本
make clean && make

# Debug版本（带UART1状态输出）
make clean && make CFLAGS_EXTRA="-DHDICU_DEBUG"
```

### 方法C：直接gcc调用（脚本方式）

见仓库中之前的编译脚本模式。关键编译参数：

```
-mcpu=cortex-m3 -mthumb -DSTM32F103xE -DUSE_HAL_DRIVER
-std=c99 -Os -fdata-sections -ffunction-sections
-specs=nano.specs -specs=nosys.specs
```

## 五、烧录方法

### 方法A：JFlash Lite（推荐，最可靠）

1. 打开 `D:\Program Files\SEGGER\JLink\JFlashLite.exe`
2. Device: `STM32F103VE`, Interface: `SWD`, Speed: `1000`
3. 如需全片擦除：先点 **Erase Chip**（卡死时按住Reset）
4. Data File: 选择 `build/firmware.bin`（或D:\firmware.bin）
5. Prog. Addr: `0x08000000`
6. 点 **Program Device**
7. 等待 `Verify successful`

### 方法B：J-Link Commander

```bash
JLink.exe -device STM32F103VE -if SWD -speed 1000 -autoconnect 1

# 在J-Link命令行中：
h
loadbin firmware.bin, 0x08000000
verifybin firmware.bin, 0x08000000
r
g
q
```

### 方法C：make flash

```bash
make flash    # 使用st-flash（需安装）
```

## 六、烧录验证

烧录后通过J-Link读取验证MCU运行状态：

```bash
# J-Link Commander
echo "h
g
sleep 2000
h
q" | JLink.exe -device STM32F103VE -if SWD -speed 1000 -autoconnect 1
```

检查项：
- `Cortex-M3 r1p1, Little endian` — MCU正确识别
- PC在Flash范围(0x0800xxxx) — 代码在执行
- CycleCnt在增长 — CPU在跑
- SP≈0x20010000 — 栈指针正常

## 七、固件版本标识

当前固件通过debug输出中的`[HDICU]`前缀和uptime标识。
建议后续添加版本号到Flash固定地址或协议响应中。

## 八、回滚方案

如果新固件导致MCU卡死：
1. 按住开发板Reset按钮
2. 打开JFlash Lite
3. 点Erase Chip（松开Reset）
4. 烧回之前正常的固件

保留旧固件bin文件作为回滚备份。

## 九、固件大小参考

| 版本 | text | data | BSS | .bin大小 |
|------|------|------|-----|---------|
| production | 21KB | 96B | 37KB | ~21KB |
| debug | 22KB | 96B | 37KB | ~22KB |
| diag | 3.3KB | 12B | 2KB | ~3KB |
| test | 2.1KB | 12B | 2KB | ~2KB |

Flash总量508KB，RAM总量64KB，余量充足。
