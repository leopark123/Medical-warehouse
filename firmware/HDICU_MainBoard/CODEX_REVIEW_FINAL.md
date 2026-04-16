# Codex 最终审查：6项GPIO驱动实现 — 实测验证后

## 背景

6项GPIO驱动已编码、编译、烧录并通过万用表实测验证。本轮审查目的：
1. 确认代码与实测结果一致
2. 检查是否有遗漏的边界条件或安全隐患
3. 确认可以作为发布基线

## 实测验证结果 (2026-04-16)

| 任务 | GPIO | 测量位置 | 默认电压 | 触发后电压 | 驱动方式 | 结果 |
|------|------|---------|---------|----------|---------|------|
| 1-检查灯 | PE10 | U32 高低电平1 | 12V(灭) | ~0V(亮) | N-MOS+12V | ✅ |
| 1-照明灯 | PE11 | U32 高低电平2 | — | — | 同上 | ⚠️ CN3硬件故障跳过 |
| 1-红蓝光 | PE12+PE13 | U32 高低电平3+4 | 12V(灭) | ~0V(亮) | 同上 | ✅ |
| 2-电磁铁 | PE7 | U31 4针座 | 12V(外循环) | ~0V(内循环) | N-MOS+12V | ✅ |
| 4-压缩机灯 | PB12 | CN14 LED脚 | 0V(灭) | 3.3V(亮) | 直驱 | ✅ |
| 5-制氧机 | PB5 | MCU pin91 | 0V | 3.3V | 直驱 | ✅ |
| 6-屏幕指示 | GRID10/11/12 | TM1640 U1 | 灭 | 亮 | TM1640 | ✅ |

### 关键发现：MOSFET驱动电路的极性

U32照明灯和U31电磁铁都通过N-MOS(AO3404)驱动：
```
MCU GPIO(3.3V) → 限流电阻 → N-MOS栅极 → 导通 → 12V负载端≈0V → 电流流过 → 设备工作
MCU GPIO(0V)   → N-MOS关断 → 12V负载端=12V(上拉) → 无电流 → 设备停止
```

代码中 `GPIO_PIN_SET = HIGH = 3.3V` → MOSFET导通 → 设备工作。
万用表在连接器端量到的是**反相电压**（工作时≈0V，停止时=12V），但MCU输出方向正确。

**结论：代码极性正确，无需修改。**

## 请审查以下方面

### 1. ControlTask执行顺序完整性

当前ControlTask的完整执行流程：
```
temp_control_update(d)         ← PTC fan duty设置(safety)
humidity_control_update(d)
oxygen_control_update(d)
interlock_apply(d)             ← 修改switch_status/relay_status
light_status = light_ctrl      ← 灯状态复制
nursing_level_actual = nursing_level
fan_speed_actual = fan_speed
timer tick
nursing LEDs(PB0/PB1/PC5)
timer beep countdown
relay_driver_apply(bitmap)     ← 继电器输出
pwm_set_fan_speed(level)       ← fan1/fan3输出
─── 新增GPIO驱动 ───
PE10-PE13: light_status bit0-3  ← 照明灯
PE7: switch_status & INNER      ← 电磁铁
PB12: relay_status & YASUO      ← 压缩机LED
PB5: relay_status & O2          ← 制氧机信号
PTC fan: fresh_air → duty 100%  ← 新风PTC覆盖
─── vTaskDelay ───
```

**请确认**：
1. 新增GPIO驱动放在relay_driver_apply和pwm_set_fan_speed之后是否正确
2. light_status赋值在interlock_apply之前（第117行），而GPIO驱动在之后。当前互锁不修改light_ctrl，但如果未来互锁需要禁止某些灯（如紫外灯开时关蓝光），这个顺序会成为隐患。是否建议移到interlock之后？
3. fresh_air PTC覆盖放在最后一行（覆盖temp_control的duty），"最后调用者胜出"的策略是否清晰enough

### 2. GPIO初始化与relay_driver的隔离

新增GPIO在main_app.c的2c块初始化：
- PE7/PE10-PE13 在GPIOE（relay_driver也初始化了GPIOE的PE0-PE4）
- PB5/PB12 在GPIOB（relay_driver也初始化了GPIOB的PB4/PB7-PB9）

**请确认**：
1. HAL_GPIO_Init对同一PORT的不同PIN是否安全（不会影响已初始化的其他PIN）
2. 两次`__HAL_RCC_GPIOE_CLK_ENABLE()`调用是否有副作用（应该没有，幂等）

