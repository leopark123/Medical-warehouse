# HDICU-ZKB01A 屏幕板/主板功能改动确认书 v1.2

**项目**：HDICU-ZKB01A 医疗仓控制系统
**日期**：2026-04-30
**版本**：v1.2（含源码证据，工程严谨版）
**作用范围**：屏幕板（GD32F303RCT6）+ 主控板（STM32F103VET6）
**前置版本**：v1.0（仅功能描述，已交客户）

---

## 一、文档目的

本文档列出本次 6 项改动的**功能定义 + 协议设计 + 源码证据**，给客户技术方/审核人员核对，确保实施前所有决策有源可溯。

每项改动包括：
1. **现状证据**（指向源码具体行）
2. **目标行为**
3. **改动要点**（不含具体代码，描述算法/逻辑）
4. **影响范围**

---

## 二、决策摘要

| # | 项 | 决策 | 客户确认 |
|---|---|------|---------|
| 1 | CN11 主串口路径 | A：CN11+CN12 双发 | ☐ 同意 ☐ 不同意 |
| 2 | LEDA7 语义 | A：保持新风按键状态指示（不动） | ☐ 同意 ☐ 不同意 |
| 3 | CN3 / CN8 重映射 | CN3 恢复照明灯 + CN8 改风速档位键 | ☐ 同意 ☐ 不同意 |
| 4 | 风速档位 | B：真 PWM 三档（0/30/60/100%）+ 三方仲裁 | ☐ 同意 ☐ 不同意 |
| 5 | 加热安全优先级 | 加热中风扇强制最低 80%，覆盖用户档位 | ☐ 接受 ☐ 不接受 |
| 6 | PE5/PC13 处理 | X：fan_speed 不再驱动 PE5/PC13（解绑） | ☐ 同意 ☐ 不同意 |
| 7 | LED8/LED9/LED10 三档指示 | 启用 PA4/PB9/PD2 三路 LED 显示风速档 | ☐ 同意 ☐ 不同意 |
| 8 | HMI 编码器扩展 | 7 项可调（含真写入主板） | ☐ 同意 ☐ 不同意 |
| 9 | 第 7 项"清供氧累计" | A：编码器旋到该页 + 按键 = 清零（复用现有 0x83）| ☐ 同意 ☐ 不同意 |
| 10 | 闪烁提示 | 仅当前编辑项数码管位 全灭 0.5s ↔ 全亮 0.5s | ☐ 同意 ☐ 不同意 |

---

## 三、屏幕板编码器写入机制说明（重要）

> **本节澄清屏幕板编码器对主板的影响范围 — 不绕过控制层，不直接驱动硬件，所有安全互锁规则继续生效。**

### 3.1 三类操作语义

HMI 7 项中按"对主板的写入性质"分为三类：

| 类型 | 项 | 写入对象 | 主板控制层是否介入 |
|------|---|---------|-----------------|
| **A 纯 setpoint** | 温度、湿度、O2、CO2 阈值（共 4 项）| `setpoint.target_xxx` 字段 | ✅ ControlTask 周期读 setpoint，按状态机+互锁慢慢逼近 |
| **B setpoint + 动作** | 雾化时长、消毒时长（共 2 项）| `control.fog_remaining` / `disinfect_remaining` 倒计时启动 | ✅ 触发前调 `interlock_can_start_fogging()` 等互锁检查 |
| **C 纯动作** | 清供氧累计（1 项）| `control.o2_accumulated = 0` | 直接清零（无控制状态影响）|

### 3.2 控制流图

```
       ┌───────────────────────┐
       │   屏幕板编码器输入    │
       └───────────┬───────────┘
                   │
   ┌───────────────┼───────────────┐
   ▼               ▼               ▼
 纯 setpoint   setpoint+动作    纯动作
 温/湿/O2/CO2  雾化/消毒        清供氧
   │               │               │
   ▼               ▼               ▼
 屏幕→主板     屏幕→主板       屏幕→主板
 0x81 ParamID  0x83 TimerType  0x83 TimerType=3
 (0x01-0x06)   (0x01/0x02)     Cmd=3
   │               │               │
   ▼               ▼               ▼
 主板写         主板调           主板清零
 setpoint       control_timers   o2_accumulated
                _start_xxx()
                ↓
                检查互锁
                interlock 规则
                  (3 条相关:
                   雾化↔紫外互斥
                   开放供氧↔加热互斥
                   开放供氧↔紫外互斥)
   │               │               │
   ▼               ▼               ▼
 ControlTask     可能拒绝         立即生效
 周期评估        启动             (无规则约束)
   │               │
   ▼               ▼
 状态机决定      启动倒计时
 是否驱动        + 开继电器
 继电器/PWM
```

