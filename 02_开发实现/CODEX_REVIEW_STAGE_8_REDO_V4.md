# Codex Review: Stage 8 redo v4 — 5 改动审查 (2026-05-07)

请逐条审查以下 5 处改动, 找出逻辑漏洞 / 边界条件 / race condition / 副作用。重点关注**纯氧隔离模式 (问题 4)** 和 **O2 计时口径变化 (问题 3)** 这两条改动可能影响的安全相关行为。

---

## 0. 前情提要 — 你上次审查发现的 2 个基础问题, 已全部修复

上次你审查 `CODEX_REVIEW_STAGE_8_REDO_V3_5_ISSUES.md` 时, 直接指出了我们排查思路里的 2 个**基础错误**, 严重影响后续诊断:

### 你指出的问题 1: JLink dump 不是可信的主板 dump
- 证据: `dump_T0.log` 里 JLink 提示 `Found Cortex-M4`, 但配置是 `Cortex-M3` (主板 STM32F103 是 M3, 屏幕板 GD32F303 是 M4)
- 结论: 之前的 dump 数据其实是从**屏幕板 RAM** 读的, 不是主板 g_app_data, 据此推断 `relay_status / switch_status / o2_accumulated` 完全无意义

**已修复**: dump 工具 `tools/diag_5issues_20260507/diag_all.bat` 加了 Cortex-M3 防呆:
```bat
findstr /C:"Found Cortex-M3" "%~dp0dump_%TAG%.log" >nul
if errorlevel 1 (
    echo [FAIL] JLink did NOT detect Cortex-M3.
    echo        Likely connected to screen board (GD32F303 = M4).
    del "%~dp0dump_%TAG%.log"
    ...
)
```
检测到 Cortex-M4 → 立即 abort + 提示用户重连主板 SWD, 不会再产生错误数据。

### 你指出的问题 2: build 目录有 stale object
- 证据: `firmware/HDICU_MainBoard/build/App/data/app_data.o` 时间戳 `2026/4/28`, `screen_protocol.o` 是 `2026/5/7 18:28`
- map 里 `g_app_data = 0x70 = 112B`, 但你按当前 `app_data.h` 重新计算 = **116B**
- 结论: ELF/map 混了旧 `app_data.o` (旧 struct 112B) 和新协议 obj (新 struct 116B), 字段地址漂移, **v3 改动 `d->control.o2_accumulated = 0` 链接到错位地址, 实际未生效**

**已修复**:
1. `make clean && make` 全量重编, 验证你的 116B 预测完全正确:
   ```
   旧 stale: g_app_data = 0x70 = 112B, MD5 = a16888ade461b60a22cb0acbeaebe412
   新 clean: g_app_data = 0x74 = 116B, MD5 = 0dffa1a60c30e9bf7387272907454d60
   ```
2. 旧 stale 目录改名 `_STALE_DO_NOT_USE_v3_182906/` 防止误烧
3. clean v3 归档到 `_post_stage8_redo_v3_clean_20260507_192939/`, 用户已重烧测试

### v3 实测后用户报告 (clean build 基础上)
- 问题 5 (KEY5 长按重新供氧从 0 起步): **修好** ✓ (v3 改动 B 在 clean build 下真生效)
- 问题 2 (内循环灯一直亮): 部分修好 (v3 KEY6 改长按防误触发), 但**OPEN_O2 模式下其他风机灯 (LEDA7 PC15, PA15, PA4, PB9, PD2) 仍亮**, 用户需要这些也关
- 用户经过万用表测电阻确认 PC13/PA0 物理无短路, 排除硬件问题
- 用户测温度数字实时变化 + PE1 加热实测正常, 确认主板调度健康

### v4 是 v3 clean build 之上的逻辑增量

本次 v4 在 v3 clean build 之上修改:
- **不动** v2 修复 (NTC 范围 + control_timers_start_fog/disinfect setpoint 同步)
- **不动** v3 case 0x06 长按清 o2_accumulated
- **不动** v3 case 0x07 改长按防误触
- **不动** g_app_data struct 布局 (新 struct 仍 116B, MD5 不同仅因逻辑变化)
- **重新做** make clean + 全量重编, 确认无 stale object 残留

