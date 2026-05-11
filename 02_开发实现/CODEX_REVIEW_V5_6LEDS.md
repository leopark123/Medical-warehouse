# Codex Review: v5 — 屏幕板 6 颗状态 LED

请审查这次**屏幕板单边**改动 (LED 屏原理图新增 6 颗状态 LED), 重点找:
- 6 个引脚是否真的空闲, 没漏掉某处占用
- active-low / GPIO 配置是否对
- PC14 处理 (默认 OSC32_IN) 是否完整
- LEDA_Update 逻辑是否跟现有 LED 风格一致

---

## 0. 前情提要

你之前 (`CODEX_REVIEW_V4_4_REVERT.md`) 审过 v4.4 → v4.3 回滚, 主板状态在 v4.3 + 清理 = `233cd8a`。
然后用户提需求: 在屏幕板加 6 颗状态 LED, **不动主板协议**。

你之前已经分析过这 6 个引脚是**屏幕板 GD32F303RCT6** 上的 (不是主板 STM32F103), 数据源主板 0x01 帧都有:
- `relay_status` (d[16-17])
- `disinfect_remaining` (d[12-13])

按你的分析方向做了实现, 现在请确认。

---

## 1. 改动范围

**只改 `firmware/HDICU_ScreenBoard/main.c`**, 主板和所有其他文件不动:
- `LEDA_Init` 加 6 个引脚 GPIO output PP 2MHz 配置 + 默认 HIGH (灭)
- `LEDA_Update` 末尾加 6 颗 LED active-low 输出逻辑
- 净增 75 行 / 删 16 行 (init 区扩展)

主板 bin **未重编** (沿用 v4.3, MD5 `6a5997fd...`)。

---

## 2. LED 引脚分配

| LED | 引脚 | GD32F303RC 默认功能 | 当前屏幕板占用 | 触发条件 |
|---|---|---|---|---|
| 压缩机/加热 | **PB11** | UART3_RX (默认复用) | 屏幕板未用 UART3 ✓ | `relay_status & ((1<<0) \| (1<<1) \| (1<<7))` |
| 加湿器 | **PA11** | USB_DM | 屏幕板未用 USB ✓ | `relay_status & (1<<5)` |
| O2 阀 | **PA12** | USB_DP | 屏幕板未用 USB ✓ | `relay_status & (1<<4)` |
| 雾化 | **PC1** | ADC12_IN11 | 屏幕板不读 ADC ✓ | `relay_status & (1<<8)` |
| 消毒倒计时 | **PC14** | OSC32_IN (LSE) | 屏幕板未启 LSE (SystemClock 用 HSE/PLL ×9) | `disinfect_remaining > 0` |
| 供氧计时 | **PA1** | ADC12_IN1 | 屏幕板不读 ADC ✓ | `relay_status & (1<<4)` (= 跟 PA12 同步) |

### 占用核对证据 (屏幕板)
- LEDA_Init 之前用的: PA0/PA5/PA15 (LEDA1/6/8), PB10 (LEDA4), PC4/PC5/PC13/PC15 (LEDA2/3/5/7)
- LED_Fan_Init: PA4/PB9/PD2 (LED8/9/10 风速)
- UART1_Init: PA9/PA10
- UART2_Init: PA2/PA3
- TM1640: PB3/PB4/PB6/PB7
- Key_Init: PA8 (KEY9), PB12-15 (KEY1-4), PC6-9 (KEY5-8)

**6 个新加 LED 引脚 (PA1/PA11/PA12/PB11/PC1/PC14) 跟以上都不冲突** ✓

---

## 3. 实现 — 完整代码 diff

### Diff A: LEDA_Init 加 6 个 GPIO 配置

