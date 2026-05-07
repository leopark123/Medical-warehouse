# Pre-Rework Backup Manifest

**备份时间**：2026-05-05 15:14:21
**目的**：在以下改造前建立完整回滚锚点
- 屏幕板编码器扩展（HMI 7 项 + 闪烁）
- CN8 改风速档位键 + LED8/9/10 三档显示
- 风速 B 方案（PTC 风机真三档 PWM + 三方 max 仲裁）
- CN11 双发恢复
- 主板 0x81 加 ParamID 0x06 (CO2)

**对应文档**：`02_开发实现/HDICU_屏幕板主板改动确认书_v1.2.md`

---

## 1. Git 锚点

```
git tag: pre-encoder-rework
HEAD:    4e14d63 chore: 工作树清理 + tools/jlink/ 调试脚本归档 + iPad APP 协议指南
```

回滚方式：
```bash
# 恢复某个文件:
git checkout pre-encoder-rework -- firmware/HDICU_ScreenBoard/main.c

# 完整回到这个 commit (会丢弃后续所有改动!):
git reset --hard pre-encoder-rework
```

---

## 2. 源码备份（待改 8 个文件）

| 备份文件 | 原路径 | 用途 |
|---------|--------|------|
| `screen_main.c` | firmware/HDICU_ScreenBoard/main.c | 屏幕板主程序（HMI/UART/按键/LED）|
| `uart_driver.c` | firmware/HDICU_MainBoard/Drivers/uart/uart_driver.c | 主板 UART 驱动（已含 v2 fix）|
| `uart_driver.h` | firmware/HDICU_MainBoard/Drivers/uart/uart_driver.h | UART 头文件 |
| `pwm_driver.c` | firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.c | 待改：风速三档 + 仲裁 |
| `pwm_driver.h` | firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.h | PWM 头文件 |
| `temp_control.c` | firmware/HDICU_MainBoard/Control/temp/temp_control.c | 待改：申请 safety_min 而非直接 80% |
| `screen_protocol.c` | firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c | 待改：加 0x82 case 0x0B + 0x81 case 0x06 |
| `tasks.c` | firmware/HDICU_MainBoard/App/tasks/tasks.c | 待改：删 fresh_air 直接覆盖，统一调仲裁 |

---

## 3. 当前烧录的固件备份

| 备份文件 | MD5 | 大小 | 来源 |
|---------|-----|------|------|
| `main_firmware_baseline.bin` | `c9671b1b133ec5477ec5898b9d3d9579` | 26944 B | firmware/HDICU_MainBoard/build/firmware.bin (P1 UART fix v2 已烧)|
| `main_firmware_baseline.elf` | — | 61168 B | 同上 .elf 调试符号 |
| `screen_fw_baseline.bin` | `bf88a0667e73f7a1a4089164110b8bd0` | 4688 B | firmware/HDICU_ScreenBoard/build/screen_fw.bin |

历史固件备份（之前已存）：
| 备份 | MD5 | 说明 |
|------|-----|------|
| `firmware/HDICU_MainBoard/backups/firmware_20260428_120252_pre_uart_fix.bin` | 5dfd8f74 | UART fix 之前 |
| `firmware/HDICU_MainBoard/backups/firmware_20260428_120540_uart_fix.bin` | d4b2b5f5 | UART fix v1 (修了错路径) |
| `firmware/HDICU_MainBoard/backups/firmware_20260428_123927_uart_fix_v2.bin` | c9671b1b | UART fix v2 = 当前主板上 |

---

## 4. 应急回滚步骤

### 场景 A: 改了一两个文件想回退
```bash
# 用 git
git checkout pre-encoder-rework -- firmware/HDICU_ScreenBoard/main.c
# 或用本地备份
cp firmware/_pre_rework_backup_20260505_151421/screen_main.c firmware/HDICU_ScreenBoard/main.c
```

### 场景 B: 整个改造放弃，回到稳定基线
```bash
# 慎用!! 会丢弃所有未 commit 改动
git reset --hard pre-encoder-rework
```

### 场景 C: 烧到板子的新固件出问题，回滚固件
```bash
# 主板 (用 backup 里的或 backups/ 里的 v2)
cp firmware/_pre_rework_backup_20260505_151421/main_firmware_baseline.bin <烧录路径>
# JLink 烧 0x08000000

# 屏幕板
cp firmware/_pre_rework_backup_20260505_151421/screen_fw_baseline.bin <烧录路径>
# JLink 烧 0x08000000 (屏幕板 GD32, device GD32F303RC, 注意不要用 STM32 名)
```

---

## 5. 实施阶段（参考确认书 v1.2 第七节）

| Stage | 范围 | 备份点 |
|-------|------|--------|
| 1 | CN11 双发 + CN3/CN8 重映射 | 完成后再做一次 backup `_post_stage1_<时间戳>` |
| 2 | LED8/9/10 + GPIOD | 完成后 `_post_stage2_<时间戳>` |
| 3 | PTC 三方仲裁（**安全关键**） | 完成后 `_post_stage3_<时间戳>` |
| 4 | HMI 7 项 + 闪烁 + 协议复用 | 完成后 `_post_stage4_<时间戳>` |

每阶段单独编译 + 烧录 + 实测 + 备份，不通过的话用上一 Stage 备份回滚。

---

**备份完整性已校验**（md5 比对源 = 备份）。
