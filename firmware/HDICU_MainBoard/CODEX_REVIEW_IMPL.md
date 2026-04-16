# Codex 审查任务：6项硬件工程师确认后的实现

## 背景

医疗仓(新生儿培养箱)双MCU系统。硬件工程师已确认所有待定GPIO分配(2026-04-16)。
需要审查以下6项实现方案，确认代码改动的正确性、完整性和安全性。

## 已确认的硬件信息

### GPIO分配 (硬件工程师确认 2026-04-16)
- PE10=LED1, PE11=LED2, PE12=LED3, PE13=LED4 → U32照明灯4色，高电平亮低电平灭
- PE7=MAGNET-IO → 推拉电磁铁 = 内/外循环切换
- PE9(PWM)+PE6(使能) → PTC风机 = 新风净化可调速
- PB5=GY-IO → 制氧机工作时输出高电平
- PB12=LED01-IO → 压缩机指示灯（启动亮/关闭灭）
- PB9=RED-IO → 红外灯空着不用
- PB6=ZY-IO → 空着不用

### BSP定义 (已添加到 bsp_config.h)
```c
#define BSP_LIGHT_LED1_PORT   GPIOE   // PE10
#define BSP_LIGHT_LED1_PIN    GPIO_PIN_10
#define BSP_LIGHT_LED2_PORT   GPIOE   // PE11
#define BSP_LIGHT_LED2_PIN    GPIO_PIN_11
#define BSP_LIGHT_LED3_PORT   GPIOE   // PE12
#define BSP_LIGHT_LED3_PIN    GPIO_PIN_12
#define BSP_LIGHT_LED4_PORT   GPIOE   // PE13
#define BSP_LIGHT_LED4_PIN    GPIO_PIN_13
#define BSP_MAGNET_PORT       GPIOE   // PE7
#define BSP_MAGNET_PIN        GPIO_PIN_7
#define BSP_GY_PORT           GPIOB   // PB5
#define BSP_GY_PIN            GPIO_PIN_5
#define BSP_COMPRESSOR_LED_PORT GPIOB // PB12
#define BSP_COMPRESSOR_LED_PIN GPIO_PIN_12
```

## 6项实现任务

### 任务1：PE10-PE13 照明灯4色GPIO驱动

**需求：** light_ctrl的bit0-3分别驱动PE10-PE13，高电平亮低电平灭。
**涉及文件：**
- `App/main_app.c` — GPIO初始化 (PE10-PE13 push-pull output)
- `App/tasks/tasks.c` — ControlTask中，在`d->control.light_status = d->setpoint.light_ctrl`之后，添加4行HAL_GPIO_WritePin

**审查要点：**
1. GPIO初始化是否在正确位置（app_init, 在relay_driver_init之后）
2. light_ctrl bit0→PE10(LED1), bit1→PE11(LED2), bit2→PE12(LED3), bit3→PE13(LED4) 对应关系是否正确
3. 高电平=GPIO_PIN_SET=灯亮，这与当前继电器(也是SET=ON)一致
4. GPIOE时钟是否已使能（relay_driver_init已使能GPIOE，所以不需要再加）

### 任务2：PE7 内/外循环电磁铁驱动

**需求：** inner_cycle=1时PE7高电平（内循环），inner_cycle=0时PE7低电平（外循环）。
**涉及文件：**
- `App/main_app.c` — GPIO初始化 (PE7 push-pull output)
- `App/tasks/tasks.c` — ControlTask中添加WritePin

**审查要点：**
1. 电磁铁初始状态：上电默认inner_cycle=0 → PE7低 → 外循环。是否合理？
2. 互锁规则4(开放供氧时强制外循环)是否仍然生效？interlock_apply修改的是setpoint.inner_cycle还是直接改relay？
   - 查看 `Control/interlocks/interlock.c` 确认互锁是在setpoint层面还是control层面
3. PE7是否需要保护逻辑（如防止频繁切换磨损电磁铁）

### 任务3：新风净化=PTC风机调速

**需求：** fresh_air=1时，PTC风机提速（增大PWM占空比）。
**涉及文件：**
- `App/tasks/tasks.c` — ControlTask中修改fan_speed逻辑
- `Drivers/pwm/pwm_driver.c` — 确认pwm_set_fan_speed是否已支持调速

**审查要点：**
1. fresh_air=1时风速调到几档？直接设为max(3)？还是在当前fan_speed基础上+1？
2. 是否与现有fan_speed设定冲突？如果用户通过iPad设了fan_speed=2，fresh_air=1会覆盖吗？
3. 互锁规则4(开放供氧时强制新风ON)是否影响风速？
4. 当前pwm_set_fan_speed的档位映射：0=off, 1=low, 2=mid, 3=high 对应的PWM占空比

### 任务4：PB12 压缩机指示灯

