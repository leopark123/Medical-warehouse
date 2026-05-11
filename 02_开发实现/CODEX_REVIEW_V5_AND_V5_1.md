# Codex Review: v5 + v5.1 — 屏幕板单边改动审查

请一次性审查 **v5** (6 颗状态 LED) + **v5.1** (撤销逻辑改) 两个 commit。两个都只动屏幕板, 主板 v4.3 完全不变。

---

## 0. 前情提要

你审过的链:
- `CODEX_REVIEW_STAGE_8_REDO_V4.md` → v4 概况
- `CODEX_REVIEW_STAGE_8_REDO_V4_1_FIX.md` → v4.1 修 3 Block
- (v4.2 Codex Pass, 直接审到 v4.3 上线)
- `CODEX_REVIEW_V4_4_REVERT.md` → v4.4 → v4.3 回滚验证
- `CODEX_REVIEW_V5_6LEDS.md` → v5 单 (本次合并到这里)

**主板基线**: v4.3 (`stage8-redo-v4-3`, commit `158f2f8`, MD5 `6a5997fdf5c3d690d97d8480b602c601`)

**屏幕板演进**:
- v1 build: `112f4c0355359106c36272d5051bf9b6` (在线运行版)
- v5 (6 LED): `2d08e6f9458c985f689b4a7d97e0bf5c` (待 Codex 审, 未烧)
- **v5.1** (撤销改): `1ab1e527105fdaf70b58c47741630a6c` (当前, 待 Codex 审, 未烧) ← 本次审查目标

---

## 1. 改动 1: v5 — 6 颗状态 LED

按 LED 屏原理图加 6 颗状态 LED, 数据源全部从主板 0x01 帧 (relay_status / disinfect_remaining) 读, **不动主板协议**。

### 引脚分配 + 占用核对

| LED | 引脚 | GD32F303RC 默认 | 屏幕板当前占用 | 触发条件 |
|---|---|---|---|---|
| 压缩机/加热 | **PB11** | UART3_RX | 未用 UART3 ✓ | `relay & ((1<<0)\|(1<<1)\|(1<<7))` |
| 加湿器 | **PA11** | USB_DM | 未用 USB ✓ | `relay & (1<<5)` |
| O2 阀 | **PA12** | USB_DP | 未用 USB ✓ | `relay & (1<<4)` |
| 雾化 | **PC1** | ADC12_IN11 | 屏幕板不读 ADC ✓ | `relay & (1<<8)` |
| 消毒倒计时 | **PC14** | OSC32_IN (LSE) | 未启 LSE (SystemClock 用 HSE/PLL) | `disinf_rem > 0` |
| 供氧计时 | **PA1** | ADC12_IN1 | 屏幕板不读 ADC ✓ | `relay & (1<<4)` (= 同 PA12 同步) |

### 现有屏幕板 GPIO 使用 (供 Codex 核对没漏)
- LEDA1-8: PA0/PA5/PA15 (LEDA1/6/8), PB10 (LEDA4), PC4/PC5/PC13/PC15 (LEDA2/3/5/7)
- 风速 LED: PA4/PB9/PD2 (LED8/9/10)
- UART1: PA9/PA10, UART2: PA2/PA3
- TM1640: PB3/PB4/PB6/PB7
- Key: PA8 (KEY9), PB12-15 (KEY1-4), PC6-9 (KEY5-8)
- 编码器: 在主板, 屏幕板不直接读

**6 个新加 LED 全部跟以上不冲突** ✓

### LEDA_Init 完整改动 (CRL/CRH 位操作)

