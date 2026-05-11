# Stage 8 redo v5.1 — 撤销时一律关闭对应控制闭环

## 烧录文件
- 主板: `main_stage8_redo_v4_3_FINAL.bin` (沿用 v4.3, MD5 `6a5997fdf5c3d690d97d8480b602c601`), **不需重烧**
- 屏幕板: `screen_stage8_redo_v5_1_FINAL.bin` (**MD5 `1ab1e527105fdaf70b58c47741630a6c`**) ← 要烧屏幕板

## v5.1 改动 (基于 v5, 仅屏幕板)

### 行为变化
| 场景 | v5 (旧) | v5.1 (新) |
|---|---|---|
| 进 SET_TEMP 页前**温控关着**, 调温后长按撤销 | 回滚 target + 清 enable_temp_ctrl (温控关) | 同 |
| 进 SET_TEMP 页前**温控已开**, 调温后长按撤销 | **只回滚 target, 温控仍开** | **回滚 target + 一律清 enable_temp_ctrl (温控关)** |

湿控 (SET_HUMID) 和 O2 控制 (SET_O2) 同步改, 行为一致.

### 副作用
- 用户调温度时手抖长按 → 温控会被意外关闭
- 关闭后想重开: 重新单击编码器进 SET_TEMP, 旋转新值 → 主板自动 enable=1

### 实现
`firmware/HDICU_ScreenBoard/main.c::hmi_revert_current_set_page`:
- 删除 3 处 `if (s_orig_enable_xxx == 0)` 条件
- 改为一律 `send_key_action(0x0C/0x0D/0x0E, 0x02)` 清对应 enable

主板代码 (case 0x0C/0x0D/0x0E action=0x02) 不动, 已存在并正确清 enable.

## 包含的所有 v5 功能 (回归基线)
- 6 颗状态 LED (PA1/PA11/PA12/PB11/PC1/PC14)
- 所有 v4.3 修复

## 烧录命令
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

## 回滚
回 v5 (撤销条件清 enable):
```cmd
loadbin firmware/_post_stage8_redo_v5_6leds_20260511_205157/screen_stage8_redo_v5_FINAL.bin 0x08000000
```
回 v1 (无 6 LED, 老行为):
```cmd
loadbin firmware/_post_stage8_redo_v2_20260507_143742/screen_stage8_redo_FINAL.bin 0x08000000
```

主板**不需要**重烧或回滚.
