# Stage 8 redo v5 — 6 颗状态 LED (屏幕板侧)

## 烧录文件
- 主板: `main_stage8_redo_v4_3_FINAL.bin` (沿用 v4.3, MD5 `6a5997fdf5c3d690d97d8480b602c601`), **不需重烧**
- 屏幕板: `screen_stage8_redo_v5_FINAL.bin` (**MD5 `2d08e6f9458c985f689b4a7d97e0bf5c`**) ← **要烧屏幕板**

## v5 改动 (仅屏幕板)
在 `LEDA_Update` 增加 6 颗状态 LED, 数据源都来自主板 0x01 帧:

| 引脚 | 状态 LED | 触发条件 |
|---|---|---|
| PB11 | 压缩机/加热 | relay_status bit 0 (PTC) OR bit 1 (JIARE) OR bit 7 (YASUO) |
| PA11 | 加湿器 | relay_status bit 5 (JIASHI) |
| PA12 | O2 阀 | relay_status bit 4 (O2) |
| PC1 | 雾化 | relay_status bit 8 (WH) |
| PC14 | 消毒倒计时 | disinfect_remaining > 0 |
| PA1 | 供氧计时 | relay_status bit 4 (= 同 PA12, 阀开就亮) |

全部 active-low (跟现有 LEDA1-8 / LED8/9/10 一致): LOW=on / HIGH=off

## 主板协议不变
- 0x01 帧字段排布全部沿用 v4.3
- 主板 v4.3 bin 已包含所需数据 (relay_status / disinfect_remaining)
- 不需重编主板

## 烧录方式
```cmd
JLink V9 接屏幕板 SWD
> si SWD
> device GD32F303RC
> connect
> loadbin screen_stage8_redo_v5_FINAL.bin 0x08000000
> r
> g
> q
```

或双击 `firmware/HDICU_ScreenBoard/flash_screen.bat` (用 v5 bin 替换原文件先)

## 回滚 (出问题时)
回到 v1 屏幕板 (无新增 LED):
```cmd
loadbin firmware/_post_stage8_redo_v2_20260507_143742/screen_stage8_redo_FINAL.bin 0x08000000
```

主板**不需要**回滚。
