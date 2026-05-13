# Stage 8 redo v6.1 — Codex Concern 修订: PA4 校准顺序

## 烧录文件
- 主板: `main_stage8_redo_v6_1_FINAL.bin` (**MD5 `79c6f6ea7d10c62efcc3f20157633f03`**) ← 替换 v6 bin
- 屏幕板: `screen_stage8_redo_v5_1_FINAL.bin` (沿用 v5.1, MD5 `1ab1e527105fdaf70b58c47741630a6c`)

## v6.1 改动 (仅主板, 1 个文件)

### Codex 在 v6 审查中指出的 Concern
`SensorTask` 顺序问题:
1. 读 NTC → 写**未校准** PA4 到 `temperature[2]`
2. `temperature_avg = temperature[2]` (此时也是未校准)
3. 读 CO2 / O2 / JFC / 输入 (有几十~几百微秒)
4. 再加校准 → `temperature[2]` 和 `temperature_avg` 同步更新

这个 1→4 中间窗口期, 同 High 优先级的 ControlTask (200ms) / AlarmTask (100ms) 如果抢占
SensorTask, 会读到"未校准 PA4". 概率低但存在.

### v6.1 修订
把 PA4 校准从原来的位置 (CO2/O2/JFC 读完后) 提到 `ntc_calc_average()` 后立即执行:

```c
adc_driver_read_all(adc_vals);
(void)ntc_calc_average(adc_vals, d->sensor.temperature);   /* 滤波仍跑 */

/* v6.1: 校准紧跟滤波后立即完成, 不留窗口 */
if (d->sensor.temperature[2] != -999) {
    int32_t t = (int32_t)d->sensor.temperature[2] + d->calibration.temp;
    if (t < -999) t = -999;
    if (t > 800) t = 800;
    d->sensor.temperature[2] = (int16_t)t;
    d->sensor.temperature_avg = (int16_t)t;
} else {
    d->sensor.temperature_avg = -999;
}

/* CO2/O2/JFC/... 在校准之后才读 */
```

写入 `temperature[2]` / `temperature_avg` 的瞬间就是**校准后**值, 任何高优先级任务任何时刻读
都不会看到"未校准 PA4". 校准变成 "atomic 块" 而不是跨数据采集的两阶段.

### 同步删除
原来下方校准块中的温度部分 (line 121-128 in v6) 被删除, 只剩 humid/o2/co2.

## v6 → v6.1 编译结果对照

| 项 | v6 | v6.1 | 变化 |
|---|---|---|---|
| text | 27784 | 27768 | -16 (重排后消除一次冗余赋值) |
| data | 104 | 104 | 0 |
| bss | 37880 | 37880 | 0 |
| `g_app_data` 地址 | 0x20000070 | 0x20000070 | 0 (相同) |
| `app_data_get/init/lock/unlock` 地址 | 0x08000a78/a80/ad8/adc | 同 | 0 |
| bin MD5 | 35f712c1... | **79c6f6ea7d10c62efcc3f20157633f03** | 改 |

**数据布局零变化, 协议/JLink dump 工具完全兼容.**

## 烧录方式

主板 (用 v6.1 替换之前的 v6 bin):
```cmd
JLink V9 接主板 SWD
> si SWD
> device STM32F103VE
> connect
> loadbin main_stage8_redo_v6_1_FINAL.bin 0x08000000
> r
> g
> q
```

屏幕板 (v5.1 沿用, 不变):
```cmd
JLink V9 接屏幕板 SWD
> si SWD
> device GD32F303RC
> connect
> loadbin screen_stage8_redo_v5_1_FINAL.bin 0x08000000
> r
> g
> q
```

## 测试要点

v6.1 行为应该跟 v6 完全一致 (Codex 只是修了实现细节, 没改语义):
- 屏幕显示温度 = iPad 0x02 byte 0-1 / 10.0 = 温控判温 = 报警判温 = PA4 校准后值
- calibration.temp = 0 时: 屏幕 / iPad 都显示纯 PA4 读数
- calibration.temp = +5 (= +0.5°C) 时: 屏幕和 iPad 都加 0.5°C
- PA4 物理掉线 → -999, temperature_avg 也 = -999, 控制/报警跟 v4.3 一致

只是消除了 "未校准 PA4 短暂可见" 这个理论上的竞态.

## 依赖回归基线
所有 v2 / v3 / v4.x / v5 / v5.1 / v6 修复, 不动. v6.1 只改 `tasks.c` 一个文件.

## Git 状态
- commit: (见 git log)
- tag: `stage8-redo-v6.1-calib-order`
- 已 push 至 origin/main
