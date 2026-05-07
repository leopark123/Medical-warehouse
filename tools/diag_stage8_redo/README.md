# Stage 8 重做诊断包

**适用固件**: `_post_stage8_redo_20260507_121517/main_stage8_redo_FINAL.bin`
(MD5 `e217b7ce3d8c3347300182401163351d`, 主板烠完后用这套诊断脚本)

**作用**: 不修改代码, 通过 JLink 实时读主板 RAM 验证 3 个问题的根因。

---

## 关键 RAM 地址表 (g_app_data @ 0x20000070, Stage 8 重做版本)

| 地址 | 字段 | 类型 | 关键性 | 期望值 |
|---|---|---|---|---|
| 0x20000074 | sensor.temperature[2] (PA4 顶部) | int16 x10 | ⭐ T3 | 稳定 200~300 (20~30°C) |
| 0x20000078 | sensor.temperature_avg | int16 x10 | | 同上 |
| 0x2000007E | sensor.o2_percent | uint8 | | 21~100 (有 O2 sensor) 或 0 (sensor 离线) |
| 0x20000080 | sensor.o2_raw | uint16 x10 | | 同 o2_percent x10 |
| 0x20000087 | sensor.o2_valid | bool | | 1 (sensor 在线) 或 0 (离线) |
| 0x2000008E | setpoint.target_temp | uint16 x10 | | 旋转后改变, 例 250 → 270 |
| 0x20000096 | setpoint.fog_time | uint16 sec | ⭐ T4 | **当前 bug**: 一直 = 0; 修复后 = 旋转设的秒数 |
| 0x20000098 | setpoint.disinfect_time | uint16 sec | ⭐ T8 | 同上 |
| 0x2000009E | setpoint.open_o2 | uint8 | | 0/1 |
| 0x200000A0 | setpoint.enable_temp_ctrl | uint8 | ⭐ T3 | 上电默认 0, 旋转后稳定 1, **不应反复 0/1** |
| 0x200000A1 | setpoint.enable_humid_ctrl | uint8 | | 同上 |
| 0x200000A2 | setpoint.enable_o2_ctrl | uint8 | | 同上 |
| 0x200000A4 | control.temp_state (enum 4 字节) | uint32 | ⭐ T3 | 0=IDLE, 1=COOLING, 2=HEATING; **不应反复变** |
| 0x200000B0 | control.fog_remaining | uint16 sec | ⭐ T4 | 雾化启动后倒计时, 数值 < setpoint.fog_time 当前(bug) |
| 0x200000B2 | control.disinfect_remaining | uint16 sec | | 同上 |
| 0x200000B4 | control.o2_accumulated | uint16 sec | | 长按 LIVE 清供氧时 = 0, 否则 open_o2=1 时每秒+1 |
| 0x200000B6 | control.relay_status | uint16 bitmap | ⭐ T3 | bit 0=PTC_IO/PE1 (heating), bit 8=WH_IO/PB4 (雾化) |
| 0x200000B8 | control.light_status | uint8 | | bit0=检查 bit1=照明 bit2=蓝 bit3=红 |
| 0x200000B9 | control.switch_status | uint8 | | bit0=内循环 bit1=新风 bit2=open_o2 |
| 0x200000BC | control.fan_speed_actual | uint8 | | 0~3 (用户按风速键设的档位) |
| 0x200000C0 | alarm.alarm_flags | uint16 | | bitmap, 0x40=COMM_FAULT |

### relay_status bit 解读

| bit | 名称 | GPIO |
|---|---|---|
| 0 | BSP_RELAY_PTC_IO | **PE1** (PTC 加热器) |
| 1 | BSP_RELAY_JIARE_IO | PE0 (底部加热) |
| 2 | BSP_RELAY_RED_IO | PB9 (红外灯) |
| 3 | BSP_RELAY_ZIY_IO | PB8 (紫外灯/消毒) |
| 4 | BSP_RELAY_O2_IO | PB7 (O2 阀) |
| 5 | BSP_RELAY_JIASHI_IO | PE4 (加湿) |
| 6 | BSP_RELAY_FENGJI_IO | PE3 (空调外风机) |
| 7 | BSP_RELAY_YASUO_IO | PE2 (压缩机) |
| 8 | BSP_RELAY_WH_IO | **PB4** (雾化器) |