v4 编译产物对照:
```
v3 clean: a16888ade461b60a22cb0acbeaebe412 → 0dffa1a60c30e9bf7387272907454d60 (重编)
v4:       0dffa1a60c30e9bf7387272907454d60 → a22d46875f6730697e6900d1d57e25ee (本次)
```

g_app_data 大小不变 (0x74 = 116B), 验证本次改动没改 struct 字段。

---

## 1. 项目状态

- 主板: STM32F103VET6, FreeRTOS 6 任务
- 屏幕板: GD32F303RCT6, 裸机
- 上一稳定版本: `stage8-redo-v3-CLEAN-BUILD` (commit `f457343`, MD5 `0dffa1a60c30e9bf7387272907454d60`)
- 本次审查: v4, MD5 `a22d46875f6730697e6900d1d57e25ee`
- 屏幕板**未改动**, 沿用 v1 build (MD5 `112f4c0355359106c36272d5051bf9b6`)
- 备份目录: `firmware/_pre_v4_5issues_20260507_204753/` (源码完整快照)
- git tag: `pre-v4-5issues-20260507`

---

## 2. 用户需求 (5 个问题)

| # | 问题 | v4 状态 |
|---|---|---|
| 1 | 长按 PC7 (内循环) 应同时关 PC15 (新风) — 互斥 | **本次实现** |
| 2 | 上电内循环灯一直亮 | v3 已部分修 (KEY6 改长按), v4 通过 Rule 4 改后, OPEN_O2 期间也不再误亮 |
| 3 | 编码器调氧浓度 (`target_o2`) 触发 O2 阀开时, 也要计时累加 | **本次实现** |
| 4 | 开放式供氧"按下后" = 纯氧隔离模式: 只开 O2 阀 + 计时, 其他全关 (风机 / 制冷 / 除湿 / 加湿 / 新风 / 加热 / 雾化 / UV) | **本次实现** |
| 5 | 长按 KEY5 重新供氧时计时从 0 起步 | v3 已实现 (case 0x06 长按 0→1 边沿清 `o2_accumulated`) |

用户额外确认:
- Q1 = 1 (护理上电默认档 = 1, 兼容 v2)
- Q2 = A (任何让 O2 阀开的路径都计时, 包括外部 PD8/PB6 demand)
- Q3 = 阀开就亮 (LEDA5 PC13 跟随 `relay_status bit 4`, **保持当前 v3 行为, 屏幕板不改**)
- Q4 = 否 (退出 OPEN_O2 不清 `enable_o2_ctrl`, 保留用户设置)

---

## 3. v4 完整改动 (5 个文件, 35 行净增)

### 改动 A: `Protocol/screen/screen_protocol.c` 三处

**A.1 KEY1 单击循环改 5 档** (line 252):
```c
// 改前: 1→2→3→1
d->setpoint.nursing_level = (d->setpoint.nursing_level % 3) + 1;
// 改后: 0→1→2→3→4→0
d->setpoint.nursing_level = (uint8_t)((d->setpoint.nursing_level + 1) % 5);
```

**A.2 编码器调护理 OOB 检查改 4** (line 219-221):
```c
// 改前: if (value >= 1 && value <= 3) ...
// 改后: if (value <= 4) d->setpoint.nursing_level = (uint8_t)value;
```
注意: 改后允许 value=0 写入 (之前不允许, 因为 `>= 1`)。

**A.3 case 0x07 长按互斥关新风** (line 339-348):
```c
case 0x07:
    if (d->setpoint.inner_cycle) {
        d->setpoint.inner_cycle = 0;
    } else {
        d->setpoint.inner_cycle = 1;
        d->setpoint.fresh_air   = 0;   /* 互斥 */
    }
    break;
```
**审查点**: 关新风时不动 fresh_air=0 是否会跟用户主动按 KEY7 的预期冲突?

### 改动 B: `App/tasks/tasks.c` 两处

**B.1 fan_speed_actual OPEN_O2 时强制 0** (line 187-194):
```c
if (d->control.switch_status & SW_BIT_OPEN_O2) {
    d->control.fan_speed_actual = 0;
} else {
    d->control.fan_speed_actual = d->setpoint.fan_speed;
}
```