### 3.3 关键安全保证

| 保证 | 实现 |
|------|------|
| 屏幕板**不能直接驱动**任何继电器/GPIO | 所有改动都通过协议消息走主板控制层 |
| 用户调温度 setpoint 不会立即开继电器 | `temp_control_update()` 周期判断 (HEATING/COOLING/IDLE)，含 ±1°C 滞环 |
| 雾化启动时被紫外占用 | `interlock_can_start_fogging()` 直接拒绝启动（return）|
| 加热中即使用户调风速档位也保持 ≥80% | 改动 4 的三方 max 仲裁规则 |
| 校准值/出厂限值改动 | 编码器**不暴露**校准/限值调节，仅 iPad APP 走 0x03/0x09 协议可调 |

### 3.4 与 iPad 协议的一致性

| 触发源 | 写 setpoint | 启动 timer | 清供氧累计 |
|--------|------------|-----------|-----------|
| iPad APP（0x03 / 0x09 / 0x0B）| ✅ | ✅（通过 fog_time/disinfect_time）| ❌（仅 0x0B 恢复出厂会清，但还会重置其他）|
| 屏幕板编码器（本次改动后）| ✅（0x81）| ✅（0x83）| ✅（0x83 Cmd=3）|

**两条路径对主板控制层的影响完全等价**，没有"屏幕板有特权"或"iPad 有特权"。

---

## 四、改动详细描述（含源码证据）

### 改动 1 — CN11 主串口路径恢复（A 双发）

#### 现状证据

| 项 | 文件:行 | 引用内容 |
|----|--------|---------|
| UART1 已初始化 (CN11=PA9/PA10) | `firmware/HDICU_ScreenBoard/main.c:268` | UART1_Init 函数完整配置 TX+RX+RXNEIE |
| `uart1_send` 函数已实现但被标 unused | `firmware/HDICU_ScreenBoard/main.c:312` | `static void __attribute__((unused)) uart1_send(...)` |
| 实际所有发送只走 UART2/CN12 | `firmware/HDICU_ScreenBoard/main.c:327` | `uart_primary_send` 内部仅调 `uart2_send` |
| 主板屏幕通信口 = USART1 | `firmware/HDICU_MainBoard/Drivers/uart/uart_driver.c:33` | `[UART_CH_SCREEN] = { BSP_UART_SCREEN, ...}` |

#### 目标行为

- 屏幕板 `uart_primary_send` 同时发送到 UART1 (CN11→主板) 和 UART2 (CN12→调试 PC)
- 主板从 CN11 恢复接收（前提：CN11 电平转换器硬件已修复）
- CN12 保留为调试备份通道

#### 影响

- 屏幕板每条消息 TX 时间 × 2（实际可忽略，115200 下每帧 < 5ms）
- 主板侧无任何改动（仍用 UART_CH_SCREEN 接收）

---

### 改动 2 — LEDA7 保持新风状态指示（A，不动）

#### 现状证据

| 项 | 文件:行 | 引用内容 |
|----|--------|---------|
| LEDA7 当前驱动 = `switch_status & SW_BIT_FRESH_AIR` | `firmware/HDICU_ScreenBoard/main.c:931-933` | `if (switch_st & (1 << 1)) GPIOC_BSRR = (1 << (15 + 16));` |
| SW_BIT_FRESH_AIR = 新风状态位 | `firmware/HDICU_MainBoard/App/data/app_data.h:108` | `#define SW_BIT_FRESH_AIR  0x02` |

#### 目标行为

**与现状一致**。CN6 新风按键按下 → setpoint.fresh_air 翻转 → ControlTask 设 switch_status bit1 → 屏幕板下次 0x01 包收到 → LEDA7 (PC15) 反映状态。

