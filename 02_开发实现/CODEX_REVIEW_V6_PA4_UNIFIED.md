# Codex Review: v6 — PA4 作为唯一舱内温度 (主板侧)

请确认这次主板 SensorTask 改动是否实现了 "温度数据源全面统一" 的产品目标, 无副作用 / 边界问题 / 跟现有互锁/报警冲突。

---

## 0. 前情提要

你之前审过的链 (主板系列):
- v4.2 Codex Pass → v4.3 上线 → v4.4 (0x0C) revert → 维持 v4.3
- v5/v5.1 是屏幕板单边改动, 主板 v4.3 不变

**本次 v6** 是主板单边改动 (跟屏幕板 v5/v5.1 配套, 不冲突)。

## 1. 改动原因 — 你之前分析过的不一致

| 用途 | v4.3 旧 | v6 新 |
|---|---|---|
| 屏幕显示 | `temperature[2]` (PA4, 无校准) | **`temperature[2]` (PA4, 校准后)** |
| 温控决策 | `temperature[2]` (PA4, 无校准) | **`temperature[2]` (PA4, 校准后)** |
| iPad 上报 | `temperature_avg` (4 路平均 + 校准) | **`temperature_avg` (= PA4, 校准后)** |
| 温度报警 | `temperature_avg` (4 路平均 + 校准) | **`temperature_avg` (= PA4, 校准后)** |
| 校准应用对象 | 只 `temperature_avg` | **`temperature[2]` + 同步 `temperature_avg`** |

改后 4 个消费者全部看到**同一个校准后 PA4 值**。

## 2. 改动 diff

### 文件 1: `App/tasks/tasks.c` SensorTask 两处

**改动 1: NTC 数据填充**

```c
/* v4.3 旧 */
adc_driver_read_all(adc_vals);
d->sensor.temperature_avg = ntc_calc_average(adc_vals, d->sensor.temperature);

/* v6 新 */
adc_driver_read_all(adc_vals);
(void)ntc_calc_average(adc_vals, d->sensor.temperature);   /* 滤波仍跑, 4 路平均返回值忽略 */
d->sensor.temperature_avg = d->sensor.temperature[2];      /* v6: PA4 = 舱内温度 */
```

**改动 2: 校准应用**

```c
/* v4.3 旧 */
if (d->sensor.temperature_avg != -999) {
    int32_t t = (int32_t)d->sensor.temperature_avg + d->calibration.temp;
    if (t < -999) t = -999;
    if (t > 800) t = 800;
    d->sensor.temperature_avg = (int16_t)t;
}

/* v6 新 */
if (d->sensor.temperature[2] != -999) {
    int32_t t = (int32_t)d->sensor.temperature[2] + d->calibration.temp;
    if (t < -999) t = -999;
    if (t > 800) t = 800;
    d->sensor.temperature[2] = (int16_t)t;        /* PA4 加校准 */
    d->sensor.temperature_avg = (int16_t)t;       /* 兼容字段, 内容 = 校准后 PA4 */
}
```

### 文件 2: `App/data/app_data.h` 注释 (字段语义重定义)

```c
int16_t  temperature[4];    /* temperature[2] = PA4 = 舱内温度 (校准后)
                             * temperature[0/1/3] = 备用通道, 无校准, 未必接探头 */
int16_t  temperature_avg;   /* v6 重定义: = temperature[2] (PA4 舱内温度, 校准后).
                             * 字段名保留兼容旧协议. */
```

## 3. 改动后所有消费者行为对照

| 消费者 | 读哪个字段 | v4.3 看到 | v6 看到 |
|---|---|---|---|
| 屏幕板 0x01 帧 | `temperature[2]` | PA4 无校准 | PA4 + 校准 |
| 温控 `temp_control.c:53` | `temperature[2]` | PA4 无校准 | PA4 + 校准 |
| iPad 0x02 帧 | `temperature_avg` | 4 路平均 + 校准 | PA4 + 校准 |
| 报警 `tasks.c:347` | `temperature_avg` | 4 路平均 + 校准 | PA4 + 校准 |
| `sensor_sanity_check_temp` | `temperature_avg` | 4 路平均 + 校准 | PA4 + 校准 |
| 调试输出 `tasks.c:689` | `temperature_avg` | 4 路平均 + 校准 | PA4 + 校准 |

**所有 6 个消费者最终看到同一个 PA4 校准后值** ✓

## 4. 不变化 (回归基线)

- `ntc_calc_average` 函数: 仍跑, 仍算 4 路滤波填 `temperature[0..3]`. 返回值 (4 路平均) 不用而已
- `temperature[0/1/3]`: 仍按 PA0/PA1/PA5 ADC 读 + 滤波. 物理未必接探头, 但代码逻辑保留 (将来加多探头支持)
- 协议层 (0x01 / 0x02 帧): 字段偏移不变, 仅 byte 0-1 内容含义统一
- 屏幕板代码: 完全未动, 沿用 v5.1
- 校准协议 (iPad 0x03 byte 22-23 写 calibration.temp): 完全不变
- v2/v3/v4/v4.1/v4.2/v4.3 所有修复: 不动

## 5. 给 Codex 的审查点

### A. 校准方向正确性
v4.3 校准加到 `_avg` (4 路平均). v6 改加到 `temperature[2]` 后同步 `_avg`. 校准的物理意义是"探头读数偏移", 加到 PA4 上**比加到平均上更合理** (因为 PA4 才是真实探头)。

