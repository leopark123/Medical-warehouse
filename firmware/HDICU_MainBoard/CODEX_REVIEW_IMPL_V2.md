# Codex 审查任务：6项GPIO驱动实现代码审查

## 背景

基于硬件工程师确认(2026-04-16)和第一轮Codex审查建议，已完成6项GPIO驱动代码。
请审查代码改动的正确性、完整性和安全性。

## 改动文件清单

1. `App/main_app.c` — GPIO初始化（PE7/PE10-13/PB5/PB12）
2. `App/tasks/tasks.c` — ControlTask中6项GPIO驱动逻辑
3. `BSP/bsp_config.h` — 新增BSP_LIGHT_LED1-4/BSP_MAGNET/BSP_GY/BSP_COMPRESSOR_LED定义
4. `firmware/HDICU_ScreenBoard/main.c` — 屏幕板GRID10/11/12运行指示灯

## 第一轮审查要求（已执行的修正）

| 任务 | 第一轮审查要求 | 是否执行 |
|------|-------------|---------|
| 2 电磁铁 | 必须用`control.switch_status`不能用`setpoint.inner_cycle` | 需确认 |
| 3 新风PTC | 不能改`fan_speed`逻辑，需单独合成PTC fan duty | 需确认 |
| 5 制氧机信号 | 必须用`relay_status & O2_IO`不能用`o2_state` | 需确认 |

---

## 审查项1：GPIO初始化 (main_app.c)

**位置**：`App/main_app.c` 的 `app_init()` 函数，`2b` GPIO初始化块之后

**已添加代码**：
```c
/* 2c. Additional GPIO outputs (硬件工程师确认 2026-04-16) */
__HAL_RCC_GPIOE_CLK_ENABLE();
__HAL_RCC_GPIOB_CLK_ENABLE();

gpio.Mode = GPIO_MODE_OUTPUT_PP;
gpio.Pull = GPIO_NOPULL;
gpio.Speed = GPIO_SPEED_FREQ_LOW;

/* PE10-PE13: U32照明灯4色 + PE7: 推拉电磁铁 */
gpio.Pin = BSP_LIGHT_LED1_PIN | BSP_LIGHT_LED2_PIN |
           BSP_LIGHT_LED3_PIN | BSP_LIGHT_LED4_PIN |
           BSP_MAGNET_PIN;
HAL_GPIO_Init(GPIOE, &gpio);
HAL_GPIO_WritePin(GPIOE, ..., GPIO_PIN_RESET);

/* PB5: GY-IO + PB12: 压缩机指示灯 */
gpio.Pin = BSP_GY_PIN | BSP_COMPRESSOR_LED_PIN;
HAL_GPIO_Init(GPIOB, &gpio);
HAL_GPIO_WritePin(GPIOB, ..., GPIO_PIN_RESET);
```

**请检查**：
1. PE7/PE10-PE13/PB5/PB12 是否与已有GPIO初始化冲突（relay_driver_init中的GPIOE引脚、GPIOB引脚）
2. 初始化顺序：此代码在`relay_driver_init()`之后执行，是否有时序问题
3. 初始状态全部RESET(低电平)是否正确：照明灯灭、电磁铁=外循环、指示灯灭、GY信号低

---

## 审查项2：ControlTask照明灯驱动 (tasks.c)

**位置**：`App/tasks/tasks.c` ControlTask，在`relay_driver_apply()`和`pwm_set_fan_speed()`之后

**已添加代码**：
```c
/* Task1: U32照明灯4色 — light_ctrl bit0-3 → PE10-PE13, 高电平亮 */
HAL_GPIO_WritePin(BSP_LIGHT_LED1_PORT, BSP_LIGHT_LED1_PIN,
                  (d->control.light_status & (1U << 0)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
HAL_GPIO_WritePin(BSP_LIGHT_LED2_PORT, BSP_LIGHT_LED2_PIN,
                  (d->control.light_status & (1U << 1)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
HAL_GPIO_WritePin(BSP_LIGHT_LED3_PORT, BSP_LIGHT_LED3_PIN,
                  (d->control.light_status & (1U << 2)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
HAL_GPIO_WritePin(BSP_LIGHT_LED4_PORT, BSP_LIGHT_LED4_PIN,
                  (d->control.light_status & (1U << 3)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
```

**请检查**：
1. `light_status`来源：`d->control.light_status = d->setpoint.light_ctrl`（第117行），这是在interlock_apply之前还是之后？如果互锁可能修改light_ctrl，需要确认
2. bit映射 `bit0→PE10, bit1→PE11, bit2→PE12, bit3→PE13` 是否与 `app_data.h` 中 `bit0=检查, bit1=照明, bit2=蓝, bit3=红` 的定义一致
3. 高电平=GPIO_PIN_SET=灯亮，是否与硬件工程师确认的"高电平亮"一致

---

