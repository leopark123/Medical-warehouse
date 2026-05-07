# Codex Review: Stage 8 redo v3 — 5 Issues 排查 (2026-05-07)

请独立评估以下排查过程, 验证诊断方向是否正确, 找出可能遗漏的根因。

---

## 1. 项目背景 (1 分钟读完)

**HDICU-ZKB01A** 婴儿/动物医疗培养箱, 双 MCU 系统:
- **主板**: STM32F103VET6 (Cortex-M3, 72MHz, 512KB Flash, 64KB RAM)
- **屏幕板**: GD32F303RCT6 (Cortex-M4, 裸机 main loop, 1342 行 main.c)

通信: UART 115200 自定义帧
- 主→屏 0x01: 39 字节实时显示数据 (1Hz)
- 屏→主 0x82: 按键事件 (KEY_ID + action)

源码:
- `firmware/HDICU_MainBoard/` (FreeRTOS, 6 任务)
- `firmware/HDICU_ScreenBoard/main.c` (单文件裸机)

---

## 2. 当前固件状态

### 已通过 (stage 8 redo v2 PASSED, 2026-05-07 早些时候)
- T3 PE1 闪烁修复: NTC `[-40, 60]°C` 范围检查 (`Sensors/ntc/ntc_sensor.c:61`)
- T4 雾化分钟显示归 0 修复: `control_timers_start_fog` 同步 setpoint (`Control/timers/control_timers.c:52, 58`)
- T8 消毒分钟显示归 0 修复: `control_timers_start_disinfect` 同步 (`control_timers.c:71, 77`)
- 主板 bin: `_post_stage8_redo_v2_20260507_143742/main_stage8_redo_v2_FINAL.bin`
- MD5: `8ff5ec6badb54e169b8ab7133ce1637b`, 27872 字节
- git tag: `stage8-redo-v2-PASSED`

### 刚出的 v3 build (2026-05-07 18:29, **本次排查的对象**)
- 主板 bin: `_post_stage8_redo_v3_20260507_182906/main_stage8_redo_v3_FINAL.bin`
- MD5: `a16888ade461b60a22cb0acbeaebe412`, 27888 字节 (+16B)
- 屏幕板 bin: 沿用 v1 build, MD5 `112f4c0355359106c36272d5051bf9b6`, **未重烧**
- v3 改动只在主板 `Protocol/screen/screen_protocol.c` (见下文)
- git tag: `pre-v3-keys-20260507` (回滚锚点)
- 备份: `_pre_v3_keys_20260507_180136/`

---

## 3. 用户报告的 5 个新问题

| # | 现象 | 用户期望 |
|---|---|---|
| 1 | 单击 KEY7 (新风, CN6) 让屏幕"供氧时间"在 0 ↔ **00:08:32 (= 512 秒)** 之间 toggle | 不该变 |
| 2 | 上电后内循环灯 **LEDA6 (PA0)** 一直亮 | 默认应灭 |
| 3 | 屏幕氧气浓度 **一直显示 61%** 不变 (摸传感器也不变) | 实时跟随 O2 sensor |
| 4 | 护理等级要 **0~4 共 5 档**, 第 4 档同时亮后两颗灯 | 当前固件只 1~3 三档 |
| 5 | 长按 KEY5 (开放供氧, CN2) 时供氧计时从 **00:25:30 (= 1530 秒)** 起步, 不是 0 | 按下从 0 开始 |

---

## 4. v3 已做的代码改动 (针对问题 2 + 5)

文件: `firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c`

### 改动 A: KEY6 (内循环) 改长按 2 秒触发 (line 294-298)
**修改前**:
```c
case 0x07: /* 内/外循环 toggle */
    d->setpoint.inner_cycle = d->setpoint.inner_cycle ? 0 : 1;
    break;
```
**修改后**:
```c
case 0x07: /* 内/外循环 — 单击忽略, 必须长按2秒才触发(防误触)
            * Stage 8 redo v3 (2026-05-07) 修问题 2: 之前单击 toggle, 上电时
            * 屏幕板 PC7 浮空容易被识别为按下→inner_cycle 卡 1 内循环灯一直亮.
            * 改长按 2 秒触发, 跟 KEY5 开放供氧一致. 实际翻转在长按分支. */
    break;
```

并在长按分支 (line 339-342) 加:
```c
case 0x07: /* 内/外循环 — 长按2秒翻转 */
    d->setpoint.inner_cycle = d->setpoint.inner_cycle ? 0 : 1;
    break;
```

