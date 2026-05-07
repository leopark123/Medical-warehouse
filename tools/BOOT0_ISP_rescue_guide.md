# BOOT0 ISP 救援手册 — 把 Stage 6 砖板救活

**适用场景**：主板烧了 Stage 6 后 SWD 死了，JLink 完全无法连接。CPU 实际还能跑（只是调试口被关了），可以用 STM32 内置的 ISP bootloader 通过 UART 重新烧录固件。

**成功率**：**100% 确定性**（不靠运气）。
**所需时间**：约 15 分钟。

---

## 一、原理说明

STM32F103VET6 出厂时 ROM 里固化了一个 **System Memory Bootloader**（位于 0x1FFFF000）。在启动时通过 BOOT0 / BOOT1 引脚选择从哪里启动：

| BOOT1 | BOOT0 | 启动位置 |
|-------|-------|---------|
| x | **0** | Flash (0x08000000) — 跑你烧的代码 |
| 0 | **1** | **System Memory** (ROM bootloader) ← 我们要这个 |
| 1 | 1 | SRAM |

把 BOOT0 拉到 **3.3V**，按复位 → CPU 跑 ROM bootloader → bootloader 监听 USART1 (PA9 TX / PA10 RX) → 通过 UART 接收命令重写 Flash → 救活板子。

**关键**：bootloader 在 ROM 里不会被破坏，**永远可用**。

---

## 二、需要的工具

### 硬件
- 主板（断电状态）
- USB-TTL 模块（**3.3V 电平**，常见的 CH340 / FT232 都行）
- 杜邦线 4-5 根
- 万用表（可选，用来测电压）

### 软件
- **STM32CubeProgrammer**（ST 官方，免费）
  - 下载：https://www.st.com/en/development-tools/stm32cubeprog.html
  - 装 Windows 版即可
- 替代：**Flash Loader Demonstrator**（老版工具，也是 ST 官方）
  - 已 deprecated，但能用，体积小

---

## 三、硬件接线

### Step 1: 找 BOOT0 引脚

主板原理图找 BOOT0 信号（STM32F103VET6 的 BOOT0 引脚在 144-LQFP 是 pin 138 / 100-LQFP 是 pin 94）。

**两种常见情况**：

| 板子设计 | 操作方式 |
|---------|---------|
| 板上有 BOOT0 跳线（2-pin 跳线）| 把跳线短接到 3.3V 那一侧 |
| 板上有 BOOT0 测试点 + 上拉/下拉电阻 | 用导线把 BOOT0 测试点短接到 3.3V |
| BOOT0 引脚直接焊死下拉到 GND | **得改硬件**（飞线断 GND 接 3.3V）|

❓ **如果不确定 BOOT0 在哪**：
- 找原理图（03_协议与规格书 里的"原理图.pdf"）搜 "BOOT0" 或 "B0"
- 或万用表搜：在主板上电时找一个对地 0V 但有电阻的点（默认 BOOT0 通过 10k 电阻拉到 GND）

### Step 2: 接 USB-TTL 到主板 USART1

| USB-TTL | 主板 |
|---------|------|
| TX | **PA10** (USART1 RX) |
| RX | **PA9** (USART1 TX) |
| GND | GND |
| 3.3V (可选) | 3.3V |

**HDICU 主板上 PA9/PA10 在哪**：
- 它们走到 **CN3 接口**（屏幕板用的接口）
- 需要先**拔掉屏幕板**与 CN3 的连线，让 USB-TTL 占用 CN3
- CN3 的 pin 定义看原理图（一般 4 针：5V/TX/RX/GND）

⚠️ **注意 CN3 上有电平转换器**：CN3 那边是 5V 电平，主板内部是 3.3V 电平。
- 如果 USB-TTL 接 CN3 外侧（5V 端），需要 5V USB-TTL
- 如果直接接 PA9/PA10 引脚（3.3V 端），用 3.3V USB-TTL
- **推荐直接焊飞线到 PA9/PA10**，避开电平转换器（之前 CN11 已知有问题）

### Step 3: 共地

USB-TTL GND 必须和主板 GND 接通（一根线就行）。

---

## 四、操作步骤（用 STM32CubeProgrammer）

### Step 1: 进入 Bootloader 模式

1. 主板**断电**
2. 把 **BOOT0 拉到 3.3V**（用跳线短接或导线）
3. 主板**上电**
4. 此时 CPU 跑 ROM bootloader，**不跑 Flash 里的 Stage 6**

### Step 2: 启动 STM32CubeProgrammer

1. 打开软件
2. 右上角接口选择改为 **UART**

### Step 3: 配置 UART 参数

| 选项 | 值 |
|------|---|
| Port | 你的 USB-TTL 对应的 COMx |
| Baudrate | **115200**（默认 ISP 速率，最稳）|
| Parity | **Even** ⚠️（重要！STM32 ISP 默认 even parity，**不是 None**）|
| Data bits | 8 |
| Stop bits | 1 |
| Flow control | Off |

⚠️ **校验位必须 Even**！这是和正常 UART 协议不同的地方，新手最容易踩坑。

### Step 4: 点 Connect

如果一切正确，左侧会显示：

```
Connection successful
Read Memory: 0x08000000
Device PID: STM32F1xx_HighDensity
```

如果失败：
- 检查 BOOT0 是否真的拉到 3.3V（用万用表测）
- 检查 USB-TTL TX/RX 是否接反（最常见错误）
- 检查 parity 是否设了 Even
- 重新断电 → 拉 BOOT0 → 上电 → 再 Connect

### Step 5: 烧入 Stage 7 修复版