#### 影响

零代码改动。

---

### 改动 3 — CN3 物理按键恢复 + CN8 改风速键

#### 现状证据

| 项 | 文件:行 | 引用内容 |
|----|--------|---------|
| KEY_ID_MAP[1] CN3 已是 0x02 | `firmware/HDICU_ScreenBoard/main.c:1218` | `0x02, /* [1] CN3/PB13: 照明灯 (CN3断路, 由CN8替代) */` |
| KEY_ID_MAP[7] CN8 当前也是 0x02 | `firmware/HDICU_ScreenBoard/main.c:1224` | `0x02, /* [7] CN8/PC9: 照明灯 (替代断路CN3) */` |
| 主板 0x82 case 0x02 = 翻转照明灯 bit1 | `firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c:192-194` | `d->setpoint.light_ctrl ^= 0x02;` |
| 主板 0x82 当前**没有** case 0x0B | `firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c:188-243` | switch 仅处理 0x01-0x0A |

#### 目标行为

- CN3 修复后按下 → 切换照明灯（**软件已正确**，硬件修后即生效）
- CN8 改 KEY_ID_MAP[7] = `0x0B` （新键 ID = 风速档位）
- 主板 0x82 新增 case 0x0B：`fan_speed = (fan_speed + 1) % 4`，循环 0→1→2→3→0

#### 影响

- 屏幕板：1 行修改
- 主板：1 个 case 新增（约 5 行）

---

### 改动 4 — 风速档位真 PWM 三档（B 方案）+ X 解绑 PE5/PC13

#### 现状证据

| 项 | 文件:行 | 引用内容 |
|----|--------|---------|
| 4 路风机/风扇硬件分配 | `firmware/HDICU_MainBoard/BSP/bsp_config.h:91-102` | PE5=内循环, PE6=PTC使能, PC13=空调内, PE9=PTC调速 |
| PWM 4 档定义已存在 | `firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.c:21-26` | `s_speed_duty[] = {0, 30, 60, 100}` |
| 当前 pwm_set_fan_speed 驱动 PE5+PC13（不驱动 PTC）| `firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.c:116-123` | `pwm_set_fan1_duty + pwm_set_fan3_duty` |
| PTC 风机由 temp_control 独立调用 | `firmware/HDICU_MainBoard/Control/temp/temp_control.c:84` | `pwm_set_fan2_duty(PTC_FAN_DUTY_PERCENT) /* SAFETY CRITICAL */` |
| 新风强制覆盖 PTC 100% | `firmware/HDICU_MainBoard/App/tasks/tasks.c:249-251` | `if (FRESH_AIR) pwm_set_fan2_duty(100);` |

#### 目标行为

##### 4.1 真三档 PWM

- `fan_speed = 0` → PTC 风机关（PE6 LOW + PE9 PWM 0%）
- `fan_speed = 1` → PTC 风机 30%（PE6 HIGH + PE9 PWM 30%）
- `fan_speed = 2` → PTC 风机 60%
- `fan_speed = 3` → PTC 风机 100%

##### 4.2 三方 max 仲裁规则

PTC 风机最终 duty = **三方期望取最大值**：
```
duty = max(
    user_fan_duty,          // 0/30/60/100 (来自 fan_speed)
    fresh_active ? 100 : 0,  // 新风强制 100%
    heating ? 80 : 0         // 加热安全最低 80%
)
```

##### 4.3 全场景验证矩阵（**实测必须每条通过**）

| 加热 | 新风 | fan_speed | 期望 PTC duty | 物理意义 |
|:----:|:----:|:---------:|:-------------:|---------|
| ❌ | ❌ | 0 | **0%** | 关 |
| ❌ | ❌ | 1 (30) | **30%** | 用户档位 |
| ❌ | ❌ | 2 (60) | **60%** | 用户档位 |
| ❌ | ❌ | 3 (100) | **100%** | 用户档位 |
| ❌ | ✅ | 任意 | **100%** | 新风强制 |
| ✅ | ❌ | 0 | **80%** | 加热安全最低 |
| ✅ | ❌ | 1 (30) | **80%** | 安全 > 用户 |
| ✅ | ❌ | 2 (60) | **80%** | 安全 > 用户 |
| ✅ | ❌ | 3 (100) | **100%** | 用户 ≥ 安全 |
| ✅ | ✅ | 0 | **100%** | 新风 ≥ 加热（**修正点**）|

