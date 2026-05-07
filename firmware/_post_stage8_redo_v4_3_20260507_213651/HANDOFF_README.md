# Stage 8 redo v4.3 — 上电护理灯全灭

**唯一变化**: 上电默认 nursing_level 从 1 改成 0, 护理 LED 全灭 (PB0/PB1/PC5)

## 烧录文件
- 主板: `main_stage8_redo_v4_3_FINAL.bin` (27872 字节, MD5 `6a5997fdf5c3d690d97d8480b602c601`)
- 屏幕板: `screen_stage8_redo_FINAL.bin` (沿用 v1, MD5 `112f4c0355359106c36272d5051bf9b6`, **不需重烧**)

## 演进
- v4.2: `7eb429d48b6ecadc0bc923f1c71ac958` (Codex P1 全修)
- **v4.3: `6a5997fdf5c3d690d97d8480b602c601` ← 当前**, 改护理上电默认值

## 变化点
1. `app_data.c`: setpoint.nursing_level 1 → 0; control.nursing_level_actual 1 → 0
2. `ipad_protocol.c`: factory reset (0x0B) nursing 1 → 0 (跟上电默认一致)

## 新行为
- 上电后护理 LED **全灭**
- 用户按 KEY1 一次 → 进档 1 (PB0 亮)
- KEY1 循环: 0 (灭) → 1 (PB0) → 2 (PB1) → 3 (PC5) → 4 (PB1+PC5) → 0
- iPad 0x0B 出厂复位 → 也回到 0 档全灭