### 改动 B: KEY5 长按 0→1 边沿清 o2_accumulated (line 329-338)
**修改前**:
```c
case 0x06: /* 开放式供氧 — 长按2秒翻转 */
    d->setpoint.open_o2 = d->setpoint.open_o2 ? 0 : 1;
    break;
```
**修改后**:
```c
case 0x06: /* 开放式供氧 — 长按2秒翻转 (防误触保护)
            * Stage 8 redo v3 (2026-05-07) 修问题 5: 0→1 边沿清 o2_accumulated. */
    if (d->setpoint.open_o2) {
        d->setpoint.open_o2 = 0;
    } else {
        d->setpoint.open_o2 = 1;
        d->control.o2_accumulated = 0;   /* 重新计时 */
    }
    break;
```

---

## 5. v3 烧录后用户实测结果

**问题 2**: 上电后内循环灯**还是亮**, 长按 KEY6 (CN4) 没反应, 单击也没反应。
**问题 5**: 长按 KEY5 (CN2) 后供氧计时**还是从 25:30 开始**, **没有清 0 (= v3 改动 B 未生效?)**。
**意外现象**: **长按 KEY6 (CN4) 后, 供氧计时数码管在变化** (累加中)。

---

## 6. 排查过程 + 关键证据

### 6.1 v2 PASSED 测试通过, 之后报 5 个新问题
v2 PASSED 当晚, 用户重烧 (相同 MD5 8ff5e...) 测试新功能, 报出 5 个问题。

### 6.2 我做了 JLink dump 工具 (`tools/diag_5issues_20260507/`)
读 24 个字段, 6 个 checkpoint (T0~T5)。**字段地址基于以下假设**:
- `g_app_data` 起始 0x20000070 (.map 验证 ✓)
- `g_app_data` 大小 0x70 = 112 字节 (.map ✓)
- enum 用 -fshort-enums = 1B
- SensorData_t 30B, Setpoints_t 22B (含 1B padding), ControlState_t 18B
- struct 顺序: sensor → setpoint → control → alarm → system → calibration → limits → cancel_flags

**Makefile 没有显式 `-fshort-enums`** (`firmware/HDICU_MainBoard/Makefile:132`):
```
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) -Wall -Wextra -fdata-sections -ffunction-sections -std=c99 $(CFLAGS_EXTRA)
```
ARM EABI 默认 enum = 4B (除非加 -fshort-enums). **这可能影响 ControlState_t 内部偏移**。

### 6.3 v1 dump 验证字节序 + 偏移图

stage 8 redo v1 PASSED 之前的 dump (`04_结构与测试资料/aaa1/dump_v2_1.log`):
- `0x2000008E = "1B 01"` → LE uint16 = 0x011B = 283 = `setpoint.target_temp` (用户调温到 28.3°C) ✓
- `0x200000AE = "03 0F"` → LE uint16 = 0x0F03 = 3843 = `control.relay_status` (HANDOFF 文档明确写) ✓

**结论**: J-Link 输出按 byte address 递增 LE 字节流, **enum=1B 假设下 v1 时偏移图正确**。

### 6.4 v2 重烧后 dump 数据反常 (2 轮 dump)

第二轮 dump (用户 v2 重烧后, `04_结构与测试资料/0-5-2/`):

| 地址 | 假设字段 | T0 值 | 期望 (init 后) | 解读 |
|---|---|---|---|---|
| 0x2000008E | setpoint.target_temp | "01 00" = LE 0x0001 = 1 | 250 = 0xFA00 | **不一致** |
| 0x200000AC | control.o2_accumulated | "00 FA" = 0xFA00 = 64000 | 0 | **不一致** |
| 0x200000AE | control.relay_status | "01 F4" = 0xF401 | 0 (上电不加热) | **不一致** |
| 0x200000B1 | control.switch_status | 0xD2 = 0b1101_0010 | 0 (无 inner_cycle) | bit 0 = 0, **跟 LEDA6 亮矛盾** |

**6 次 dump (T0~T5) 这些字段全部死板不变**, 但 KEY1 nursing_level 字段 (0x2000009B) 工作正常 (T0=1 → T5=3 经 5 次 KEY1 cycle)。

### 6.5 用户实测温度数字**实时变化** (摸传感器跟随)

⇒ ControlTask + SensorTask + CommScreen 都在跑 (温度数字依赖 SensorTask 算 + CommScreen 1Hz 发 0x01 包)

⇒ **不是 task hung**

### 6.6 PE1 加热继电器**实测正常工作** (温度高时亮)

⇒ ControlTask 写过 `control.relay_status & (1<<0)` + `relay_driver_apply` 跑过

### 6.7 屏幕板 LED 物理状态 (用户观察)
- LEDA1 (KEY1 护理) 灭 ⇒ 屏幕板看到 byte 9 (`nursing_level_actual`) = 0
- LEDA5 (KEY5 开放供氧) 亮 ⇒ byte 17 bit 4 = 1 (relay_status low byte 含 bit 4)
- LEDA6 (KEY6 内循环) 亮 ⇒ byte 19 bit 0 = 1 (switch_status & SW_BIT_INNER_CYCLE)
- LEDA2/3/4/7/8 灭

