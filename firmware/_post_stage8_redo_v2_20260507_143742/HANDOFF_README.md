# Stage 8 redo v2 — NTC 温度范围限制 + 雾化/消毒 setpoint 同步

**生成时间**: 2026-05-07 14:37
**基于**: Stage 8 redo (`_post_stage8_redo_20260507_121517/`)
**修复**:
- T3 PE1 闪烁 (NTC 物理短路时 ADC 算出极高温度 → 滤波平均拉高 → 误切 COOLING)
- T4/T8 雾化/消毒分钟显示归 0 (control_timers_start_fog/disinfect 未同步 setpoint)

**烠录**: 仅烠主板; **屏幕板不变** (与 redo v1 同, MD5 `112f4c0355359106c36272d5051bf9b6`)

---

## 二进制产物

| 文件 | 大小 | MD5 |
|---|---|---|
| `main_stage8_redo_v2_FINAL.bin` | 27872 B | `8ff5ec6badb54e169b8ab7133ce1637b` |
| `main_stage8_redo_v2_FINAL.elf` | 61116 B | — |
| `screen_stage8_redo_FINAL.bin` (与 v1 相同, 不需重烠) | 5656 B | `112f4c0355359106c36272d5051bf9b6` |

**Size 增量** (vs Stage 8 redo v1):
- 主板: 27856 → 27872 B (+16 B, 来自 NTC 范围检查 + 4 行 setpoint 同步)
- 屏幕板: 不变

---

## 一、修复 1: NTC 温度范围合理性检查 (修 PE1 闪烁)

### 根因 (JLink dump 证据)
10 次 dump 显示 `temp_state` 在 sample 3 / 5 异常切到 COOLING (=1), 但 actual=26.7°C 远低于 setpoint=28.3°C, 按状态机逻辑不可能。

`ntc_adc_to_temp_x10` 边界分析:
- ADC=10 (短路边界刚通过): 计算出 **272°C**
- ADC=100: **140°C**
- ADC=500: **77°C**
- ADC=2000: 26°C (正常)

NTC 物理短路时 (湿气/凝结), ADC 短暂跳到 10~100 范围, 算出 100°C+ 极值。30 sample 滑动平均**不能完全稀释**, 拉高滤波后温度 → temp_control 切 COOLING → PE1 OFF + PE2 ON, 用户看到 PE1 一起一停。

### 修复方案
`Sensors/ntc/ntc_sensor.c::ntc_adc_to_temp_x10` 末尾加温度范围检查:
```c
int16_t t_x10 = (int16_t)(t_celsius * 10.0f);
if (t_x10 < -400 || t_x10 > 600) {
    return -999;   /* 超出 -40~60°C 物理范围, 视为 invalid, 滤波器跳过 */
}
return t_x10;
```

**有效范围**: -40°C ~ 60°C (培养箱物理使用范围 10~40°C, 60°C 是充足上限)。
**效果**: NTC 短路时计算出的 200°C+ 被识别为 invalid, 滤波器跳过, 不污染平均温度。

---

## 二、修复 2: T4/T8 雾化/消毒 setpoint 同步

### 根因 (JLink dump 证据)
`setpoint.fog_time = 0`, 但 `fog_remaining` 在某次启动时设过非零。
`control_timers_start_fog()` 只更新 `control.fog_remaining`, 不更新 `setpoint.fog_time`。
屏幕板从 0x01 帧 byte 34-35 (= setpoint.fog_time) 同步 shadow, 一直读到 0 → SET_FOG 闪烁页显示 00。

### 修复方案
`Control/timers/control_timers.c::control_timers_start_fog` 启动/停止时同步更新 `setpoint.fog_time`:
```c
if (duration_sec == 0) {
    d->control.fog_remaining = 0;
    d->setpoint.fog_time = 0;          // 新增
    d->control.relay_status &= ~(1U << BSP_RELAY_WH_IO);
} else {
    if (!interlock_can_start_fogging(d)) return;
    d->control.fog_remaining = duration_sec;
    d->setpoint.fog_time = duration_sec;  // 新增
    d->control.relay_status |= (1U << BSP_RELAY_WH_IO);
}
```
`control_timers_start_disinfect` 同样改 (同步 `setpoint.disinfect_time`)。

---

## 三、烠录步骤

### 仅烠主板
```cmd
JLink V9 接主板 SWD (PA13/PA14/3V3/GND)
loadbin _post_stage8_redo_v2_20260507_143742/main_stage8_redo_v2_FINAL.bin 0x08000000
```
**屏幕板不动** (沿用 v1 烠录的 screen_stage8_redo_FINAL.bin, MD5 `112f4c0355359106c36272d5051bf9b6`)。

### 用 flash_main.bat
直接双击 `firmware/HDICU_MainBoard/flash_main.bat` (脚本会重新编译, 编译产物与本目录的 .bin 一致)。

---

## 四、烠录后测试矩阵

### T3 PE1 闪烁是否修复
1. 上电, 屏幕板进 SET_TEMP, 旋 +5 格 (设 setpoint = 30.5°C 之类)
2. 等加热启动
3. **观察 30 秒**:
   - PE1 LED **常亮** ✅ (修复成功)
   - PE1 LED 仍周期性闪 ❌ (NTC 物理问题更严重, 需检查传感器接线)

### T4 雾化倒计时是否修复
1. 屏幕板进 SET_FOG, 旋 +1 格 (1 分钟)
2. 等 5 秒超时, 屏幕回 LIVE
3. **再次进 SET_FOG**:
   - 显示 "01" (1 分钟) ✅ 修复成功
   - 显示 "00" ❌ 未修复

### T8 消毒倒计时同上

### 重要 dump 验证 (新偏移, ARM -fshort-enums)
| 字段 | 地址 | 修复后期望 |
|---|---|---|
| sensor.temperature[2] | 0x20000074 | 稳定温度值 (NTC 短路时**不再飙到 60°C+**) |
| setpoint.fog_time | 0x20000096 | 雾化运行时 = duration (例 0x003C=60) |
| setpoint.disinfect_time | 0x20000098 | 消毒同上 |
| **control.temp_state** | **0x200000A4** (1 byte) | 稳定 = 0x02 (HEATING), **不再周期切 0x01** |
| control.fog_remaining | 0x200000A8 | 雾化运行时倒数中 |
| control.relay_status | 0x200000AE | bit 0 (PE1) **稳定 1 在 HEATING 期间** |

---

## 五、未修复 (留 v3 或 Stage 9)

- `relay_status = 0x0F03` 高 bit 9-11 = 1 异常 (无对应 relay 但被错误置位). 不影响 GPIO 输出 (relay_driver_apply 只写 bit 0-8), 但来源未明, 待调查。
- T3 用户报告 "PB4 常亮" — 可能是 v1 测试中启动雾化的残留, v2 后**首次烠完应清零** (BSS 默认), 后续不会再异常 ON 除非用户主动启动。

---

## 六、回滚

如 v2 出新问题:
```cmd
loadbin _post_stage8_redo_20260507_121517/main_stage8_redo_FINAL.bin 0x08000000
```
回到 v1 (T3/T4/T8 仍有问题, 但其他模块行为已知)。

或回 Stage 7:
```cmd
loadbin _post_stage7_swj_fix_20260506_134251/main_stage7_FINAL.bin 0x08000000
loadbin _post_stage7_swj_fix_20260506_134251/screen_stage7_FINAL.bin 0x08000000
```

---

**本文档结束**.