```c
static void LEDA_Init(void)
{
    RCC_APB2ENR |= (1 << 2) | (1 << 3) | (1 << 4);    /* GPIOA/B/C */

    /* GPIOA: PA0 PA1(new) PA5 PA11(new) PA12(new) PA15 */
    uint32_t crl = GPIOA_CRL;
    crl &= ~(0xFU << 0);  crl |= (0x2U << 0);    /* PA0  LEDA6 */
    crl &= ~(0xFU << 4);  crl |= (0x2U << 4);    /* PA1  v5 供氧计时 */
    crl &= ~(0xFU << 20); crl |= (0x2U << 20);   /* PA5  LEDA1 */
    GPIOA_CRL = crl;
    uint32_t crh = GPIOA_CRH;
    crh &= ~(0xFU << 12); crh |= (0x2U << 12);   /* PA11 v5 加湿 */
    crh &= ~(0xFU << 16); crh |= (0x2U << 16);   /* PA12 v5 O2 阀 */
    crh &= ~(0xFU << 28); crh |= (0x2U << 28);   /* PA15 LEDA8 */
    GPIOA_CRH = crh;
    GPIOA_BSRR = (1<<0) | (1<<1) | (1<<5) | (1<<11) | (1<<12) | (1<<15);   /* 默认 HIGH = off */

    /* GPIOB: PB10 PB11(new) */
    {
        uint32_t b_crh = GPIOB_CRH;
        b_crh &= ~(0xFU << 8);  b_crh |= (0x2U << 8);     /* PB10 LEDA4 */
        b_crh &= ~(0xFU << 12); b_crh |= (0x2U << 12);    /* PB11 v5 压缩机/加热 */
        GPIOB_CRH = b_crh;
        GPIOB_BSRR = (1 << 10) | (1 << 11);
    }

    /* GPIOC: PC1(new) PC4 PC5 PC13 PC14(new) PC15 */
    crl = GPIOC_CRL;
    crl &= ~(0xFU << 4);  crl |= (0x2U << 4);    /* PC1  v5 雾化 */
    crl &= ~(0xFU << 16); crl |= (0x2U << 16);   /* PC4 LEDA2 */
    crl &= ~(0xFU << 20); crl |= (0x2U << 20);   /* PC5 LEDA3 */
    GPIOC_CRL = crl;
    crh = GPIOC_CRH;
    crh &= ~(0xFU << 20); crh |= (0x2U << 20);   /* PC13 LEDA5 */
    crh &= ~(0xFU << 24); crh |= (0x2U << 24);   /* PC14 v5 消毒倒计时 */
    crh &= ~(0xFU << 28); crh |= (0x2U << 28);   /* PC15 LEDA7 */
    GPIOC_CRH = crh;
    GPIOC_BSRR = (1<<1) | (1<<4) | (1<<5) | (1<<13) | (1<<14) | (1<<15);
}
```

### LEDA_Update 加 6 颗 LED 显示

```c
/* 在风机 LED 块后面, 函数 } 前 */
{
    /* PB11 — 压缩机或加热 */
    const uint16_t MASK_HEAT_COMP = (1U << 0) | (1U << 1) | (1U << 7);
    if (relay_st & MASK_HEAT_COMP) GPIOB_BSRR = (1 << (11 + 16));
    else                           GPIOB_BSRR = (1 << 11);

    /* PA11 — 加湿器 (bit 5) */
    if (relay_st & (1U << 5)) GPIOA_BSRR = (1 << (11 + 16));
    else                      GPIOA_BSRR = (1 << 11);

    /* PA12 — O2 阀 (bit 4) */
    if (relay_st & (1U << 4)) GPIOA_BSRR = (1 << (12 + 16));
    else                      GPIOA_BSRR = (1 << 12);

    /* PC1 — 雾化 (bit 8) */
    if (relay_st & (1U << 8)) GPIOC_BSRR = (1 << (1 + 16));
    else                      GPIOC_BSRR = (1 << 1);

    /* PC14 — 消毒倒计时 */
    uint16_t dis_rem = (uint16_t)((d[12] << 8) | d[13]);
    if (dis_rem > 0) GPIOC_BSRR = (1 << (14 + 16));
    else             GPIOC_BSRR = (1 << 14);

    /* PA1 — 供氧计时 (= 跟 PA12 同步, 阀开就亮) */
    if (relay_st & (1U << 4)) GPIOA_BSRR = (1 << (1 + 16));
    else                      GPIOA_BSRR = (1 << 1);
}
```

全部 active-low (跟现有 LEDA / LED8/9/10 一致): BSRR (pin+16) = reset = LOW = on; BSRR pin = set = HIGH = off。

---

## 2. 改动 2: v5.1 — 撤销时一律清 enable

### 用户场景
用户反馈: "已经开着温控 (target=26), 进 SET_TEMP 改成 28, 长按撤销, 只回退到 26, 温控没关"。

经分析 + 跟用户确认, 产品决策: **撤销 = 一律清 enable**, 不再保留 "进页前已开就继续开" 的语义。

### 改动 (3 处 if 删除)

`hmi_revert_current_set_page` 的 3 个 SET 分支:

**改前**:
```c
case HMI_PAGE_SET_TEMP:
    s_set_temp_x10 = s_orig_temp_x10;
    send_param_set(0x01, (uint16_t)s_orig_temp_x10);
    if (s_orig_enable_temp == 0) send_key_action(0x0C, 0x02);   /* 条件清 */
    return 1;
case HMI_PAGE_SET_HUMID:
    ...
    if (s_orig_enable_humid == 0) send_key_action(0x0D, 0x02);
    return 1;
case HMI_PAGE_SET_O2:
    ...
    if (s_orig_enable_o2 == 0) send_key_action(0x0E, 0x02);
    return 1;
```

**改后**:
```c
case HMI_PAGE_SET_TEMP:
    s_set_temp_x10 = s_orig_temp_x10;
    send_param_set(0x01, (uint16_t)s_orig_temp_x10);
    send_key_action(0x0C, 0x02);    /* v5.1: 一律清 enable */
    return 1;
case HMI_PAGE_SET_HUMID:
    ...
    send_key_action(0x0D, 0x02);    /* v5.1 */
    return 1;
case HMI_PAGE_SET_O2:
    ...
    send_key_action(0x0E, 0x02);    /* v5.1 */
    return 1;
```

### 主板对应 case (`screen_protocol.c`, 没改, 已正确)

```c
case 0x0C: /* 虚拟 KeyID — SET_TEMP 撤销时清 enable_temp_ctrl */
    if (action == 0x02) d->setpoint.enable_temp_ctrl = 0;
    break;
case 0x0D: /* 同上, 湿控 */
    if (action == 0x02) d->setpoint.enable_humid_ctrl = 0;
    break;
case 0x0E: /* 同上, O2 控 */
    if (action == 0x02) d->setpoint.enable_o2_ctrl = 0;
    break;
```

### 命令顺序 (重要: 不会被自动重开覆盖)

撤销时屏幕板发**两条**命令, 顺序固定:
1. `send_param_set(0x01, s_orig_temp_x10)` → 主板 case 0x01: `target_temp = value` + `enable_temp_ctrl = 1` (自动开)
2. `send_key_action(0x0C, 0x02)` → 主板 case 0x0C: `enable_temp_ctrl = 0`

UART 同步发送, 主板 RX 顺序处理, 最终 `enable = 0`。中间有微秒级 `enable = 1` 窗口, 但 ControlTask 200ms 周期内只采样最终态。

### 副作用 (已跟用户确认接受)
用户在 SET_TEMP 页**手抖长按** → 温控被意外关。恢复方式: 重新单击进 SET_TEMP, 旋转新值 (case 0x01 自动 enable=1)。

---

## 3. 不变化 (回归基线)

- **主板**: v4.3 bin 不重编, MD5 `6a5997fd...` 不变
- **屏幕板其他**: LEDA1-8 / LED8/9/10 / TM1640 数字显示 / KEY / UART / 编码器流程 / hmi_cycle_page / hmi_seed_from_display 全部未动
- **协议层**: 0x01 帧布局 / 0x82 KeyID 0x0C/0x0D/0x0E 已存在 / 不动
- **主板 case 0x0C/0x0D/0x0E**: 没动 (本来就清 enable, 实现正确)

---

## 4. 编译产物

| 项 | v5.1 |
|---|---|
| bin | `firmware/_post_stage8_redo_v5_1_revert_clear_20260511_213240/screen_stage8_redo_v5_1_FINAL.bin` |
| **MD5** | `1ab1e527105fdaf70b58c47741630a6c` |
| text | 5684 (-92 vs v5 5776, GCC LTO 优化掉死代码 s_orig_enable_*) |
| data | 12 |
| bss | 492 (-4 vs v5) |
| commit | `61a4040` (已 push origin) |
| tag | `stage8-redo-v5-1-revert-always-clear` |

---

## 5. 给 Codex 的 8 个审查点

### 5.1 v5 部分

**A. 引脚冲突 (核对没漏)**:
6 个新加 LED 引脚 (PA1/PA11/PA12/PB11/PC1/PC14) 跟屏幕板现有所有 GPIO 使用 (LEDA1-8 / 风速 LED / TM1640 / UART1/2 / KEY) 都不冲突? 是否漏了某个角落 (interrupt / SystemClock / IWDG / etc.)?

