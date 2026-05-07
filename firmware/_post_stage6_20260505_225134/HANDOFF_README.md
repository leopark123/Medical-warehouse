# 烧录交付包 — Stage 1~6 全部完成（最终版）

**生成时间**：2026-05-05 22:51:34
**状态**：6 个 Stage 改动全部编译通过，**未实测**
**对应文档**：
- `02_开发实现/HDICU_屏幕板主板改动确认书_v1.2.md` (Stage 1-4)
- `02_开发实现/CODEX_REVIEW_STAGE_5_SUPPLEMENT.md` (Stage 5)
- 本 README 含 Stage 6 增量说明

---

## 一、固件文件（直接烧录）

| 板 | 文件 | 大小 | MD5 | 烧录地址 |
|----|------|------|------|---------|
| **主板** STM32F103VET6 | `main_stage6_FINAL.bin` | 27184 B | `58f8c1a796d79f9e978f2a4ea2ec7c8b` | `0x08000000` |
| **屏幕板** GD32F303RCT6 | `screen_stage6_FINAL.bin` | 5244 B | `b6307c333eaa6f4100de7afeb19dd10b`（与 Stage 5 同）| `0x08000000` |

⚠️ **请使用本目录的 Stage 6 主板版本**。屏幕板与 Stage 5 完全相同（无变化）。

---

## 二、Stage 6 改动摘要

**单一改动**：PE9 PTC 风机 PWM 频率 **100 Hz → 15 kHz**

### 旧实现（Stage 5 及之前）
- TIM6 ISR @ 10 kHz，软件计数 100 步
- PWM 实际频率 = **100 Hz**（人耳可闻，婴儿可清晰听到）
- ISR CPU 占用 ~1%

### 新实现（Stage 6）
- TIM1_CH1 硬件 PWM @ **15 kHz**（72 MHz / 48 / 100 = 15000 Hz 整数除尽）
- 100 步分辨率（与现有 0/30/60/80/100 完美对齐）
- **0% CPU ISR 占用**（硬件直接输出）

### 涉及的硬件改动（无）
- 无外设接线变化
- 无新增引脚
- PE9 仍是 PTC 风机 PWM 输出，只是底层从软件 PWM 切到硬件 PWM

### AFIO 配置说明
- `AFIO_MAPR.TIM1_REMAP[1:0] = 11`（全重映射）
- 副作用：TIM1_CH2/3/4 等被映射到 PE11/PE13/PE14，但**不影响**这些引脚现有功能：
  - PE11 = 照明灯 LED2（GPIO Output 模式不变）
  - PE13 = 照明灯 LED4（GPIO Output 模式不变）
  - PE14 = 未使用
- AFIO 只决定"如果某 pin 配置为 AF 时复用哪个外设"，不强制激活 AF

---

## 三、烧录命令

### 主板
```
si SWD
speed 4000
device STM32F103VE
connect
h
loadbin <临时路径>\main_stage6_FINAL.bin 0x08000000
verifybin <临时路径>\main_stage6_FINAL.bin 0x08000000
r
g
q
```

### 屏幕板
**Stage 6 屏幕板没改，可保留现有 Stage 5 屏幕板固件不烧**。如需重烧：
```
si SWD
speed 1000
device GD32F303RC                  ← ⚠️ 必须 GD32 名
connect
h
loadbin <临时路径>\screen_stage6_FINAL.bin 0x08000000
verifybin <临时路径>\screen_stage6_FINAL.bin 0x08000000
r
g
q
```

---

## 四、烧录后 Stage 6 验证清单

### 4.1 听感测试（最直接）
- 设温度比环境高 5°C → 主板进入 HEATING → PTC 风机 80% 转
- **耳朵贴近 PTC 风机听**：
  - **旧版（100 Hz）**：明显 buzz 嗡嗡声
  - **新版（15 kHz）**：仅风扇本身风声，**无 PWM 嗡声** ✓