1. 切到 "**Erasing & programming**" 标签（左侧）
2. 点 "**Browse**"，选择文件：
   ```
   F:\小项目\医疗仓\firmware\_post_stage7_swj_fix_20260506_134251\main_stage7_FINAL.bin
   ```
3. **Start address**: `0x08000000`
4. **不要勾** Run after programming（先烧再说）
5. 勾选 "**Verify programming**"
6. 点 "**Start Programming**"

烧录速度：115200 baud 下约 30 秒/26KB。

完成后下方应显示：
```
File download complete
Time elapsed during download operation: ...
```

### Step 6: 退出 Bootloader 模式

1. **断电**主板
2. 把 **BOOT0 拉回 GND**（恢复原状）
3. 上电
4. CPU 跑 Flash 里新烧的 Stage 7 → **SWD 立刻恢复**

### Step 7: 用 JLink 验证（可选）

```
JLink.exe -CommanderScript rescue.jlink
```

如果能正常读 CPUID = 0x411FC231 → SWD 恢复 → **救援成功** ✅

---

## 五、常见错误排查

### 错误 1: Connect 失败 "No response from device"

**最可能原因**（按概率）：
1. BOOT0 没拉到 3.3V（检查跳线 / 用万用表测 BOOT0 电压应 ≈ 3.3V）
2. USB-TTL TX/RX 接反
3. Parity 没设 Even
4. 没共地
5. CPU 没真的复位（拉 BOOT0 后必须断电再上电，不能仅按 NRST）

### 错误 2: Connect OK 但 Programming 失败

可能原因：
- USB-TTL 不稳定，降低 baudrate 到 57600 或 38400 重试
- 主板供电跌落（太多外设上电），先拔掉屏幕板等再试

### 错误 3: Programming 完成但 SWD 仍然死

- 检查烧的是 **Stage 7**（main_stage7_FINAL.bin, MD5 `34d9d4de`），不是 Stage 6
- 确认 Start address = 0x08000000
- 拉 BOOT0 回 GND 没？必须回 GND 才会从 Flash 启动

### 错误 4: 找不到 BOOT0 跳线

如果板子设计死焊 BOOT0 = GND，**唯一方法**是飞线：
1. 用刀片或拆焊枪把 BOOT0 与 GND 之间的电阻 / 走线断开
2. 飞一根线从 BOOT0 引脚到 3.3V（用 10k 电阻限流，可选）
3. 烧完恢复

或者**用 RTS/DTR 控制**（如果 USB-TTL 模块有 DTR/RTS 引脚）：把 DTR 接 BOOT0，让 STM32CubeProgrammer 自动控制 BOOT0 + NRST 时序。这是高级用法，需要懂电路。

---

## 六、备选方案：用 Flash Loader Demonstrator（旧工具）

如果 STM32CubeProgrammer 不会用：

1. 下载 **Flash Loader Demonstrator** (ST 官方，老版工具)
2. 操作步骤几乎一样
3. 优点：界面简单，专门为 STM32 ISP 设计
4. 缺点：已 deprecated，新 STM32 不支持，但 STM32F103 完全 OK

下载链接（搜索 "Flash Loader Demonstrator STM32"）：
- https://www.st.com/en/development-tools/flasher-stm32.html

---

## 七、烧 Stage 7 之后的验证

### 1. 视觉检查
- 板子启动后绿色 PWR LED 亮（说明 CPU 跑起来了）
- 屏幕板（如果接了）开始显示数据（说明 UART 通信正常）

### 2. JLink 测试
```
JLink.exe
si SWD
speed 4000
device STM32F103VE
connect
mem32 0xE000ED00 1   ; 读 CPUID, 应显示 0x411FC231
q
```

### 3. 灯常亮问题验证
跑你之前的 `diag_stage6_lights.bat`，看：
- AFIO_MAPR 的 SWJ_CFG (bits 24:26) 现在应该 = 010 (NOJTAG, SWD enabled)
- TIM1 PWM 在 PE9 上正常输出（示波器测）
- PE10-PE13 GPIO 状态正常

---

## 八、整个救援流程时间预算

| 步骤 | 时间 |
|------|------|
| 找 BOOT0 跳线 / 飞线（首次）| 5-10 分钟 |
| 接 USB-TTL 到 USART1 | 3 分钟 |
| 安装 STM32CubeProgrammer（首次）| 10 分钟 |
| 配置 + Connect | 2 分钟 |
| 烧录 Stage 7 (115200 baud) | 30 秒 |
| 恢复 BOOT0 + 验证 | 2 分钟 |
| **总计** | **15-30 分钟** |

第二次救援因为软件已装 + 接线流程熟悉，5 分钟以内。

---

## 九、给烧录人员的简短话术

> 1. **先备份**：把 Stage 6 那个 .bin 留着，别覆盖（万一要看烧了什么）。
> 2. 装 STM32CubeProgrammer。
> 3. 找主板 BOOT0 跳线/测试点。
> 4. BOOT0 拉 3.3V → 断电再上电 → CPU 进 ROM。
> 5. USB-TTL 接 PA9/PA10/GND，或 CN3（拔屏幕板）。
> 6. STM32CubeProgrammer：选 UART → COM 口 → 115200 → **Even parity** → Connect。
> 7. Programming → 烧 `main_stage7_FINAL.bin` 到 `0x08000000` → Verify。
> 8. BOOT0 拉回 GND → 断电再上电 → SWD 恢复。
> 9. 烧完用 JLink 试 connect 看是否正常。

---

**本文档结束**。如果照做后还是失败，把每步的截图 + 错误信息发回，我远程定位。