**B.2 护理 5 档 LED 物理映射** (line 197-216):
```c
uint8_t lv = d->control.nursing_level_actual;
HAL_GPIO_WritePin(BSP_LED_HULI1_PORT, BSP_LED_HULI1_PIN,    // PB1
                  (lv == 2 || lv == 4) ? GPIO_PIN_SET : GPIO_PIN_RESET);
HAL_GPIO_WritePin(BSP_LED_HULI2_PORT, BSP_LED_HULI2_PIN,    // PB0
                  (lv == 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
HAL_GPIO_WritePin(BSP_LED_HULI3_PORT, BSP_LED_HULI3_PIN,    // PC5
                  (lv == 3 || lv == 4) ? GPIO_PIN_SET : GPIO_PIN_RESET);
```
注意: BSP 命名 HULI1=PB1, HULI2=PB0 (BSP 编号跟物理排列对调). 用户给的物理映射:
- 档 0: 全灭
- 档 1: PB0 (= HULI2)
- 档 2: PB1 (= HULI1)
- 档 3: PC5 (= HULI3)
- 档 4: PB1 + PC5 (= HULI1 + HULI3)

### 改动 C: `Control/interlocks/interlock.c` Rule 4 (line 87-126)

```c
if (d->control.switch_status & SW_BIT_OPEN_O2) {
    relay_set(r, BSP_RELAY_O2_IO);

    // v4 关键: 内/外/新风全关 (之前 v3 强制开新风)
    d->control.switch_status &= ~SW_BIT_INNER_CYCLE;
    d->control.switch_status &= ~SW_BIT_FRESH_AIR;     // ← v4 改: 之前是 |= SW_BIT_FRESH_AIR

    relay_clear(r, BSP_RELAY_PTC_IO);
    relay_clear(r, BSP_RELAY_JIARE_IO);
    relay_clear(r, BSP_RELAY_WH_IO);
    d->control.fog_remaining = 0;
    relay_clear(r, BSP_RELAY_ZIY_IO);
    d->control.disinfect_remaining = 0;

    // v4 新增: 制冷/除湿/加湿/外风机全关
    relay_clear(r, BSP_RELAY_YASUO_IO);     // 压缩机 (= 制冷+除湿)
    relay_clear(r, BSP_RELAY_FENGJI_IO);    // 空调外风机
    relay_clear(r, BSP_RELAY_JIASHI_IO);    // 加湿器
    triggered = true;
}
```

**关键审查点**:
1. `interlock_can_start_cooling()` 仍有依赖 `outer_on && fresh_on` 的判断, OPEN_O2 时 fresh_on=false 后该函数返回 false → cooling 不能启动 (这跟 Rule 4 强制清 YASUO 一致), 是否还需要简化这函数?
2. PTC 风机由 `pwm_set_ptc_arbiter(safety_min, fresh_duty, user_duty)` 仲裁, OPEN_O2 时三方都 0:
   - `safety_min = (temp_state == HEATING) ? 80 : 0`: 但 Rule 4 清了加热 relay, temp_state 可能在下一个 ControlTask 周期才更新到 IDLE. **这一拍内是否仍可能 safety_min=80?** (race window)
   - `fresh_duty = (switch_status & SW_BIT_FRESH_AIR) ? 100 : 0`: Rule 4 已清, 同周期生效 ✓
   - `user_duty`: 来自 `fan_speed_actual` (B.1 改), 同周期生效 ✓
3. 退出 OPEN_O2 (`setpoint.open_o2 = 0`) 后, Rule 4 不再触发, 各继电器交回 control 子函数仲裁。**有无 race window?** 比如 humidity_control 在同一拍重新打开 JIASHI, 但 Rule 4 已经不强制清, 用户原来设的湿度恢复闭环 — 这是预期行为还是问题?

### 改动 D: `Control/timers/control_timers.c` 累加条件 (line 36-44)

```c
// 改前
if (d->setpoint.open_o2 && o2_accumulated < 0xFFFF) {
    o2_accumulated++;
}
// 改后
if ((d->control.relay_status & (1U << BSP_RELAY_O2_IO)) &&
    o2_accumulated < 0xFFFF) {
    o2_accumulated++;
}
```

**关键审查点**:
1. 改后**外部 PD8/PB6 demand** 触发 O2 阀也会累加。Q2=A 已确认接受。
2. `case 0x06` 长按 0→1 时清 `o2_accumulated = 0` (v3 已加), 但 OPEN_O2 mode 下 O2 阀必然开, 累加从 0 开始 ✓
3. 但**第一次进入 OPEN_O2 这一秒可能丢一拍**: case 0x06 长按 → 主板把 setpoint.open_o2=1 → 下个 ControlTask (≤200ms) 才跑 interlock_apply → 才设 BSP_RELAY_O2_IO → 下一秒 control_timers_tick_1s 才累加. 累加从 1 开始 (第 1 秒) 还是从 0 开始 (按下瞬间 = 0)? 可能差 1 秒, 是否要紧?