##### 4.4 X 方案：PE5/PC13 与 fan_speed 解绑

- `pwm_set_fan_speed` 不再调用 `pwm_set_fan1_duty (PE5)` 和 `pwm_set_fan3_duty (PC13)`
- PE5、PC13 由其他逻辑独立管理（保持现状，本次不动）

#### 影响

- pwm_driver.c：新增 `pwm_set_ptc_arbiter()` 函数 + 改写 `pwm_set_fan_speed()`
- temp_control.c：不再直接调 `pwm_set_fan2_duty(80)`，改为"申请 safety_min = 80"
- tasks.c：删除直接调 `pwm_set_fan2_duty(100)` for fresh_air；改为统一调 `pwm_set_ptc_arbiter()`

**安全等级**：⚠️ 临床安全相关（PTC 风机 + 加热）。**Stage 3 必须 10 种场景全实测**才能上线。

---

### 改动 5 — LED8/LED9/LED10 三档显示

#### 现状证据

| 项 | 文件:行 | 引用内容 |
|----|--------|---------|
| 当前 LEDA1-8 = PA5/PC4/PC5/PB10/PC13/PA0/PC15/PA15 | `firmware/HDICU_ScreenBoard/main.c:849-893` | LEDA_Init 配置 |
| **GPIOD 寄存器**未在 main.c 定义 | `firmware/HDICU_ScreenBoard/main.c:41-66` (寄存器宏定义区) | 仅有 GPIOA/B/C，无 GPIOD |
| **GPIOD 时钟**未在任何 init 启用 | `firmware/HDICU_ScreenBoard/main.c` | grep 全文无 `RCC_APB2ENR.*5` for GPIOD |
| PA4/PB9/PD2 当前未占用 | grep 全文 | 0 处使用 |

#### 目标行为

**显示语义：B 档位累加**（已确认）

| fan_speed | LED8 (PA4) | LED9 (PB9) | LED10 (PD2) | 视觉效果 |
|:---------:|:----------:|:----------:|:-----------:|---------|
| 0 | 灭 | 灭 | 灭 | 全灭 |
| 1 | **亮** | 灭 | 灭 | 1 颗（最弱）|
| 2 | **亮** | **亮** | 灭 | 2 颗（中等）|
| 3 | **亮** | **亮** | **亮** | 3 颗（最强）|

类似条形图/音量条，档位越高亮越多颗，符合直觉。

驱动逻辑（伪代码）：
```
LED8  on if fan_speed >= 1
LED9  on if fan_speed >= 2
LED10 on if fan_speed >= 3
```

#### 影响

- 屏幕板：新增 GPIOD 寄存器宏 + 启用 GPIOD 时钟 + 新增 LED_Fan_Init() + LEDA_Update 增强

**前置条件**：PA4/PB9/PD2 真实接入了 LED8/9/10（已由你确认原理图证据）。

---

### 改动 6 — HMI 编码器扩展 7 项 + 闪烁 + 真写入主板

#### 现状证据

| 项 | 文件:行 | 引用内容 |
|----|--------|---------|
| 当前 HMI 4 页（LIVE/温/湿/O2）| `firmware/HDICU_ScreenBoard/main.c:142-147` | HmiPage_t 枚举 |
| 0x81 ParamID 已支持 0x01-0x05 | `firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c:154-167` | 温/湿/O2/风速/护理 |
| 0x83 已支持雾化/消毒/供氧累计 | `firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c:265-297` | TimerType 0x01/0x02/0x03 |
| 0x81 当前**无** CO2 ParamID | `firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c:159-166` | switch 仅 0x01-0x05 |
| 当前闪烁逻辑 = 实测↔设定交替 | `firmware/HDICU_ScreenBoard/main.c:759` | `if (page!=LIVE && tick&500) {显示设定}` |

#### 目标行为

##### 6.1 编码器循环 8 个页面