**用户万用表测 PA0 / PC13 对地电阻**: 正常 (高阻), **排除物理短路**。

### 6.8 dump 跟实测的核心矛盾

| 实测 | dump 数据 | 矛盾 |
|---|---|---|
| LEDA5 亮 | 0x200000AE = 0xF401, bit 4 = 0 | byte 17 bit 4 应 = 1 但 dump = 0 |
| LEDA6 亮 | 0x200000B1 = 0xD2, bit 0 = 0 | byte 19 bit 0 应 = 1 但 dump = 0 |
| LEDA1 灭 | 0x200000B5 = 0 | byte 9 应 = 0 ✓ (但 ControlTask 应该把 setpoint=1 同步到 actual=1) |
| 6 次 dump 字段死板 | 0xF401 / 0xD2 不变 | ControlTask 在跑 (PE1+温度证) 但字段不更新? |

**两种假设**:
- (a) **dump 的字段地址错位** — enum=4B 让 ControlState_t 偏移移位, 我读到的"0x200000AE / 0x200000B1"实际不是 relay_status / switch_status
- (b) **g_app_data 内部 layout 不是我假设的顺序** — 但 .map 验证起始地址 + 大小都对

### 6.9 v3 烧录后用户实测 — **决定性新证据**

用户报告 "**长按 KEY6 (CN4) 后供氧计时数码管在变**"。

`o2_accumulated` 只有 2 个写入路径:
1. `control_timers_tick_1s` 在 `setpoint.open_o2 = 1` 时累加
2. `case 0x06 长按` (v3 改动 B) 0→1 边沿清 0

**长按 KEY6 触发"供氧计时变化"** ⇒ 主板收到的 KEY_ID 是 0x06 (开放供氧 case 0x06 长按), **不是** 0x07 (内循环)。

但**屏幕板代码 KEY_ID_MAP[5] = 0x07** (CN4/PC7), `key_read_raw(5)` 读 PC7 (`screen_main.c:1596`):
```c
case 5: level = GPIOC_IDR & (1 << 7);  break;  /* KEY6 PC7  */
```

⇒ **CN4 物理按键的信号实际接到了 PC6, 不是 PC7** (要么 PCB 接线交换, 要么 CN2/CN4 标签贴反)

⇒ 这同时解释了 LEDA6 一直亮: **PC7 走线断 / 浮空 → debounce 误识别为按下 → 长按 → 主板 case 0x07 长按 → toggle inner_cycle 0→1**

⇒ 之后 "再按 KEY6 没反应" 是因为 raw 一直被识别为按下, 不会出现 release-then-press 边沿。

### 6.10 但还有未解的事:
- 问题 5 用户说 v3 改动 B 没生效 ("还是 25:30") — **如果 KEY5 长按物理上接到了 PC7**, 那触发的会是 case 0x07 长按, 不是 case 0x06 长按, o2_accumulated 不会被清 0
- 问题 1 (KEY7 让供氧时间变 8:32) 仍无完整解释
- 问题 3 (氧气 61) 没法验证 — 需要 dump sensor.o2_raw / o2_valid 多次
- dump 字段错位/死板的根因没确认 (假设 a 还是 b)

---

## 7. 我的当前诊断

**主因**: **PCB 接线问题** (硬件), 软件代码本身正确。
- CN4 (内循环) 接到 PC6, CN2 (开放供氧) 可能接到 PC7
- 或两个 CN 标签互换
- 或 PC7 走线断, PC6 信号串到了 CN4

**次因**: dump 字段地址解读不准, 但跟核心问题无关。可能 enum=4B 让 ControlState_t 偏移漂移, 但因为 ControlTask **在跑** (PE1 实测+温度实时) 且行为基本正确 (除了误触发的 inner_cycle), 软件层面没看到 race / corrupt。

**v3 改动**:
- KEY6 (KEY_ID 0x07) 改长按: 跟接线 bug 叠加后**没用** (因为 PC7 不是真按 CN4 的事件源, 实际触发 case 0x06 case 0x07 都不会真按 CN4)
- KEY5 长按清 o2_accumulated: **应该生效**, 但用户实测"还是 25:30" — 暗示用户长按的"KEY5"实际不是 case 0x06 长按事件

---

## 8. 给 Codex 的具体问题

请独立判断:

1. **enum 大小**: ARM GCC 14.3.1 (`F:\ST\STM32CubeIDE_2.1.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin`) 默认 enum 大小是几字节? 跟 -fshort-enums 行为相比, ControlState_t 内的 `TempState_t / HumidState_t / O2State_t` 三个 enum 实际占几字节?