### 3. PB9和PB6空置处理

硬件确认PB9(RED-IO)和PB6(ZY-IO)空着不用：
- PB9在relay_driver中已初始化为输出，relay_status bit2永远为0（无业务逻辑置位）
- PB6未初始化（无BSP定义，无GPIO_Init调用）

**请确认**：
1. PB9 relay_status bit2是否有可能被意外置位（检查所有写relay_status的代码路径）
2. PB6悬空是否需要配置为输入下拉或输出低电平以避免漂浮

### 4. 屏幕板指示灯relay_status位编号

屏幕板代码使用硬编码的位编号：
```c
uint8_t fog_on  = (relay_status & (1U << 8)) ? 1 : 0;  /* WH */
uint8_t uv_on   = (relay_status & (1U << 3)) ? 1 : 0;  /* ZIY */
uint8_t o2_on   = (relay_status & (1U << 4)) ? 1 : 0;  /* O2 */
uint8_t cool_on = (relay_status & (1U << 7)) ? 1 : 0;  /* YASUO */
uint8_t heat_on = (relay_status & ((1U << 0) | (1U << 1))) ? 1 : 0;
```

**请与bsp_config.h交叉核对**：
```
BSP_RELAY_PTC_IO    = 0  → bit0 (加热)
BSP_RELAY_JIARE_IO  = 1  → bit1 (底热)
BSP_RELAY_RED_IO    = 2  → bit2 (空置)
BSP_RELAY_ZIY_IO    = 3  → bit3 (紫外) ✓
BSP_RELAY_O2_IO     = 4  → bit4 (O2)   ✓
BSP_RELAY_JIASHI_IO = 5  → bit5 (加湿)
BSP_RELAY_FENGJI_IO = 6  → bit6 (外风机)
BSP_RELAY_YASUO_IO  = 7  → bit7 (压缩机) ✓
BSP_RELAY_WH_IO     = 8  → bit8 (雾化)  ✓
```

1. 位编号是否全部正确
2. 加湿器(bit5)未被任何指示灯跟踪——是否应该加入GRID11或GRID12
3. 外风机(bit6)通常伴随压缩机(bit7)一起开，不单独指示是否合理

### 5. CN3硬件故障的软件影响

屏幕板CN3(KEY2照明灯, PB13)硬件故障：
- 按键无法触发
- PE11(U32高低电平2)无法通过正常按键验证

**请确认**：
1. CN3故障是否影响其他按键的扫描（Key_Scan轮询所有10个键，一个键故障不应阻塞其他键）
2. 如果PB13物理短路到GND，key_read_raw会持续返回"pressed"，可能触发长按事件。是否需要加保护
3. PE11的软件逻辑与PE10/PE12/PE13完全相同（同一段代码），硬件修复后应直接可用

### 6. 整体安全审查

1. **上电默认状态**：所有新增GPIO初始化为RESET(LOW)。照明灯灭、电磁铁=外循环、指示灯灭、GY信号低。是否全部合理？
2. **看门狗覆盖**：新增代码增加了约30行WritePin和1个条件分支。在200ms ControlTask周期内，这些操作耗时远小于4s IWDG超时。是否安全？
3. **app_data锁**：新增GPIO驱动读取`d->control.light_status/switch_status/relay_status`。这些读取在ControlTask内部，不需要额外加锁（单一写者模式）。是否正确？
4. **电源域隔离**：PE10-PE13和PE7通过MOSFET驱动12V负载，MCU端只输出3.3V到栅极。GPIO输出不会反向影响MCU。是否有其他电气风险？

## 文件清单

| 文件 | 改动类型 | 行数变化 |
|------|---------|---------|
| `BSP/bsp_config.h` | 新增定义 | +30行 |
| `App/main_app.c` | GPIO初始化 | +15行 |
| `App/tasks/tasks.c` | ControlTask驱动 | +25行 |
| `firmware/HDICU_ScreenBoard/main.c` | GRID指示灯 | +10行（替换3行） |

## 期望输出

1. 对每项给出"通过/需修改"
2. 特别关注：light_status赋值时序是否需要移到interlock之后
3. PB6悬空处理建议
4. 加湿器是否纳入指示灯
5. CN3故障下的软件保护建议
6. 整体安全判断：是否可以作为发布基线