| 页 | 项 | 范围 | 步长 | 协议路径 |
|----|---|------|------|---------|
| 0 | LIVE 实时显示 | — | — | — |
| 1 | 温度 | 10.0~40.0°C | 0.1°C | 0x81 ParamID 0x01 ✅ 已有 |
| 2 | 湿度 | 30~90% | 1% | 0x81 ParamID 0x02 ✅ 已有 |
| 3 | O2 | 21~100% | 1% | 0x81 ParamID 0x03 ✅ 已有 |
| 4 | **CO2 阈值** | 0~5000 ppm | 100 ppm | **0x81 ParamID 0x06**（新增主板 case） |
| 5 | **雾化时长** | 0~3600 秒 | 60 秒 | **0x83 TimerType=0x01 Cmd=0x01 Duration** ✅ 已有 |
| 6 | **消毒时长** | 0~3600 秒 | 60 秒 | **0x83 TimerType=0x02 Cmd=0x01 Duration** ✅ 已有 |
| 7 | **清供氧累计** | 触发动作 | — | **0x83 TimerType=0x03 Cmd=0x03** ✅ 已有 |

**重要**：6 个项目复用现有协议（无主板改动），仅 CO2 一项需主板新增 1 个 case。

##### 6.2 闪烁逻辑

- 仅当前编辑页对应的 GRID 数码管位
- 0.5 秒全灭 → 0.5 秒显示 setpoint → 循环
- 其他位（实测温/湿/O2/CO2/时长等）正常显示，不受闪烁影响

##### 6.3 第 7 页"清供氧"操作

- 旋编码器到第 7 页 → 屏幕显示当前 o2_accumulated（HH:MM:SS）+ 闪烁
- 按编码器键（单击）→ 屏幕板发 `0x83 TimerType=3 Cmd=3` → 主板 `control_timers_reset_o2_accum()` 清零 → 自动回到 LIVE 页

#### 影响

| 模块 | 改动 |
|------|------|
| 屏幕板 main.c | HMI 页面扩展 + 4 个 shadow + 闪烁逻辑改写 + 协议发送 + GRID 映射表 |
| 主板 screen_protocol.c | 0x81 加 1 个 case 0x06 处理 CO2 |

---

## 五、协议向后兼容性

| 接口 | 改动 | 兼容性 |
|------|------|------|
| iPad ↔ 主板（v2.1）| 主板 0x81 新增 case 0x06（实际 iPad 不发 0x81）| ✅ 完全兼容，APP 不需改 |
| 屏幕 → 主板 0x81 | +1 个 ParamID | ✅ 旧屏幕板不发 0x06 也正常 |
| 屏幕 → 主板 0x82 | +1 个 key_id 0x0B | ✅ 旧屏幕板不发 0x0B 也正常 |
| 屏幕 → 主板 0x83 | 不改，复用 | ✅ |
| 主板 → 屏幕 0x01 | 不改 | ✅ |

**结论**：所有改动**单向新增**，不破坏既有功能。新旧固件可共存（屏幕板新固件 + 主板旧固件 = 屏幕板新增功能不可用，但旧功能正常）。

---

## 六、安全与回滚

### 备份策略

- 改动前对当前固件做完整备份（已存 `firmware/HDICU_MainBoard/backups/`）
- 每个 Stage 完成后单独备份
- 任何阶段出现问题 → 30 秒内 JLink 烧回旧版

### 当前基线

| 模块 | 当前固件 | 备份位置 |
|------|---------|---------|
| 主板 | `firmware_20260428_123927_uart_fix_v2.bin` | `firmware/HDICU_MainBoard/backups/` |
| 屏幕板 | `screen_fw.bin` (commit 中) | `firmware/HDICU_ScreenBoard/build/` |

### 主板单元测试基线

`101 passed, 0 failed`（来自 Codex 报告，需在每次改动后保持）

---

## 七、实施顺序（4 阶段，严格分阶段验收）

| Stage | 范围 | 行数估算 | 关键验收 |
|-------|------|---------|---------|
| **1** | CN11 双发 + CN3/CN8 重映射 | ~8 | CN3 物理按 → 照明切换；CN8 物理按 → fan_speed 0→1→2→3→0 |
| **2** | LED8/9/10 + GPIOD 启用 | ~46 | 改 fan_speed → LED 对应亮 |
| **3** | PTC 三方 max 仲裁（**安全关键**）| ~58 | 10 种场景实测物理风机 |
| **4** | HMI 7 项 + 闪烁 + 协议复用 | ~162 | 编码器逐项调，JLink 验证 setpoint 写入 |