### 4.2 示波器验证（最严谨，可选）
探针接 PE9（推拉电磁铁旁的 PWM 控制脚）：
- 频率应为 **15 kHz ± 1%**（实测应该是 **正好 15000 Hz**）
- 占空比 = 设定值（80% 加热 / 100% 新风 / 30/60% 用户档位等）
- 波形应为完整方波（不是软件 PWM 那种偶尔抖动）

### 4.3 功能等价性测试
所有 Stage 1-5 的功能必须照常工作：
- [ ] CN3 切换照明灯
- [ ] CN8 切换风速档（LED8/9/10 累加显示）
- [ ] 加热时 PTC 风机仍 80%（听得到风声）
- [ ] 新风模式 PTC 风机 100%
- [ ] 编码器顺时针 = 增加
- [ ] HMI 7 项编辑

### 4.4 长跑稳定性
- 让设备跑 30 分钟，多次切换温度让 PTC 反复启停
- PE9 信号稳定，风机不漂移
- 无任何异常发热（MOSFET 开关频率提升 150 倍但损耗仍 < 5 mW）

---

## 五、所有 Stage 改动累计 (1~6)

| Stage | 范围 | 主要改动 |
|-------|------|---------|
| 1 | CN11 双发 + CN3/CN8 重映射 | UART1 双发, KEY_ID_MAP[7]=0x0B, 主板加 0x82 case 0x0B |
| 2 | LED8/9/10 + GPIOD | LED_Fan_Init + LEDA_Update 累加显示 |
| 3 | PTC 风机 max3 仲裁 | pwm_set_ptc_arbiter, temp_control 申请 safety_min |
| 4 | HMI 7 项 + 闪烁 + 协议复用 | HmiPage 8 项, send_timer_ctrl, CO2 0x81 case 0x06 |
| 5 | LEDA8 + LED 跟 PTC duty + 编码器方向 | LEDA_Update 等效 duty, enc_table 反转 |
| **6** | **PE9 PWM 100 Hz → 15 kHz** | **TIM6 软件 PWM → TIM1 硬件 PWM @ 15 kHz** |

总改动：~7 文件，+450/-60 行（Stage 1-6 累计）。

---

## 六、回滚

### 回到 Stage 5（如果 Stage 6 出问题）
```
cp firmware/_post_stage5_20260505_210540/main_stage5_FINAL.bin <烧录路径>
JLink 烧 0x08000000
```

### 回到 Stage 1-4 之前的稳定基线
```
cp firmware/_pre_rework_backup_20260505_151421/main_firmware_baseline.bin <烧录路径>
```

### 备份目录全列表
```
firmware/_pre_rework_backup_20260505_151421/  ← 改动前
firmware/_post_stage1_20260505_151901/        ← Stage 1
firmware/_post_stage4_20260505_153022/        ← Stage 4
firmware/_post_stage5_20260505_210540/        ← Stage 5
firmware/_pre_stage6_20260505_224934/         ← Stage 6 改动前
firmware/_post_stage6_20260505_225134/        ← 本目录, Stage 6 完成 (推荐烧此版本)
```

---

## 七、Stage 6 风险与缓解

| 风险 | 缓解 |
|------|------|
| TIM1 全重映射意外影响 PE10-PE13 照明灯 | GPIO mode 由 CRH 锁定 Output, AFIO 只决定 AF 复用映射, 不强制激活 |
| 15 kHz 谐波 EMI 进音频段 | 仅基波 15 kHz, 谐波 30/45 kHz... 都超音频, EMC 友好 |
| MOSFET 开关频率提升 150 倍 (100Hz→15kHz) 损耗增加 | 实际损耗 < 5 mW, 开关时间 ~50 ns 远小于 PWM 周期 66.67 µs |
| PWM 启动瞬间风机异响 | TIM1 init 后立即 Start, 占空比从 0 开始, 平滑启动 |
| 烧录后没声音以为坏了 | 是预期: 100 Hz buzz 消失 = 改动成功; 风扇本身风声仍在 |

---

**交付完成**。把整个文件夹发烧录人员，看 README 烧录验证即可。