### 改动 E: `App/data/app_data.h` 注释更新

只改注释, 无逻辑影响。

---

## 4. 不变化的 (跟 v3 一致, 不要去碰)

- v2 修复 (NTC 范围检查 + control_timers_start_fog/disinfect 同步 setpoint): **未动**
- v3 case 0x06 长按 0→1 清 `o2_accumulated`: **保留**
- v3 case 0x07 改长按防误触: **保留** (本次基础上加互斥关新风)
- 屏幕板代码: **完全未动**, MD5 不变, 不需要重新编译屏幕板
- `app_data.c` init 默认值: `nursing_level = 1`, `inner_cycle = 0`, `fresh_air = 0` 等都没动
- iPad 协议 (`ipad_protocol.c`) 中 nursing 范围: **没显式 OOB 检查**, 用户写任意 uint8 都接受 (本次没加 OOB)

---

## 5. 给 Codex 的具体审查请求

请按下面 7 个角度逐项检查, 如有问题给出**具体行号 + 修复建议**:

### A. 5 档循环边界
- `(level + 1) % 5` 当 level=4 时下一个是 0 ✓
- 当 level 被 OOB 检查 (case 0x05 改后允许 0~4) 设到 0 时, 屏幕板 LEDA1 (`if (nursing > 0)`) 灭, 是否符合"档 0 = 关闭护理"语义?
- iPad 0x03 写 `nursing_level = 5` (越界值), `handle_write_params` 没有 OOB 检查, 直接 `setpoint.nursing_level = 5` 后, KEY1 单击会让它变成 `(5+1)%5=1` (神奇地 normalize 了)。这种"被动修复"是否可接受?

### B. Rule 4 加严的副作用 — race condition
- ControlTask 顺序: `temp_control_update → humidity_control_update → oxygen_control_update → interlock_apply → fan_speed_actual 同步 → ... → relay_driver_apply`
- temp_control 在 oxygen_control 之前, 它可能根据 `setpoint.target_temp > sensor.temperature_avg` 把 BSP_RELAY_PTC_IO 置 1, 然后 interlock_apply Rule 4 清掉。**期间 control.temp_state = HEATING** — 然后在 fan_speed_actual 同步之后 `pwm_set_ptc_arbiter(safety_min=80, ...)`?
- 同样: humidity_control 可能设 JIASHI bit, Rule 4 清掉; humid_state 字段呢?

### C. 累加条件改动后的语义
- 用户进入 OPEN_O2 → o2_accumulated 累加
- 用户退出 OPEN_O2 → 此后若 enable_o2_ctrl=1 + sensor.o2 < target_o2 → SUPPLYING → O2 阀开 → 仍然累加
- 累加值是 "OPEN_O2 时长 + 自动闭环时长 + 外部 demand 时长" 的总和, 用户从屏幕只能看到一个数字。**是否需要给屏幕加文字提示区分?** (Q3 用户回答 "阀开就亮" 暗示不需要区分)

### D. interlock_can_start_cooling
```c
if (d->control.switch_status & SW_BIT_OPEN_O2) {
    bool outer_on = !(d->control.switch_status & SW_BIT_INNER_CYCLE);
    bool fresh_on = (d->control.switch_status & SW_BIT_FRESH_AIR) != 0;
    return outer_on && fresh_on;   // v4 中 fresh_on 永远 false → 返回 false ✓
}
return true;
```
v4 后 OPEN_O2 时 fresh_on 必然 false, 这函数的 OPEN_O2 分支永远 return false. 等价于 `if (OPEN_O2) return false;`. 是否要简化?

### E. 屏幕板 LEDA5 行为 (Q3=阀开就亮)
现行 (`screen_main.c:1303`):
```c
if (relay_st & (1 << 4)) GPIOC_BSRR = (1 << (13 + 16));
```
意味着自动闭环 + 外部 demand 时 LEDA5 也亮. 用户回答 "阀开就亮" 接受这个。但**屏幕标签是"开放式供氧"**, 自动闭环时用户看到这灯亮可能误以为开放供氧。**是否要在用户文档说明?**

