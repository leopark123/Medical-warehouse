# Stage 8 redo v6 — PA4 作为唯一舱内温度 (主板)

## 烧录文件
- 主板: `main_stage8_redo_v6_FINAL.bin` (**MD5 `35f712c163877f344012a6536f581789`**) ← 要烧主板
- 屏幕板: `screen_stage8_redo_v5_1_FINAL.bin` (沿用 v5.1, MD5 `1ab1e527105fdaf70b58c47741630a6c`) ← 要烧屏幕板

## v6 改动 (仅主板, 1 处函数, 2 文件注释)

### 问题
之前 (v4.3) 温度数据源不统一:
- 屏幕显示 / 温控用 `temperature[2]` (PA4 单路, 无校准)
- iPad 上报 / 报警用 `temperature_avg` (4 路平均 + calibration 偏移)
- 校准只加到 `_avg`, 屏幕和温控看不到校准

### 改动
`SensorTask` 把 `temperature_avg` 重新定义为 "PA4 校准后舱内温度":
1. 读 ADC + Steinhart-Hart + 4 路滤波 (仍跑, 算 `temperature[0..3]`)
2. `temperature_avg = temperature[2]` (PA4 = 唯一舱内温度)
3. 校准加到 `temperature[2]`, 同步 `temperature_avg`

结果: 屏幕 / iPad / 温控 / 报警 4 个消费者都看到**同一个校准后 PA4 值**, 数据完全一致。

## 烧录方式

**主板 + 屏幕板都要烧** (主板 v6, 屏幕板 v5.1):

```cmd
JLink V9 接主板 SWD
> si SWD
> device STM32F103VE
> connect
> loadbin main_stage8_redo_v6_FINAL.bin 0x08000000
> r
> g
> q
```

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

## 包含的所有修复 (回归基线)
- v2 修复: NTC 范围检查 / T4/T8 雾化消毒分钟显示
- v3 修复: KEY5/KEY6 长按防误触发 / 开放供氧从 0 起步
- v4 修复: 5 档护理 / OPEN_O2 纯氧隔离 / 计时口径
- v4.1 修复: Codex 3 Block (PTC race / iPad OOB / O2 全路径互锁)
- v4.2 修复: Codex 2 P1 (UV vs auto O2 / iPad 互斥归一化)
- v4.3: 上电护理灯全灭
- v5: 6 颗状态 LED (屏幕板)
- v5.1: SET_* 长按一律清 enable (屏幕板)
- **v6**: PA4 作为唯一舱内温度 (主板) ← 本次

## 回滚
```cmd
# 主板回 v4.3 (温度 4 路平均)
loadbin firmware/_post_stage8_redo_v4_3_20260507_213651/main_stage8_redo_v4_3_FINAL.bin 0x08000000

# 屏幕板回 v1 (无 6 LED)
loadbin firmware/_post_stage8_redo_v2_20260507_143742/screen_stage8_redo_FINAL.bin 0x08000000
```