```c
static void LEDA_Init(void)
{
    /* Enable GPIO clocks: A(bit2), B(bit3), C(bit4) */
    RCC_APB2ENR |= (1 << 2) | (1 << 3) | (1 << 4);

    /* --- GPIOA: PA0[3:0], PA5[23:20], PA15 via CRH[31:28]
     * v5 (2026-05-11): + PA1 (供氧计时), PA11 (加湿), PA12 (O2 阀) */
    uint32_t crl = GPIOA_CRL;
    crl &= ~(0xFU << 0);    /* PA0 */
    crl |=  (0x2U << 0);
    crl &= ~(0xFU << 4);    /* PA1  v5 */
    crl |=  (0x2U << 4);
    crl &= ~(0xFU << 20);   /* PA5 */
    crl |=  (0x2U << 20);
    GPIOA_CRL = crl;
    uint32_t crh = GPIOA_CRH;
    crh &= ~(0xFU << 12);   /* PA11 v5 */
    crh |=  (0x2U << 12);
    crh &= ~(0xFU << 16);   /* PA12 v5 */
    crh |=  (0x2U << 16);
    crh &= ~(0xFU << 28);   /* PA15 */
    crh |=  (0x2U << 28);
    GPIOA_CRH = crh;
    /* Default HIGH = LEDs off (active-low) */
    GPIOA_BSRR = (1 << 0) | (1 << 1) | (1 << 5) | (1 << 11) | (1 << 12) | (1 << 15);

    /* --- GPIOB: PB10 (LEDA4), PB11 (v5 压缩机/加热) --- */
    {
        uint32_t b_crh = GPIOB_CRH;
        b_crh &= ~(0xFU << 8);     /* PB10 */
        b_crh |=  (0x2U << 8);
        b_crh &= ~(0xFU << 12);    /* PB11 v5 */
        b_crh |=  (0x2U << 12);
        GPIOB_CRH = b_crh;
        GPIOB_BSRR = (1 << 10) | (1 << 11);    /* HIGH = off */
    }

    /* --- GPIOC: PC4/5/13/15 现有, PC1 (v5 雾化), PC14 (v5 消毒) --- */
    crl = GPIOC_CRL;
    crl &= ~(0xFU << 4);    /* PC1  v5 */
    crl |=  (0x2U << 4);
    crl &= ~(0xFU << 16);   /* PC4 */
    crl |=  (0x2U << 16);
    crl &= ~(0xFU << 20);   /* PC5 */
    crl |=  (0x2U << 20);
    GPIOC_CRL = crl;
    crh = GPIOC_CRH;
    crh &= ~(0xFU << 20);   /* PC13 */
    crh |=  (0x2U << 20);
    crh &= ~(0xFU << 24);   /* PC14 v5 */
    crh |=  (0x2U << 24);
    crh &= ~(0xFU << 28);   /* PC15 */
    crh |=  (0x2U << 28);
    GPIOC_CRH = crh;
    GPIOC_BSRR = (1 << 1) | (1 << 4) | (1 << 5) | (1 << 13) | (1 << 14) | (1 << 15);
}
```

### Diff B: LEDA_Update 末尾加 6 颗 LED

(在风机 LED 块后, 函数 `}` 前)

```c
/* === Stage 8 redo v5 (2026-05-11): 6 颗状态 LED — LED 屏原理图新增 ===
 * 数据源全部来自 0x01 帧已有字段, 不动主板协议.
 * 全部 active-low: BSRR (pin+16)=LOW=on, BSRR pin=HIGH=off */
{
    /* PB11 — 压缩机或加热: bit 0 (PTC) OR bit 1 (JIARE) OR bit 7 (YASUO) */
    const uint16_t MASK_HEAT_COMP = (1U << 0) | (1U << 1) | (1U << 7);
    if (relay_st & MASK_HEAT_COMP) GPIOB_BSRR = (1 << (11 + 16));
    else                           GPIOB_BSRR = (1 << 11);

    /* PA11 — 加湿器: bit 5 (JIASHI) */
    if (relay_st & (1U << 5)) GPIOA_BSRR = (1 << (11 + 16));
    else                      GPIOA_BSRR = (1 << 11);

    /* PA12 — 制氧机阀门: bit 4 (O2) */
    if (relay_st & (1U << 4)) GPIOA_BSRR = (1 << (12 + 16));
    else                      GPIOA_BSRR = (1 << 12);

    /* PC1 — 雾化: bit 8 (WH) */
    if (relay_st & (1U << 8)) GPIOC_BSRR = (1 << (1 + 16));
    else                      GPIOC_BSRR = (1 << 1);

    /* PC14 — 消毒倒计时: disinfect_remaining > 0 */
    uint16_t dis_rem = (uint16_t)((d[12] << 8) | d[13]);
    if (dis_rem > 0) GPIOC_BSRR = (1 << (14 + 16));
    else             GPIOC_BSRR = (1 << 14);

    /* PA1 — 供氧计时: O2 阀实际打开就亮 (跟 PA12 同步)
     * 语义 = "正在供氧" (与 LEDA5 PC13 / PA12 一致) */
    if (relay_st & (1U << 4)) GPIOA_BSRR = (1 << (1 + 16));
    else                      GPIOA_BSRR = (1 << 1);
}
```

---

## 4. 设计决策

### Q1: 6 颗 LED 都是 active-low
跟现有 LEDA1-8 / LED8/9/10 一致 (LOW = on, HIGH = off). 用户原话"亮 / 灭"没明确极性, 按现有风格做。如果实际原理图是 active-high, 一行翻转即可。

### Q2: PA1 供氧计时语义 = O2 阀打开就亮 (Q1=A)
用户在前一轮选了 A (跟 PA12 同步, 阀开就亮)。意味着 PA1 / PA12 / 屏幕板 LEDA5 (PC13) 三盏同时亮灭, 都代表 O2 阀实际开启。

### Q3: PC14 没显式禁 LSE
屏幕板 `SystemClock_Config` 用 HSE 8MHz → PLL ×9 → 72MHz, **没启用 LSE**, 也没用 RTC。
PC15 已经成功被用作 LEDA7 output (line 1268), 说明 backup domain 默认不锁住 PC14/PC15。
所以 PC14 直接配 GPIO output 应该可行, 跟 PC15 同理。