**B. CRL/CRH 位操作**:
每个引脚的 4-bit nibble 位置算对了? 
- PA1 在 GPIOA_CRL bit [7:4]
- PA11 在 GPIOA_CRH bit [15:12]
- PA12 在 GPIOA_CRH bit [19:16]
- PB11 在 GPIOB_CRH bit [15:12]
- PC1 在 GPIOC_CRL bit [7:4]
- PC14 在 GPIOC_CRH bit [27:24]

**C. PC14 / OSC32_IN**:
不显式 `RCC->BDCR &= ~LSEON` 直接当 GPIO, GD32F303 是否可行? PC15 已成功被 LEDA7 用 (同样 backup domain 关联引脚), 是否证明 PC14 同理工作?

**D. active-low 极性**:
跟 LEDA1-8 / LED8/9/10 一致 (BSRR (pin+16) = LOW = on)。LED 屏原理图实际接法如果是 active-high, 一行翻转即可。

### 5.2 v5.1 部分

**E. 撤销语义变化**:
原 spec "撤销 = 回进页前状态" 改成 "撤销 = 一律关控制 + 回滚 target"。这跟主板 case 0x01/0x02/0x03 "写参数自动开 enable" 配合后, 用户行为是否符合产品需求? 是否引入新边界 case?

**F. 命令顺序 race**:
send_param_set + send_key_action 同步发出, 主板 UART RX 顺序处理。是否存在中间状态 (enable=1 闪一下) 让 ControlTask 误触发?
- ControlTask 周期 200ms
- 两条命令 UART 发送 < 1ms
- 主板 dispatch 在 CommScreen task (100ms 周期或事件触发)
- 中间窗口微秒级, ControlTask 几乎不会采样到

**G. v5.1 跟 v5 互相独立?**:
v5.1 只改 hmi_revert_current_set_page 函数, 跟 v5 加的 6 LED 在 LEDA_Init / LEDA_Update 是不同函数, 互相不耦合 ✓

### 5.3 整体

**H. 烧录可行性**:
屏幕板 v5.1 bin (MD5 `1ab1e527...`) 直接烧, 主板 v4.3 不动。如出问题, 回滚到 v1 屏幕板 (`112f4c03...`) 即可, 主板永远不需要动。

---

## 6. 测试矩阵 (Codex Pass 后)

### v5 (6 LED) 回归
- [ ] 主板设温度高 5°C → PB11 LED 亮 (PTC ON)
- [ ] 设温度恢复 → PB11 灭
- [ ] iPad 调湿度高 → PA11 亮
- [ ] iPad 长按 KEY5 开放供氧 → **PA12 + PA1 + LEDA5 同时亮** (O2 阀状态 3 处指示)
- [ ] 0x83 启动雾化 60s → PC1 亮
- [ ] 0x83 启动消毒 60s → PC14 亮; 60s 后 PC14 灭
- [ ] LEDA1-8 / LED8/9/10 / 数字显示 / KEY 全部回归正常

### v5.1 (撤销) 测试
- [ ] 上电默认温控关. 进 SET_TEMP 旋到 26 → 主板 enable=1. 长按 (5s 内) → enable=0 + target 回 25 + 回 LIVE ✓ (原本就 OK)
- [ ] 调温到 26, 等加热运行. 再进 SET_TEMP 改 28 → enable 仍 1, target=28. 长按 → **enable=0 + target 回 26 + 回 LIVE** (这是新行为!)
- [ ] 湿控同步: 改完湿度长按 → enable_humid_ctrl 也清 0
- [ ] O2 控制同步

### v4 / v4.3 修复回归
- [ ] T3 PE1 不闪
- [ ] T4/T8 雾化消毒分钟显示对
- [ ] KEY5 长按重新供氧从 0 起步
- [ ] KEY1 5 档循环, 第 4 档 PB1+PC5 双亮

---

## 7. 回应格式

```
[整体] Pass / Concern / Block

5.1 v5:
  A. 引脚冲突: ...
  B. CRL/CRH: ...
  C. PC14: ...
  D. active-low: ...

5.2 v5.1:
  E. 撤销语义: ...
  F. 命令顺序 race: ...
  G. 独立性: ...

5.3 整体:
  H. 烧录可行性: ...

[建议下一步烧录 v5.1 屏幕板 bin]: Y / N
```

Pass 后, 用户直接烧 `screen_stage8_redo_v5_1_FINAL.bin`, 只烧屏幕板, 主板 v4.3 不动。
