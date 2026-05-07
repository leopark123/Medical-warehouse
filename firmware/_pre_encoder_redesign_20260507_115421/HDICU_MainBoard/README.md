# HDICU-ZKB01A Main Board Firmware

STM32F103VET6 主控板固件工程

## 编译前准备

1. 安装 [GNU ARM Embedded Toolchain](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm)
2. 将以下库放入 `Lib/` 目录:
   - `Lib/STM32F1xx_HAL_Driver/` — STM32Cube HAL 驱动
   - `Lib/CMSIS/` — ARM CMSIS 头文件
   - `Lib/FreeRTOS/` — FreeRTOS 内核源码
3. 将 `startup_stm32f103xe.s` 放入工程根目录（可从 STM32Cube 包获取）
4. 取消 `Makefile` 中 HAL 和 FreeRTOS 源文件的注释

## 编译

```bash
make                # Release build
make DEBUG=1        # Debug build with symbols
make clean          # Clean build artifacts
make flash          # Flash via ST-Link
make size           # Show section sizes
```

## 单元测试（无需硬件）

```bash
cd Tests
python run_tests.py
```

## 项目结构

```
├── main.c                  # 系统入口 + 时钟配置
├── Makefile                # 构建系统
├── STM32F103VETx_FLASH.ld  # 链接脚本（508K代码 + 4K参数区）
├── FreeRTOSConfig.h        # FreeRTOS 配置
├── BSP/                    # 板级硬件映射
├── App/                    # 应用层（任务 + 数据中心）
├── Protocol/               # 通信协议（iPad + 屏幕板）
├── Sensors/                # 传感器驱动
├── Control/                # 控制逻辑 + 互锁 + 报警
├── Drivers/                # 硬件驱动（UART/ADC/PWM/GPIO/Flash）
└── Tests/                  # PC端单元测试
```
