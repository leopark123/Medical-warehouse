# Stage 7 — 紧急修复 Stage 6 致命 SWJ_CFG bug

**生成时间**：2026-05-06 13:42
**状态**：已编译，**未实测**（Stage 6 砖板需先救活才能测）
**优先级**：🔴 **紧急** — Stage 6 板子已变砖，必须烧 Stage 7 才能恢复

---

## 一、Stage 6 致命 bug 说明

### 现象
- Stage 6 烧录后 **JLink 永远无法连接**
- 反复 "Can not attach to CPU. Trying connect under reset. Connecting via connect under reset failed."

### 根因
Stage 6 的 `pwm_driver.c` 在 AFIO_MAPR 写 TIM1 全重映射时用了 RMW（read-modify-write）：

```c
uint32_t mapr = AFIO->MAPR;       // 读
mapr &= ~(0x3UL << 6);
mapr |=  (0x3UL << 6);
AFIO->MAPR = mapr;                // 写回
```

STM32F103 reference manual：**SWJ_CFG[2:0] bits at 24:26 are write-only (when read, the value is undefined)**.

读 AFIO->MAPR 时 SWJ_CFG 返回未定义值。RMW 把这个未定义值写回，**可能写出 SWJ_CFG = 100 = 完全禁用 SWD+JTAG**。

CPU 仍能跑 Flash 里的代码，但 **SWD 调试端口被关闭**，JLink 永远无法 attach。

---

## 二、Stage 7 修复

[pwm_driver.c Stage 7 修复段](firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.c)：
```c
uint32_t mapr = AFIO->MAPR;
mapr &= ~((0x3UL << 6) | (0x7UL << 24));   /* 清 TIM1_REMAP + SWJ_CFG */
mapr |=  ((0x3UL << 6) | (0x2UL << 24));   /* 设 TIM1=11, SWJ_CFG=010 (NOJTAG) */
AFIO->MAPR = mapr;
```

**显式覆盖 SWJ_CFG = 010**（NOJTAG, SWD 保留），无论之前读什么值都会写回正确状态。

---

## 三、文件清单

| 文件 | 大小 | MD5 |
|------|------|------|
| `main_stage7_FINAL.bin` | 27200 B | `34d9d4de15824932418c5756e17ba98f` |
| `main_stage7_FINAL.elf` | 60952 B | — |
| `screen_stage7_FINAL.bin` | 5244 B | `b6307c333eaa6f4100de7afeb19dd10b`（与 Stage 5/6 同，不需改）|

---

## 四、烧录步骤

### 情况 A：板子上还是 Stage 5（未烧 Stage 6）
直接 JLink 烧 `main_stage7_FINAL.bin`：
```
si SWD
speed 4000
device STM32F103VE
connect
h
loadbin <path>\main_stage7_FINAL.bin 0x08000000
verifybin <path>\main_stage7_FINAL.bin 0x08000000
r
g
q
```

### 情况 B：板子上是 Stage 6（已变砖，JLink 连不上）
**必须用 BOOT0 ISP UART 烧录**。详见 `tools/BOOT0_ISP_rescue_guide.md`。

简要步骤：
1. 拉 BOOT0 = 3.3V
2. 断电再上电（CPU 进 ROM bootloader）
3. USB-TTL 接 PA9/PA10/GND
4. STM32CubeProgrammer：UART / 115200 / **Even parity** / COM 口
5. Connect
6. 烧 `main_stage7_FINAL.bin` 到 `0x08000000`
7. 拉 BOOT0 回 GND
8. 断电再上电 → SWD 恢复 → 验证 JLink 能 attach

---

## 五、验证 Stage 7 修复成功

烧完 Stage 7 后，跑 JLink 诊断：

```
mem32 0x40010004 1
```

读 AFIO_MAPR 寄存器值应为：
- bit 6:7 = 11 (TIM1 全重映射) → 0xC0
- bit 24:26 = 010 (SWJ_NOJTAG) → 0x02000000
- 总值大约 = 0x020000C0

如果读出来 SWJ_CFG bits 都是 0（不是 010），可能 SWJ_CFG 读返回 undefined（这是预期）。关键是 **JLink 能 attach** = SWD 工作 = 修复成功。

---

## 六、Stage 7 之后还有别的 bug 吗？

### 已知遗留问题
- "灯常亮"问题：Stage 5 设计行为（加热触发 LED），Stage 7 也保留（除非另写 Stage 8 改 LEDA_Update 逻辑）
- 屏幕板 CN11 通信只能通过 ~3% 字节（电平转换器硬件问题，不是固件问题）

### Stage 7 = Stage 6 + SWJ 修复
其他 Stage 6 的 PWM 改动（TIM1_CH1 @ 15 kHz）保留。

---

## 七、回滚方案

如果 Stage 7 也有问题（可能性极低，仅改 4 行）：

```
# 回滚到 Stage 5 (不带 PWM 频率改动, 100Hz buzz 但 SWD OK)
cp firmware/_post_stage5_20260505_210540/main_stage5_FINAL.bin <烧录路径>
```

---

## 八、烧录人员行动清单

- [ ] 备份当前板子上的 Stage 6 .bin（用 JLink savebin 或 BOOT0 ISP 读 Flash）
- [ ] 装 STM32CubeProgrammer
- [ ] 看 `tools/BOOT0_ISP_rescue_guide.md`
- [ ] 找主板 BOOT0 跳线 / 飞线
- [ ] BOOT0 拉 3.3V → 断电再上电
- [ ] USB-TTL 接 PA9/PA10
- [ ] STM32CubeProgrammer 烧 Stage 7
- [ ] 拉 BOOT0 回 GND，断电再上电
- [ ] JLink 验证 attach 成功
- [ ] 反馈结果

---

**本文档结束**。