### B. -999 invalid 边界
- `ntc_calc_average` 如果 PA4 通道 `temperature[2]` 单点全 invalid → 滤波后 `temperature[2] = -999`
- v6 校准代码 `if (d->sensor.temperature[2] != -999)` 守卫成立, 不会把 -999 加校准
- `_avg = temperature[2]` 也 = -999, 一致传播 ✓
- 温控 / 报警 / sensor_sanity 看到 `_avg = -999` 时跟 v4.3 行为一致 (停加热、不报警等)

### C. ntc_calc_average 返回值忽略
- `(void)ntc_calc_average(...)` 显式 cast, 表明意图
- 返回的 4 路平均不再用, 但函数内部行为不变 (仍写 `temperature[0..3]`)

### D. 协议字段语义保留
- `temperature_avg` 字段在 iPad 0x02 帧 byte 0-1 / 屏幕板 0x01 帧 byte 0-1 (实际屏幕板用 `temperature[2]` 不用 `_avg`) 仍存在, 仅内容含义改
- APP 端解析无需改 (字段位置 + 类型不变, 值会跟屏幕显示一致)

### E. 校准下界
v6 校准代码: `if (t < -999) t = -999`. 但 -999 是 invalid 标志, 跟"有效校准结果"撞值。如果校准让结果刚好 = -999 (例如 PA4 read = 25.0°C + calibration.temp = -274.9 → 让 t = -2499 → clamp 到 -999), 会被下游误识别为 invalid。

但实际 `calibration.temp` 范围是 ±500 (±50°C), `temperature[2]` 范围 v2 已限到 [-400, 600] (±40~60°C), 加起来:
- 最低: -400 + (-500) = -900 → clamp 到 -999 ⚠️ 但 v2 没把 -999 这个值排除
- 最高: 600 + 500 = 1100 → clamp 到 800 ✓

下界 -999 可能跟 invalid 撞。但实际场景 NTC < -40°C + 用户校准 -50°C 不太可能, 风险低。是否要把下界 clamp 改成 -998 来避免歧义?

### F. 屏幕板 / 温控行为变化的副作用
现在屏幕板和温控也会看到校准后温度。如果用户:
- 校准 +0.5°C, 设 target = 25.0°C, 实际探头 24.5°C
- 主板 `temperature[2]` = 24.5 + 0.5 = 25.0
- 温控看 25.0 == 25.0, 不动作
- 实际探头物理 24.5°C — 但因为校准, 主板"以为"是 25.0°C

这是校准的本意 (修正传感器偏移让读数准), 还是产品想要"校准只影响显示, 不影响温控"? 跟产品方需求对齐。

**默认 calibration.temp = 0**, 实际不影响。除非用户主动校准, 否则没影响。

### G. 性能
v4.3: `temperature_avg = ntc_calc_average(...)` — 直接赋值
v6: `(void)ntc_calc_average(...)` + 2 句赋值 — 多了 1 句赋值。CPU 开销可忽略 (100ms 周期跑一次)。

### H. v4 系列修复回归
- T3 PE1 闪烁: 看 temperature[2] 的滤波后值, 跟 v4.3 一样
- T4/T8: 不受温度影响
- KEY5 长按等屏幕板按键: 不受影响

## 6. 编译产物

- 主板 bin: `firmware/_post_stage8_redo_v6_pa4_unified_*/main_stage8_redo_v6_FINAL.bin`
- **MD5**: `35f712c163877f344012a6536f581789`
- text 27784 (+16 vs v4.3), data 104, bss 37880, g_app_data 116B 不变
- 屏幕板 bin 沿用 v5.1: `1ab1e527...`
- git commit `3b9cb8e`, tag `stage8-redo-v6-pa4-unified` (已 push)

## 7. 测试矩阵 (Codex Pass 后用户烧)

### v6 行为验证
- [ ] 默认 calibration.temp = 0:
  - 屏幕显示温度 == iPad 0x02 byte 0-1 / 10.0 (= 同一个数字)
  - 温控 / 报警 按同一个数字判
- [ ] 设 calibration.temp = +5 (= +0.5°C, iPad 0x03 byte 22-23 写 0x00 0x05):
  - 屏幕显示**也**加 0.5°C (v4.3 时屏幕不加)
  - iPad 显示加 0.5°C (跟屏幕一致)
  - 温控按校准后温度判
- [ ] PA4 物理掉线 (-999):
  - `temperature[2] = -999`, `temperature_avg = -999`
  - 屏幕 GRID 0-2 显示 dash 或 999 (按现有逻辑)
  - iPad 收到 `0xFC 0x19` (= -999)
  - 温控停加热制冷 (safety)
  - 报警: `enable_temp_ctrl=1` 但 `_avg = -999` 时 not flagged (跟 v4.3 一致)

### v5.1 屏幕板 + v4 系列回归
- [ ] 6 颗状态 LED (PB11/PA11/PA12/PC1/PC14/PA1) 全部工作
- [ ] SET_* 长按一律清 enable
- [ ] 5 档护理 / OPEN_O2 纯氧隔离 / KEY5 长按重新供氧从 0

## 8. 回应格式

```
[整体] Pass / Concern / Block

A. 校准方向: ...
B. -999 invalid 边界: ...
C. ntc_calc_average 返回忽略: ...
D. 协议字段语义保留: ...
E. 校准下界 clamp 跟 -999 撞: ...
F. 屏幕/温控加校准的副作用: ...
G. 性能: ...
H. v4 修复回归: ...

[是否建议烧录 v6 主板 + v5.1 屏幕板]: Y / N
```

Pass 后用户拿到板子时, 主板烧 v6 bin (`35f712c1...`), 屏幕板烧 v5.1 bin (`1ab1e527...`)。
