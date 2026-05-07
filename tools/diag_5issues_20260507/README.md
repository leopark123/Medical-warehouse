# HDICU 5 问题联合诊断 (2026-05-07)

针对 Stage 8 redo v2 烧录后报告的 5 个问题, 一次跑完拿全部 RAM 数据。

## 5 个问题速览
| # | 现象 | 关键字段 |
|---|---|---|
| 1 | 按 KEY7 (新风) 单击, 屏幕"供氧时间"toggle 0 ↔ 00:08:32 | `setpoint.fresh_air`, `control.o2_accumulated` |
| 2 | 内循环按键灯一直亮 (LEDA6 PA0) | `setpoint.inner_cycle`, `control.switch_status` |
| 3 | 氧气浓度一直 61% 不变 | `sensor.o2_valid`, `sensor.o2_raw` |
| 4 | 护理等级要 5 档 + 第 4 档双灯 | `setpoint.nursing_level` 循环行为 |
| 5 | 长按 KEY5 开放供氧, 计时从 00:25:30 起步, 不是 0 | `control.o2_accumulated`, `setpoint.open_o2` |

## 怎么跑

1. 把 v2 主板 bin 烧好 (`firmware/_post_stage8_redo_v2_20260507_143742/main_stage8_redo_v2_FINAL.bin`)
2. 主板上电, 屏幕板上电 (LIVE 页), 所有传感器接好
3. JLink V9 接主板 SWD (PA13/PA14/3V3/GND)
4. **双击 `diag_all.bat`**, 按提示 6 步操作:
   - **T0**: 上电 5 秒后, 不动任何按键, 按 Enter dump
   - **T1**: 按 KEY7 (新风) 第 1 次, 按 Enter dump
   - **T2**: 按 KEY7 第 2 次, 按 Enter dump
   - **T3**: 长按 KEY5 (开放供氧) 2 秒, 按 Enter dump
   - **T4**: 等 30 秒不动, 自动 dump (验证 o2_raw 是否变化 + o2_accumulated 是否在累加)
   - **T5**: 按 KEY1 (护理) 5 次, 按 Enter dump

## 它会输出什么

- `dump_T0.log` ~ `dump_T5.log` — 6 个 JLink mem 原始 hex 输出
- **`summary.txt`** — 所有字段并排对比, 6 列一目了然 (这个是要发回来分析的)

## 工具特性

- **单次 SWD 连接读完 24 个字段** — 不分批, 不会有"读到一半连接掉了"
- **每个 checkpoint 失败都能 R 重试** — 不用重头跑
- **JLink 命令脚本写在 `%TEMP%\hdicu_dump_all.txt`** — 避开中文路径编码坑
- **自动定位 JLink.exe** — D盘/C盘/x86 三个标准路径都试

## dump 出来的字段总表 (24 个)

| 地址 | 字段 | 大小 | 说明 |
|---|---|---|---|
| 0x20000074 | sensor.temperature[2] | 2B int16 | NTC 通道 2 (×10) |
| 0x20000078 | sensor.temperature_avg | 2B int16 | NTC 平均 (×10) |
| 0x2000007E | sensor.o2_percent | 1B | O2% 整数 |
| 0x20000080 | sensor.o2_raw | 2B | O2 原始 (×10) |
| 0x20000087 | sensor.o2_valid | 1B | bool |
| 0x2000008E | setpoint.target_temp | 2B | 目标温度 (×10) |
| 0x20000096 | setpoint.fog_time | 2B | 雾化时长 (秒) |
| 0x20000098 | setpoint.disinfect_time | 2B | 消毒时长 (秒) |
| 0x2000009A | setpoint.fan_speed | 1B | 0~3 |
| 0x2000009B | setpoint.nursing_level | 1B | 1~3 (frozen) |
| 0x2000009C | setpoint.inner_cycle | 1B | 0/1 |
| 0x2000009D | setpoint.fresh_air | 1B | 0/1 ← 问题 1 |
| 0x2000009E | setpoint.open_o2 | 1B | 0/1 ← 问题 5 |
| 0x2000009F | setpoint.light_ctrl | 1B | bit0~3 |
| 0x200000A4 | control.temp_state | 1B | 0/1/2 |
| 0x200000A8 | control.fog_remaining | 2B | 秒 |
| 0x200000AA | control.disinfect_remaining | 2B | 秒 |
| 0x200000AC | control.o2_accumulated | 2B | 秒 ← 问题 1+5 |
| 0x200000AE | control.relay_status | 2B | bitmap |
| 0x200000B0 | control.light_status | 1B | bit0~3 |
| 0x200000B1 | control.switch_status | 1B | bit0~2 ← 问题 2 |
| 0x200000B4 | control.fan_speed_actual | 1B | 0~3 |
| 0x200000B5 | control.nursing_level_actual | 1B | 1~3 ← 问题 4 |
| 0x200000B6 | alarm.alarm_flags | 2B | bitmap |

## 分析判定逻辑 (summary.txt 末尾自动给提示)

- **问题 1 验证**: T0 → T1 → T2 看 `o2_accumulated` 是否真在 0 ↔ 512 toggle
  - 如果 toggle: 这是真 RAM 问题, 必须深挖 (memory aliasing / 中断越界)
  - 如果不动: 屏幕显示 bug, 与按键真实写入无关
- **问题 2 验证**: T0 时 `setpoint.inner_cycle` 是 0 还是 1
  - = 0 而 LED 仍亮 → PA0 物理短路, 跟主板软件无关
  - = 1 → 软件被写过 (KEY6 抖动 / iPad 改的)
- **问题 3 验证**: T0 与 T4 (30 秒后) 比 `o2_raw`
  - 不变 → sensor 物理掉线 (UART4 PC10/PC11 / sensor 没电)
  - 变了 → sensor 工作, 软件还有别的 bug
- **问题 4 验证**: T5 后 `nursing_level_actual` = 1/2/3 中哪个 (5 次循环回到 1 或 2 即三档循环, 期望)
- **问题 5 验证**: T3 时 `o2_accumulated` 起始值
  - = 0 → 单次按下 OK, 是问题 1 的影响导致后面看到 25:30
  - != 0 → 累加器从未清, 这是设计语义问题