## 审查项3：ControlTask电磁铁驱动 (tasks.c)

**已添加代码**：
```c
/* Task2: 内/外循环电磁铁 — 基于互锁后的switch_status */
HAL_GPIO_WritePin(BSP_MAGNET_PORT, BSP_MAGNET_PIN,
                  (d->control.switch_status & SW_BIT_INNER_CYCLE) ?
                  GPIO_PIN_SET : GPIO_PIN_RESET);
```

**请检查**：
1. **关键**：是否确实使用了`control.switch_status`而非`setpoint.inner_cycle`
2. `SW_BIT_INNER_CYCLE = 0x01` — 定义在 `app_data.h:105`
3. `control.switch_status`由`interlock.c`的`sync_switch_status()`从setpoint同步后被`interlock_apply()`修改。确认此驱动代码在`interlock_apply()`之后执行
4. 开放供氧时互锁会清`SW_BIT_INNER_CYCLE`（强制外循环），此时PE7应该LOW。是否正确？
5. 默认inner_cycle=0 → PE7=LOW → 外循环。上电默认合理性

---

## 审查项4：ControlTask压缩机指示灯 (tasks.c)

**已添加代码**：
```c
/* Task4: 压缩机指示灯 — 跟随relay_status中YASUO_IO位 */
HAL_GPIO_WritePin(BSP_COMPRESSOR_LED_PORT, BSP_COMPRESSOR_LED_PIN,
                  (d->control.relay_status & (1U << BSP_RELAY_YASUO_IO)) ?
                  GPIO_PIN_SET : GPIO_PIN_RESET);
```

**请检查**：
1. `BSP_RELAY_YASUO_IO = 7`，对应PE2压缩机继电器
2. 此代码在`relay_driver_apply()`之后，所以relay_status已经是最终值
3. 是否应该跟随relay_status还是跟随实际GPIO读回？（系统无继电器反馈，bitmap已是最终状态）

---

## 审查项5：ControlTask制氧机信号 (tasks.c)

**已添加代码**：
```c
/* Task5: 制氧机信号 — 跟随relay_status中O2_IO位(互锁后的实际状态) */
HAL_GPIO_WritePin(BSP_GY_PORT, BSP_GY_PIN,
                  (d->control.relay_status & (1U << BSP_RELAY_O2_IO)) ?
                  GPIO_PIN_SET : GPIO_PIN_RESET);
```

**请检查**：
1. **关键**：是否确实使用了`relay_status & O2_IO`而非`o2_state`
2. `BSP_RELAY_O2_IO = 4`，对应PB7 O2电磁阀
3. 当O2阀被互锁关闭时，relay_status中O2位也会被清，所以GY-IO也跟着低。确认互锁链路
4. 此代码在`interlock_apply()`和`relay_driver_apply()`之后，时序正确

---

## 审查项6：新风PTC风机调速 (tasks.c)

**已添加代码**：
```c
/* Task3: 新风净化 → PTC风机全速通风 */
if (d->control.switch_status & SW_BIT_FRESH_AIR) {
    pwm_set_fan2_duty(100);
}
```

**请检查**：
1. **关键**：是否使用了`control.switch_status`而非`setpoint.fresh_air`
2. `SW_BIT_FRESH_AIR = 0x02`
3. 此代码放在所有GPIO驱动之后、`vTaskDelay`之前
4. **时序问题**：`temp_control_update()`在ControlTask循环开头调用，会设置`pwm_set_fan2_duty(safety_duty)`。然后互锁和其他逻辑执行。最后这行代码覆盖为100%。**最后调用者胜出**——当fresh_air=1时，duty恒为100%。当fresh_air=0时，此行不执行，temp_control的设置保留。这个覆盖逻辑是否安全？
5. **安全场景**：如果同时加热(PTC ON, safety fan duty=70%)+新风(fresh_air=1)，最终duty=100%（>70%安全值）。是否安全？更高的风速应该更安全（更好的散热），所以取max是合理的
6. **边界场景**：开放供氧时互锁强制fresh_air=1 → PTC风机100%。是否与开放供氧的"停用加热"互锁兼容？（加热被互锁关闭，但PTC风机还在跑100%用于通风，这应该是可接受的）

---

## 审查项7：屏幕板运行指示灯 (screen main.c)

**位置**：`firmware/HDICU_ScreenBoard/main.c` 的 `update_display_from_data()` 函数

