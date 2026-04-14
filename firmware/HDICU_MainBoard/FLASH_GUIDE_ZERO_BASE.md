# 医疗仓PCB首次烧录指南（零基础版）

> 本文档假设你从未烧过STM32。每一步都有截图级描述。
> 按顺序做，不要跳步。每步做完打勾再往下。

---

## 你需要准备的东西

| 序号 | 物品 | 用途 | 你有吗 |
|------|------|------|--------|
| 1 | 医疗仓PCB板(HDICU-ZKB01A) | 被烧录的目标板 | ☐ |
| 2 | J-Link仿真器(ARM V9) | 通过SWD烧录固件 | ☐ |
| 3 | 4根杜邦线(母对母) | 连接J-Link和PCB的SWD | ☐ |
| 4 | USB线(连J-Link到电脑) | J-Link供电+通信 | ☐ |
| 5 | USB-TTL串口模块(CH343) | 看固件输出 | ☐ |
| 6 | 2根杜邦线(母对母) | 连接串口模块TX/RX | ☐ |
| 7 | 12V电源适配器 | 给PCB供电 | ☐ |
| 8 | 电脑(Windows) | 运行CubeIDE和JFlash | ☐ |
| 9 | 万用表 | 验证继电器通路 | ☐ |

**暂时不需要：** 示波器（后面PE9验证时再用）、220V负载（全部验证通过前绝不接）

---

## 第一阶段：编译固件（电脑上操作，不接PCB）

### 步骤1.1：打开CubeIDE

- ☐ 双击 `F:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\STM32CubeIDE.exe`
- ☐ 工作空间路径选一个**没有中文**的目录，比如 `D:\workspace`
- ☐ 等它加载完

### 步骤1.2：导入项目

- ☐ 菜单 `File` → `Import...`
- ☐ 选 `C/C++` → `Existing Code as Makefile Project` → 点 `Next`
- ☐ `Existing Code Location` 点 `Browse`，选到 `F:\小项目\医疗仓\firmware\HDICU_MainBoard`
- ☐ `Toolchain for Indexer Settings` 选 `MCU ARM GCC`
- ☐ 点 `Finish`

### 步骤1.3：检查Lib目录

项目依赖的HAL/FreeRTOS库不在Git中，需要确认已存在：

- ☐ 在CubeIDE左侧项目树中展开 `Lib/` 目录
- ☐ 确认有以下3个子目录（里面有.c和.h文件）：
  - `Lib/STM32F1xx_HAL_Driver/` （HAL驱动）
  - `Lib/CMSIS/` （ARM头文件）
  - `Lib/FreeRTOS/` （实时操作系统）
  - `Lib/system_stm32f1xx.c` （系统初始化）
- ☐ 如果缺失，从之前开发板项目或STM32CubeF1包中复制进来

### 步骤1.4：编译PCB测试固件

我们**先不编译正式固件**，而是编译一个专门的PCB测试固件。

需要做一个小改动：**把main_pcb_test.c当作入口，替换main.c**。

方法：
- ☐ 在CubeIDE项目树中，右键 `main.c` → `Resource Configurations` → `Exclude from Build...` → 勾选所有配置 → OK
- ☐ 同样排除 `App/main_app.c`、`App/tasks/tasks.c`、`App/freertos_hooks.c`、`App/data/app_data.c`
- ☐ 同样排除所有 `Protocol/`、`Sensors/`、`Control/` 目录下的 .c 文件
- ☐ 右键 `main_pcb_test.c` → 确认**没有**被排除（如果被排除了，取消排除）

> **为什么？** main_pcb_test.c 是一个独立的测试程序，它有自己的 main() 函数，
> 不需要FreeRTOS、协议、传感器这些。排除它们避免链接冲突。

简化方法（如果上面太复杂）：
- ☐ 把 `main.c` 重命名为 `main.c.bak`
- ☐ 把 `main_pcb_test.c` 复制一份命名为 `main.c`
- ☐ 修改Makefile第38行，把 `main.c \` 改成只保留这一个入口

最简单的方法（命令行）：
```
cd F:\小项目\医疗仓\firmware\HDICU_MainBoard

# 用以下命令直接编译（把下面的GCC路径改成你电脑上的实际路径）
# 注意：这是一条很长的命令，全部复制粘贴