### F. 退出 OPEN_O2 后的设备恢复
Q4 = 否 (不清 enable_o2_ctrl). 用户原来按了 KEY5 长按打开开放供氧, 之前 `enable_o2_ctrl=1` (因为编码器调过 target_o2). 退出 OPEN_O2 后:
- enable_o2_ctrl 仍 = 1
- 自动闭环重新工作, 检测 sensor.o2 < target_o2 → 开 O2 阀
- LEDA5 亮 (Q3 接受)
- O2 累加继续 (D 改后接受)
- **但 Rule 4 期间清 YASUO/FENGJI/JIASHI 等**, 这些字段退出后由 humidity_control / temp_control 重新决策. 如果用户原本制冷在跑 (压缩机 ON), Rule 4 关掉后, 退出 OPEN_O2 后 temp_control 看到当前温度可能仍 > target_temp → 重新打开制冷. 这是预期行为 (恢复正常), 但**温度可能在 OPEN_O2 期间漂移过, 重启控制是否会出现激烈震荡?**

### G. KEY6 互斥关新风的反向需求
本次改 case 0x07: 开内循环时关新风。但**未实现反向**: 开新风时关内循环?
- 如果用户先长按 KEY6 开内循环 (新风灭) → 再单击 KEY7 (新风) → 新风开 (内循环不变)
- 现在内循环 + 新风同时开, switch_status = 0x03 (bit0 + bit1)
- 这是不是用户想要的"互斥"? 还是 KEY7 触发时也要清 inner_cycle?

---

## 6. 编译产物

- 主板 bin: `firmware/_post_stage8_redo_v4_20260507_205107/main_stage8_redo_v4_FINAL.bin`
- 主板 elf: 同目录
- map: 同目录 (g_app_data 116B, 跟 v3 一致, struct 没变)
- MD5: `a22d46875f6730697e6900d1d57e25ee`
- size: text 27784 / data 104 / bss 37880 (跟 v3 同 size, 仅逻辑改变)

## 7. 测试矩阵 (待 Codex 审过再烧测)

### 新功能 (v4)
- [ ] T1: 上电后冷启动, 长按 KEY6 (CN4) 2 秒 → 内循环灯 (PA0) 亮 + 新风灯 (PC15) 灭. 单击 KEY6 不响应.
- [ ] T2: KEY1 单击 5 次, 观察护理灯按 0→1→2→3→4→0 循环, 第 4 档 PB1+PC5 同时亮.
- [ ] T3: 长按 KEY5 (CN2) 进入开放供氧, 观察:
  - O2 阀 (PB7) 亮 ✓
  - 屏幕 LEDA5 (PC13) 亮 ✓
  - 屏幕 LEDA7 (PC15) **灭**
  - 屏幕 LEDA8 (PA15) **灭**
  - 屏幕 LED8/9/10 (PA4/PB9/PD2) **全灭**
  - 主板压缩机 (PE2) **关**
  - 主板加湿器 (PE4) **关**
  - 主板 PTC 风机 (PE9 PWM) **0%**
- [ ] T4: 编码器调 target_o2=40, sensor.o2 < 40 → O2 阀开 → 屏幕"供氧时间"开始累加.
- [ ] T5: KEY5 长按再触发 (1→0 退出) → O2 阀关, 各设备恢复正常控制 (压缩机/加湿/风机按 setpoint 重新决策).

### v3 回归 (确认不退化)
- [ ] T3 PE1 闪烁仍修好 (NTC 范围检查)
- [ ] T4/T8 雾化/消毒 SET 页显示分钟仍对
- [ ] KEY5 长按打开 → 计时从 00:00:00 起步

### v2 回归
- [ ] T1-T8 stage 8 redo v2 PASSED 测试矩阵全部通过

---

## 请 Codex 给的回应格式

```
[Pass / Concern / Block] - 按 A~G 7 项审查角度
A. ...
B. ...
...
G. ...

[整体建议]
- 是否可以烧录 v4 给用户测试?
- 必须先修的 critical issue (block)
- 建议优化但不阻塞的 (concern)
- 测试时特别注意观察的点
```

如有发现, 请引用具体源码行号 (我用的相对路径是 `firmware/HDICU_MainBoard/...`)。