**风险**: 如果 GD32F303RC 跟 STM32F103 行为有微差, PC14 可能仍被 backup 锁。但
1. 屏幕板代码风格里 `LEDA_Init` 直接位操作 CRL/CRH, 不用 HAL, 跟 PC15 完全对称, 应该 OK
2. 烧录后实测就能确认 PC14 亮灭是否跟着 disinf_rem 变化, 如果坏只是 PC14 不亮, 其他 5 颗仍工作, 可加补丁

### Q4: 主板协议不动
所有数据源都从已有的 0x01 帧字段读 (relay_status / disinfect_remaining), 不需要扩展 0x01 帧长度, 不需要新命令, 主板 v4.3 bin 完全不动。

---

## 5. 编译产物

- 屏幕板 bin: `firmware/_post_stage8_redo_v5_6leds_20260511_205157/screen_stage8_redo_v5_FINAL.bin`
- **MD5**: `2d08e6f9458c985f689b4a7d97e0bf5c` (v1 是 `112f4c0355359106c36272d5051bf9b6`)
- 主板 bin: 沿用 v4.3 `main_stage8_redo_v4_3_FINAL.bin` (MD5 `6a5997fdf5c3d690d97d8480b602c601`), **不需重烧**
- text 5776 (vs v1 体积接近), data 12, bss 496
- git commit `7516c7c`, tag `stage8-redo-v5-6leds-pre-codex` (已 push)

---

## 6. 给 Codex 的 6 个审查点

### A. 引脚冲突
6 个引脚 (PA1/PA11/PA12/PB11/PC1/PC14) 跟屏幕板现有代码 (LEDA1-8 / LED8/9/10 / TM1640 / UART1/2 / KEY) 没冲突? 漏看了某处占用?

### B. GPIO 配置
CRL/CRH 位操作 (按 GD32F303RC 的 GPIO 寄存器位定义) 是否对? PA11 在 CRH bit 12-15 (= bit (11-8)*4=12), PA12 在 CRH bit 16-19. PC1 在 CRL bit 4-7. PC14 在 CRH bit 24-27. 算对了?

### C. active-low 极性
跟现有 LED 一致 (BSRR (pin+16) = reset = LOW = on)? 默认初始化为 HIGH (灭)?

### D. PC14 / OSC32_IN
不显式禁 LSE 直接当 GPIO 用, GD32F303 是否可行? PC15 在 line 1268 已经被同样方式用作 LEDA7, 证明这套写法 work, PC14 同理可行?

### E. LEDA_Update 逻辑
- relay_st 已经从 d[16]/d[17] 提取 (line 1297, 在新增代码之前), 复用即可 ✓
- dis_rem 在新增块内重新提取 d[12]/d[13] — 是否需要跟函数前面的某个变量复用 (我没看到 disinfect_remaining 之前被提取过, 所以新提取是必须的)
- 每 100ms 调一次, 跟其他 LED 一起更新, 周期跟随 ✓

### F. 安全 / 副作用
- 6 颗 LED 加上去, 不影响主板原有控制逻辑 ✓ (主板不动)
- 不影响 LEDA1-8 / LED8/9/10 (它们用不同引脚) ✓
- 不影响 KEY / UART / TM1640 ✓
- 烧录失败时屏幕板可回滚到 v1 (MD5 `112f4c03...`), 风险可控

---

## 7. 测试用例 (烧录后)

- [ ] 主板设温度高 5°C → PB11 LED **亮** (PTC 启动)
- [ ] 主板设温度低 5°C → 等加热停, PB11 LED **灭** (压缩机不开)
- [ ] iPad 写 target_humidity > sensor → 加湿启动 → **PA11 亮**
- [ ] iPad 长按 KEY5 开放供氧 → **PA12 亮 + PA1 亮 + LEDA5 (PC13) 亮** (三盏同步)
- [ ] iPad 0x83 启动雾化 60s → **PC1 亮** (雾化运行中)
- [ ] iPad 0x83 启动消毒 60s → **PC14 亮** (倒计时 > 0); 60s 后 → PC14 **灭**
- [ ] 退出开放供氧 (KEY5 长按再触发) → **PA12 + PA1 + LEDA5 全灭**
- [ ] 回归: LEDA1-8 + LED8/9/10 + 7 段数码管显示全部正常 (不退化)

---

## 8. 回应格式

```
[整体判断] Pass / Concern / Block

A. 引脚冲突: ...
B. GPIO 配置: ...
C. active-low: ...
D. PC14: ...
E. LEDA_Update 逻辑: ...
F. 安全: ...

[是否建议烧录 v5 屏幕板 bin 测试]: Y / N
```

如全 Pass + Y, 用户立即烧 `screen_stage8_redo_v5_FINAL.bin`, **只烧屏幕板, 主板不动**。