"F:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin\arm-none-eabi-gcc.exe" -mcpu=cortex-m3 -mthumb -DSTM32F103xE -DUSE_HAL_DRIVER -I. -IBSP -IDrivers/uart -IDrivers/adc -IDrivers/pwm -IDrivers/gpio -IDrivers/flash -ILib/STM32F1xx_HAL_Driver/Inc -ILib/CMSIS/Device/ST/STM32F1xx/Include -ILib/CMSIS/Include -Os -std=c99 -specs=nano.specs -specs=nosys.specs -fdata-sections -ffunction-sections -TSTM32F103VETx_FLASH.ld -Wl,--gc-sections -o pcb_test.elf main_pcb_test.c Drivers/uart/uart_driver.c Drivers/adc/adc_driver.c Drivers/gpio/relay_driver.c Drivers/pwm/pwm_driver.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_uart.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_adc_ex.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim_ex.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c Lib/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c Lib/system_stm32f1xx.c startup_stm32f103xe.s -lc -lm -lnosys
```

然后生成bin：
```
"...(同上路径)...\arm-none-eabi-objcopy.exe" -O binary pcb_test.elf pcb_test.bin
```

### 步骤1.5：确认编译成功

- ☐ 没有报错（warnings可以忽略）
- ☐ 生成了 `pcb_test.bin`（或 `build/firmware.bin`）
- ☐ 文件大小大约 3-10KB（不会超过20KB）
- ☐ 把这个 .bin 文件复制到一个好找的地方，比如 `D:\pcb_test.bin`

---

## 第二阶段：接线（断电状态操作！）

### 步骤2.1：找到PCB上的接口

你需要找到这3个接口：

| 接口 | 位置 | 针数 | 用途 |
|------|------|------|------|
| U2 | 12V DC输入 | 4针(VH3.96) | PCB供电 |
| H1 | SWD调试 | 4针排针 | J-Link连接 |
| UART1 TX(PA9) | CN3(屏幕板接口) | — | 串口输出 |

### 步骤2.2：连接J-Link到PCB（不通电！）

J-Link的20针接口中你只需要4根线。J-Link接口定义（看J-Link背面标注）：

| J-Link针脚 | 信号 | 连到PCB(H1) |
|-----------|------|------------|
| Pin 1 | VTref (3.3V) | H1的3V3 |
| Pin 7 | SWDIO | H1的SWDIO(PA13) |
| Pin 9 | SWCLK | H1的SWCLK(PA14) |
| Pin 4 | GND | H1的GND |

> **重要：** 如果你不确定H1的针脚顺序，用万用表量哪个针和PCB地(GND)通，那个就是GND。
> 然后从GND出发，按原理图H1的顺序找其他针。

- ☐ 用4根杜邦线连接上述4个信号
- ☐ 检查：没有接反、没有短路
- ☐ J-Link的USB线连到电脑（此时J-Link亮灯，但PCB还没通电）

### 步骤2.3：连接串口模块

| 串口模块 | 连到PCB |
|---------|--------|
| RX | PA9 (UART1 TX) |
| GND | PCB GND |

> **注意：** 串口模块的RX接MCU的TX。不需要接TX→RX（PCB测试固件只发不收）。

- ☐ 串口模块USB插到电脑
- ☐ 打开串口助手（SSCOM、PuTTY等），选对应COM口
- ☐ 波特率设 **115200**, 8N1

### 步骤2.4：给PCB供电

- ☐ 12V电源适配器插到U2接口
- ☐ 确认PCB上电源指示灯亮（有LED2亮）
- ☐ 用万用表量PCB上3.3V测试点，确认3.3V正常

---

## 第三阶段：烧录

### 步骤3.1：打开JFlash Lite

- ☐ 打开 `D:\Program Files\SEGGER\JLink\JFlashLite.exe`
- ☐ 设置：
  - Device: **STM32F103VE**
  - Interface: **SWD**
  - Speed: **1000** (kHz)
- ☐ 点 `OK`

### 步骤3.2：确认J-Link识别MCU

- ☐ JFlash Lite窗口底部日志应显示类似：
  ```
  Connecting...
  Connected successfully.
  Found Cortex-M3 r1p1, Little endian.
  ```
- ☐ 如果显示 `Cannot connect` 或 `No device found`：
  - 检查杜邦线是否松动
  - 检查SWDIO和SWCLK是否接反
  - 确认PCB已通电(3.3V正常)
  - 尝试按住PCB上Reset按钮再松开，然后重新连接

### 步骤3.3：烧录

- ☐ `Data File` 点 `...` 浏览，选择你的 `pcb_test.bin`
- ☐ `Prog. Addr` 填 `0x08000000`（这是Flash起始地址，通常已自动填好）
- ☐ 先点 **Erase Chip**（全片擦除，约2-3秒）
- ☐ 再点 **Program Device**（烧录，约1-2秒）
- ☐ 等日志显示：
  ```
  Erasing...Done
  Programming...Done
  Verifying...O.K.
  ```

### 步骤3.4：看串口输出

烧录成功后MCU自动复位运行。看串口助手：

- ☐ 应该看到类似输出：
  ```
  [PCB] ==============================
  [PCB]  HDICU-ZKB01A PCB Test
  [PCB]  SystemCoreClock = 72000000
  [PCB] ==============================
  ```
- ☐ 如果SystemCoreClock = **72000000** → HSE+PLL成功！
- ☐ 如果SystemCoreClock = **8000000** → HSE失败，回退到HSI（仍能工作但非最优）
- ☐ 如果串口完全没输出：
  - 检查串口模块RX是否接到PA9
  - 检查波特率是否115200
  - 检查杜邦线是否松动
  - 用万用表量PA9是否有电压跳动

---

## 第四阶段：逐项验证（跟着串口输出走）

PCB测试固件会自动依次执行以下测试，你只需要**看串口输出 + 用万用表/耳朵确认**。

### 测试1：ADC/NTC温度（自动）

串口会输出4路ADC值和对应温度：
```
[PCB] === ADC/NTC Test ===
[PCB] CH0(PA0): ADC=2048 -> 25.3°C
[PCB] CH1(PA1): ADC=2051 -> 25.2°C
...
```

- ☐ 检查：温度值是否合理（室温附近）
- ☐ 如果显示 `-999` → 该通道NTC断路或未接

### 测试2：继电器空载测试（自动，听声音）

```
[PCB] === Relay Test ===
[PCB] Relay0 PTC(PE1) ON
```
每路会依次通断，你应该**听到继电器"哒"的吸合声**。

- ☐ Relay0 PTC(PE1) — 听到click ☐
- ☐ Relay1 JIARE(PE0) — 听到click ☐
- ☐ Relay2 RED(PB9) — 听到click ☐
- ☐ Relay3 ZIY(PB8) — 听到click ☐
- ☐ Relay4 O2(PB7) — 听到click ☐
- ☐ Relay5 JIASHI(PE4) — 听到click ☐
- ☐ Relay6 FENGJI(PE3) — 听到click ☐
- ☐ Relay7 YASUO(PE2) — 听到click ☐
- ☐ Relay8 WH(PB4) — 听到click ☐

> 如果某路没有click声：该路GPIO映射可能有问题，用万用表量该MCU引脚是否有高电平输出。

### 测试3：风机GPIO + PE9 PWM（自动）

```
[PCB] === Fan GPIO Test ===
[PCB] --- ON/OFF test: PE5/PE6/PC13 toggle 1Hz x6 ---
[PCB] PE5/PE6/PC13 HIGH
[PCB] PE5/PE6/PC13 LOW
...
[PCB] --- PE9 PWM test: ~1kHz 50% duty for 3s ---
[PCB] --- PE6+PE9 combo: PE6=ON + PE9=PWM for 3s ---
```

- ☐ PE5/PE6/PC13翻转时，用万用表量这3个引脚，应该看到0V→3.3V→0V交替
- ☐ PE9 PWM时，如果有示波器接PE9，应该看到~1kHz方波
- ☐ PE6+PE9联动时，PE6应该保持高电平，PE9应该有PWM波

### 测试4：护理灯LED（自动）

```
[PCB] === LED Test ===
```

- ☐ 如果板上有对应LED，应该看到依次亮灭

### 测试5：蜂鸣器（自动）

```
[PCB] === Buzzer Test (PB3) ===
```

- ☐ 应该**听到蜂鸣器响一声**（约500ms）

### 测试6：输入检测

```
[PCB] === Input Test (PB14/PB15) ===
[PCB] PB14(liquid)=1
[PCB] PB15(urine)=1
```

- ☐ 记录初始值
- ☐ 如果有液位/尿液传感器可接，接上后看值是否变化

---

## 第五阶段：结果判定

### 全部通过 ✅

如果以上所有测试都通过（9路继电器全部click、蜂鸣器响、ADC有值、串口输出正常）：

- ☐ **PCB硬件验证通过**
- ☐ 可以进入下一步：烧录正式固件（production firmware）
- ☐ 正式固件烧录方法与上面完全相同，只是把 `pcb_test.bin` 换成 `firmware.bin`

### 部分不通过 ⚠️

| 现象 | 可能原因 | 排查方法 |
|------|---------|---------|
| 串口完全无输出 | 杜邦线松/波特率错/PA9接错 | 量PA9电压 |
| SystemCoreClock=8000000 | HSE晶振未起振 | 量晶振两端波形 |
| 某路继电器无click | GPIO映射错或ULN坏 | 万用表量MCU引脚 |
| 蜂鸣器不响 | PB3接线问题 | 量PB3有无3.3V |
| ADC全部0 | PA0-PA5未接NTC | 正常，NTC未接时为0 |
| ADC全部4095 | NTC短路 | 检查NTC接线 |

---

## 安全红线（绝对不能做的事）

1. **在继电器空载测试全部通过之前，绝不接220V负载**
2. **不要同时给J-Link供电和12V供电然后拔J-Link** — 先断12V，再拔J-Link
3. **不要带电插拔SWD杜邦线**
4. **如果MCU卡死无法连接** — 按住Reset，连JFlash，点Erase Chip，松Reset

---

## 附录：文件清单

| 文件 | 用途 | 你需要用到 |
|------|------|-----------|
| `pcb_test.bin` | PCB测试固件 | ✅ 第一次烧这个 |
| `firmware.bin` | 正式固件 | 测试通过后烧这个 |
| `main_pcb_test.c` | 测试固件源码 | 不用管 |
| `main.c` | 正式固件入口 | 不用管 |
| `HARDWARE_PIN_MATRIX.md` | 硬件引脚对照表 | 排查问题时看 |
| `PCB_TEST_PROCEDURE.md` | 详细测试流程 | 深入测试时看 |