**已添加代码**：
```c
/* Relay bitmap bits: 0=PTC, 1=JIARE, 3=ZIY, 4=O2, 7=YASUO, 8=WH */
uint8_t fog_on  = (relay_status & (1U << 8)) ? 1 : 0;  /* WH 雾化 */
uint8_t uv_on   = (relay_status & (1U << 3)) ? 1 : 0;  /* ZIY 紫外 */
uint8_t o2_on   = (relay_status & (1U << 4)) ? 1 : 0;  /* O2 供氧 */
uint8_t cool_on = (relay_status & (1U << 7)) ? 1 : 0;  /* YASUO 压缩机 */
uint8_t heat_on = (relay_status & ((1U << 0) | (1U << 1))) ? 1 : 0; /* PTC+底热 */

s_u1_buf[10] = (alarm_flags != 0) ? 0xFF : 0x00;   /* 报警 */
s_u1_buf[11] = (fog_on || uv_on) ? 0xFF : 0x00;    /* 治疗 */
s_u1_buf[12] = (o2_on || cool_on || heat_on) ? 0xFF : 0x00; /* 温控/供氧 */
```

**请检查**：
1. relay_status字节序：`d[16]<<8 | d[17]`，主板发送端是否大端？与screen_protocol.c的`send_display_data()`对照
2. 继电器位编号是否与`bsp_config.h`的`BSP_RELAY_*`定义一致
3. `(1U << 8)` 对 `uint16_t relay_status` 是否正确？WH-IO是relay[8]，bit8在16位范围内
4. 加湿器(bit5 JIASHI)是否也应该纳入某个指示灯？当前只有PTC+JIARE(加热)、YASUO(制冷)、O2、WH(雾化)、ZIY(UV)
5. GRID10/11/12在TM1640 U1上的物理位置——这些GRID对应面板上哪些LED？如果不确定，至少确认它们不会覆盖已有的显示数据
6. HMI编辑模式(`s_u1_buf[11] = 0xFF`在编辑模式overlay中)是否会与GRID11运行指示冲突

---

## 交叉检查

### 1. GPIO引脚冲突矩阵
请确认以下引脚无重复初始化或冲突：

| GPIO | 原有用途 | 新增用途 | 冲突？ |
|------|---------|---------|--------|
| PE7 | 无 | MAGNET电磁铁 | 无 |
| PE10 | 无 | LED1照明 | 无 |
| PE11 | 无 | LED2照明 | 无 |
| PE12 | 无 | LED3照明 | 无 |
| PE13 | 无 | LED4照明 | 无 |
| PB5 | 无 | GY-IO制氧机 | 与relay_driver的PB4(WH)相邻，确认不冲突 |
| PB12 | 无 | 压缩机LED | 与PB13(KEY1-IO检测输入)相邻，确认不冲突 |

### 2. ControlTask执行顺序
```
temp_control_update(d)        ← 设置PTC fan duty(safety)
humidity_control_update(d)
oxygen_control_update(d)
interlock_apply(d)            ← 修改switch_status和relay_status
light_status = light_ctrl     ← 复制灯状态
nursing level LEDs            ← 已有
relay_driver_apply()          ← 输出继电器
pwm_set_fan_speed()           ← 输出fan1/fan3

─── 新增代码从这里开始 ───
Task1: PE10-13照明灯         ← 基于light_status
Task2: PE7电磁铁              ← 基于switch_status（互锁后）
Task4: PB12压缩机LED          ← 基于relay_status（互锁后）
Task5: PB5制氧机信号          ← 基于relay_status（互锁后）
Task3: PTC fan fresh_air覆盖  ← 基于switch_status（互锁后），覆盖temp_control的duty
```

**问题**：Task1的`light_status`赋值（第117行）在`interlock_apply()`之前。如果互锁需要修改照明状态，当前不会生效。是否需要移到interlock之后？

### 3. bsp_config.h定义审查
请确认新增的GPIO_PIN定义与原理图确认的引脚一致：
```c
BSP_LIGHT_LED1_PIN = GPIO_PIN_10  // PE10
BSP_LIGHT_LED2_PIN = GPIO_PIN_11  // PE11
BSP_LIGHT_LED3_PIN = GPIO_PIN_12  // PE12
BSP_LIGHT_LED4_PIN = GPIO_PIN_13  // PE13
BSP_MAGNET_PIN     = GPIO_PIN_7   // PE7
BSP_GY_PIN         = GPIO_PIN_5   // PB5
BSP_COMPRESSOR_LED_PIN = GPIO_PIN_12 // PB12
```

### 4. 遗留问题
- PB9(RED-IO)和PB6(ZY-IO)确认空着不用，但relay_driver仍然初始化PB9为输出。是否需要确保relay_status bit2永远为0？
- `app_data_init()`中未对新增GPIO的对应setpoint/state做显式初始化（依赖memset 0），是否足够？

---

## 期望输出

1. 对每项改动给出"通过/不通过/需修改"判断
2. 特别关注：Task2/5是否用了正确的数据源（switch_status/relay_status）
3. 特别关注：Task3的PTC fan覆盖逻辑是否安全
4. 特别关注：Task1的light_status赋值时序是否需要移到interlock之后
5. 屏幕板GRID10/11/12的relay_status位编号是否正确
6. 指出任何遗漏或潜在风险