**总计** ≈ 274 行（屏幕板 + 主板）

每个 Stage 单独编译 → 烧录 → 实测 → 验收通过后再做下一个。

---

## 八、客户最终确认

### 同意确认的项目（请勾选）

- ☐ 改动 1 — CN11 双发（CN11 电平转换器已修复）
- ☐ 改动 2 — LEDA7 保持新风按键状态指示（不动）
- ☐ 改动 3 — CN3 恢复照明灯，CN8 改为风速档位键（key_id 0x0B 新增）
- ☐ 改动 4 — 风速 B 方案 + 三方 max 仲裁
- ☐ 接受**加热时风扇强制 ≥80%** 安全规则（即使用户设 fan_speed=1 30%）
- ☐ 改动 4-X — fan_speed 不再驱动 PE5/PC13（解绑）
- ☐ 改动 5 — LED8/LED9/LED10 = PA4/PB9/PD2，硬件已接入
- ☐ 改动 6.1 — HMI 编码器 8 页（含 7 项可调）
- ☐ 改动 6.2 — 仅当前编辑项数码管位 0.5 秒灭/亮闪烁
- ☐ 改动 6.3 — 第 7 页按编码器键 = 清供氧累计

### LED8/9/10 显示语义（已选 B）

- ☐ A. 档位独立指示：fan=1→LED8 亮，fan=2→LED9 亮，fan=3→LED10 亮（一次只亮一颗）
- ☑ **B. 档位累加显示**：fan=1→LED8 亮；fan=2→LED8+LED9；fan=3→LED8+LED9+LED10 ✅ **客户已确认**

### 已知不在本次改动范围（明示）

iPad APP 端 6 项 bug 待 APP 开发方处理：
1. cancel_flags 全填 0（导致温/湿/O2/CO2 setpoint 永不生效）
2. nursing_level / fog_time 写入时未读现状（覆盖物理按键）
3. 4 个开关按钮（内循环/新风/灯）未绑定到 0x03 协议
4. 出厂限值 0x09 写入未实现
5. 体征曲线 1 Hz 后台 polling 0x05 未实现
6. APP 应基于 0x01 响应组装 0x03（非默认值）

---

**客户/产品方签字**：______________________
**日期**：______________________
**联系方式**：______________________

---

## 附录 A：源码证据索引

所有引用按 `文件:行号` 格式。点击可定位（如使用 IDE 打开本文档）。

| 引用 | 来源 |
|------|------|
| KEY_ID_MAP | firmware/HDICU_ScreenBoard/main.c:1216-1227 |
| 0x82 单击 | firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c:186-243 |
| 0x81 ParamID | firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c:154-167 |
| 0x83 Timer | firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c:265-297 |
| LEDA_Update | firmware/HDICU_ScreenBoard/main.c:897-938 |
| LEDA_Init | firmware/HDICU_ScreenBoard/main.c:849-893 |
| pwm_set_fan_speed | firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.c:116-123 |
| s_speed_duty | firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.c:21-26 |
| pwm_set_fan2_duty (PTC) | firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.c:101-107 |
| temp_control 80% | firmware/HDICU_MainBoard/Control/temp/temp_control.c:84 |
| 新风 100% 覆盖 | firmware/HDICU_MainBoard/App/tasks/tasks.c:249-251 |
| BSP 风机引脚 | firmware/HDICU_MainBoard/BSP/bsp_config.h:91-102 |
| HmiPage_t | firmware/HDICU_ScreenBoard/main.c:142-147 |
| 闪烁现状 | firmware/HDICU_ScreenBoard/main.c:759 |
| uart_primary_send | firmware/HDICU_ScreenBoard/main.c:327 |
| uart1_send (unused) | firmware/HDICU_ScreenBoard/main.c:312 |
| UART1_Init | firmware/HDICU_ScreenBoard/main.c:268 |
| 主板 UART_CH_SCREEN | firmware/HDICU_MainBoard/Drivers/uart/uart_driver.c:33 |

---

**文档结束**