**需求：** 压缩机继电器(BSP_RELAY_YASUO_IO=PE2)开时PB12亮，关时PB12灭。
**涉及文件：**
- `App/main_app.c` — GPIO初始化 (PB12 push-pull output)
- `App/tasks/tasks.c` — ControlTask中，在relay_driver_apply之后添加WritePin

**审查要点：**
1. 指示灯跟随relay_status还是跟随实际GPIO？用bitmap判断还是读回GPIO？
2. PB12初始化是否与GPIOB其他已用引脚（PB0/PB1/PB3/PB4/PB5/PB7/PB8/PB9）的时钟使能冲突

### 任务5：PB5 制氧机输出信号

**需求：** 供氧控制处于SUPPLYING或OPEN_MODE时，PB5输出高电平；IDLE时低电平。
**涉及文件：**
- `App/main_app.c` — GPIO初始化 (PB5 push-pull output)
- `App/tasks/tasks.c` — ControlTask中，在oxygen_control之后添加WritePin

**审查要点：**
1. 判断条件：用o2_state(SUPPLYING/OPEN_MODE)还是用relay_status的O2-IO位？
2. 当O2阀被互锁关闭时，GY-IO是否也应该关？
3. PB5是否与WH-IO(PB4 雾化)在relay_driver_init中已经初始化GPIOB时钟？

### 任务6：屏幕板设备运行指示LED

**需求：** 屏幕板面板上的绿色指示LED，在对应设备运行时亮。
**涉及文件：**
- `firmware/HDICU_ScreenBoard/main.c` — update_display_from_data()中添加指示灯驱动

**审查要点：**
1. 指示灯数据来源：0x01包中的relay_status(d[16-17])已有，可直接用
2. 屏幕板LEDA5-8对应的MCU GPIO尚未完全确认，可能需要从屏幕板原理图追踪
3. 如果LEDA信号通过TM1640 GRID驱动而非直接GPIO，实现方式不同
4. 当前update_display_from_data只设了GRID10(报警指示)，GRID11/12预留
5. 建议：如果无法确认GPIO，先通过TM1640的GRID/SEG位驱动（利用已有的s_u1_buf）

## 关键文件路径

| 文件 | 作用 |
|------|------|
| `BSP/bsp_config.h` | GPIO定义 (✅ 已更新) |
| `App/main_app.c` | GPIO初始化 (需修改) |
| `App/tasks/tasks.c` | ControlTask GPIO驱动 (需修改) |
| `Drivers/pwm/pwm_driver.c` | PTC风机PWM (需确认接口) |
| `Control/interlocks/interlock.c` | 互锁规则 (需确认inner_cycle处理方式) |
| `Control/oxygen/oxygen_control.c` | 供氧状态 (需确认o2_state枚举) |
| `App/data/app_data.h` | 数据结构 (参考light_ctrl/inner_cycle/o2_state定义) |
| `firmware/HDICU_ScreenBoard/main.c` | 屏幕板指示灯 (需修改) |
| `HARDWARE_PIN_MATRIX.md` | 引脚矩阵 (✅ 已更新) |

## 当前已验证的struct布局

```
AppData_t (76 bytes, g_app_data at 0x20000068):
  SensorData_t sensor      — 28 bytes (offset 0x00, 含_pad对齐修复)
  Setpoints_t setpoint     — 18 bytes (offset 0x1C)
    +0  target_temp (uint16)
    +2  target_humidity (uint16)
    +4  target_o2 (uint16)
    +6  target_co2 (uint16)
    +8  fog_time (uint16)
    +10 disinfect_time (uint16)
    +12 fan_speed (uint8)
    +13 nursing_level (uint8)
    +14 inner_cycle (uint8)
    +15 fresh_air (uint8)
    +16 open_o2 (uint8)
    +17 light_ctrl (uint8): bit0=检查, bit1=照明, bit2=蓝, bit3=红
  ControlState_t control   — ... (offset 0x2E)
  AlarmState_t alarm       — ... (offset 0x40, alarm_flags at +0x40)
  SystemState_t system     — ...
```

## 安全注意事项

1. **不要修改继电器映射** — 9路继电器(relay_driver.c)已实测通过，不碰
2. **不要修改struct SensorData_t的_pad** — 对齐修复刚做完，已验证
3. **互锁规则不能削弱** — 6条安全规则(interlock.c)只能加强不能放松
4. **PE10-PE13方向确认** — 高电平=亮，低电平=灭（与继电器SET=ON方向一致）
5. **PTC风机调速** — fresh_air不应该覆盖用户设定的fan_speed，只在需要时临时提速

## 期望输出

1. 对每项任务给出具体的代码修改方案（文件名+行号+代码片段）
2. 指出任何潜在的冲突或遗漏
3. 特别关注互锁逻辑与新GPIO驱动的交互
4. 评估任务6(屏幕板指示LED)是否可以在不确认LEDA GPIO的情况下实现
5. 给出建议的实现顺序和测试方案