2. **dump 字段错位**: 给定 g_app_data 在 0x20000070, 大小 0x70 (112B), 假设 enum=4B 重新算 control 内部偏移, 看 control.relay_status 真实地址是什么。基于 v3 dump 数据 (T0 baseline, ControlTask 跑过), 实际 relay_status 值应该是什么?

3. **接线诊断**: 现象"按 CN4 触发 KEY_ID 0x06" 是否唯一指向 PCB 接线? 还有没有别的软件路径让按 PC7 转发为 0x06 事件?

4. **问题 1 (KEY7 让供氧时间变 8:32)** 的原因? 8:32 = 512 秒 = 0x0200, byte 14 = 0x02 = SW_BIT_FRESH_AIR — 这巧合是否说明屏幕板 byte 14 (= o2_accumulated 高字节) 跟 byte 19 (= switch_status) 显示有交叉?

5. **问题 5 v3 改动失效**: 如果用户按的是 KEY5 (CN2), v3 改动 B 应让 o2_accumulated 清 0; 但实测 "还是 25:30". 说明:
   - (a) 用户实际按的不是 PC6 (case 0x06)
   - (b) v3 改动 B 没正确编译进去
   - (c) o2_accumulated 在按下后**立即被另一个写入** (race)
   哪种最可能?

6. **v3 build 是否正确**: 主板 .bin 27888 字节 (vs v2 27872, +16B). 16 字节增量看起来合理吗?

7. **建议 v4 改动**: 如果根因是 PCB 接线 (CN2/CN4 交换), 软件层面把屏幕板 KEY_ID_MAP[4]/[5] 互换是不是临时绕过的最快方法? 副作用?

---

## 9. 关键文件路径 (供 Codex 索引)

源码:
- `firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c` (case 0x06/0x07/0x08 dispatch)
- `firmware/HDICU_MainBoard/Control/interlocks/interlock.c` (sync_switch_status)
- `firmware/HDICU_MainBoard/Control/timers/control_timers.c` (o2_accumulated 累加 + reset)
- `firmware/HDICU_MainBoard/App/data/app_data.h` (struct 定义)
- `firmware/HDICU_MainBoard/App/data/app_data.c` (init 默认值)
- `firmware/HDICU_MainBoard/App/tasks/tasks.c` (ControlTask line 170-280)
- `firmware/HDICU_MainBoard/Drivers/gpio/relay_driver.c` (BSP_RELAY_xxx → GPIO 映射)
- `firmware/HDICU_MainBoard/BSP/bsp_config.h` (BSP_RELAY_PTC_IO=0, O2_IO=4, WH_IO=8)
- `firmware/HDICU_ScreenBoard/main.c` (1342 行, 含 LEDA_Update / Key_Scan / KEY_ID_MAP)

Dump 数据:
- `04_结构与测试资料/aaa1/dump_v2_*.log` (v1 stage 8 redo 时, 含 PE1 闪烁状态)
- `04_结构与测试资料/aaa0-5/dump_T0..T5.log` (v2 重烧 dump 第二轮)
- `04_结构与测试资料/0-5-2/dump_T0..T5.log` (v2 重烧 dump 第三轮)

固件 + map:
- `firmware/_post_stage8_redo_v2_20260507_143742/` (v2 PASSED bin + elf)
- `firmware/_post_stage8_redo_v3_20260507_182906/` (v3 待测 bin + elf)
- `firmware/HDICU_MainBoard/build/firmware.map` (g_app_data @ 0x20000070, 0x70)

Backup:
- `firmware/_pre_v3_keys_20260507_180136/` (改 v3 之前的源码完整快照)

---

## 10. 测试矩阵 (用户已跑过)

T0: 上电 baseline (5 秒后 dump)
T1: 按 KEY7 第 1 次, dump
T2: 按 KEY7 第 2 次, dump
T3: 长按 KEY5 (用户标记的开放供氧 CN2), dump
T4: 等 30 秒, dump
T5: 按 KEY1 5 次, dump

---

## 11. Codex 请回答

请用结构化方式回答以下:

A. 假设 enum=4B 重新算字段地址, dump 数据是否能 self-consistent (相互一致)?
B. PCB 接线交换是不是 5 个问题中至少 2/5 (问题 2+5) 的真正根因?
C. 问题 1 (KEY7 → 供氧时间变 8:32) 的根因是什么?
D. 问题 3 (氧气 61) 跟 problems 2/5 是否同源?
E. 问题 4 (5 档) 是新功能确认, 不需要排查; 但 5 档实现细节有什么值得提前注意的?
F. v3 改动 (KEY6 长按 + KEY5 清 o2_accumulated) 是否需要回滚? 还是保留 + 加新改动?
G. 下一步建议: 立即可做的代码改动 / 必须用户参与的硬件测试 / 需要 elf 符号表 dump 的诊断动作?