---

## 诊断脚本

### 脚本 1: `diag_fog_disinfect.bat` — 验证 T4/T8 (1 次采样)

**操作**:
1. JLink 连主板 SWD
2. 屏幕板进 SET_FOG, 旋转 +1 格 (设 1 分钟)
3. 立即双击运行 .bat
4. 看 `dump_fog.log` 中:
   - `0x20000096` (setpoint.fog_time): 期望 0x003C (60s), 实际 = 0 → **确认 T4 根因**
   - `0x200000B0` (fog_remaining): 应该 = 60 倒计时中
   - `0x200000B6` bit 8: 应 = 1 (PB4 ON)

### 脚本 2: `diag_pe1_flicker.bat` — 验证 T3 PE1 闪烁 (10 次采样, 总 ~10 秒)

**操作**:
1. JLink 连主板 SWD
2. 屏幕板单击进 SET_TEMP, 旋转 +5 格让加热启动
3. 看到 PE1 (或风速 LED) 真在闪烁后, 双击运行 .bat
4. 等 10 秒, 生成 `dump_1.log` ~ `dump_10.log`
5. **打包所有 log 发给固件工程师**

**判读**:
- 如果 `enable_temp_ctrl` (0x200000A0) 在 10 个 log 里都是 1 → 不是 enable bug
- 如果 `temp_state` (0x200000A4) 在 10 个 log 里有的 0 (IDLE) 有的 2 (HEATING) → temp_state 真在闪 (NTC 滤波 + 滞环临界震荡)
- 如果 `relay_status` (0x200000B6) bit 0 在 10 个 log 反复 0/1 → PE1 真在闪 (确认现象)
- 如果 `temperature[2]` (0x20000074) 反复 -999 (0xFC19) ↔ 真实值 → NTC 物理或滤波问题

---

## 数值阅读 (JLink mem 输出格式)

JLink 输出格式:
```
0x20000074 = 0x00 0xF4 0x01 0x00
```
小端序, 含义按字段类型:
- **uint8** (1 字节): 直接读第 1 字节 (例 `0x00 0xF4 ...` → 字段值 0x00)
- **uint16** (2 字节): 小端组合 (例 `0x00 0xF4` → 0xF400; 但温度 -999 = 0xFC19 显示为 `0x19 0xFC`)
- **enum** (4 字节): 小端 (例 `0x02 0x00 0x00 0x00` → temp_state = 2 = HEATING)

特殊值:
- temperature -999 (0.1°C 单位) 表示 sensor invalid/未连接
- temp_state: 0=IDLE, 1=COOLING, 2=HEATING

---

## 把诊断结果发回的格式

```
=== T4 雾化诊断 ===
旋转 SET_FOG +1 格 (1 分钟) 后:
0x20000096 = [paste here]   (setpoint.fog_time)
0x200000B0 = [paste here]   (fog_remaining)
0x200000B6 = [paste here]   (relay_status, 看 bit 8)

=== T3 PE1 闪烁诊断 ===
看到 PE1 闪后 10 次采样, 关键字段:
sample 1:  temp_state=?, enable_temp=?, relay_status=?, temperature[2]=?
sample 2:  ...
...
sample 10: ...
```

或更简单: 直接把 `dump_*.log` 文件打包发回。

---

## 故障排查

### JLink 找不到 .exe
修改 .bat 中 `JLINK=` 路径, 指向你机器上的 JLink.exe

### "Cannot connect to target"
- 检查 SWD 接线 (PA13=SWDIO, PA14=SWCLK, GND, 3V3)
- 主板上电
- 检查 `device STM32F103VE` 是否正确

### dump_*.log 是空的
脚本 timeout 太快 → 改成 `timeout /t 2` (2 秒间隔)

---

**文档结束**.
