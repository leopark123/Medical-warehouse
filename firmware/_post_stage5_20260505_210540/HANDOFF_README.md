# 烧录交付包 — Stage 1~5 全部完成（最终版，覆盖之前 Stage 1~4 包）

**生成时间**：2026-05-05 21:05:40
**状态**：5 个 Stage 改动全部编译通过，**未实测**（待烧录人员验证）
**对应文档**：
- `02_开发实现/HDICU_屏幕板主板改动确认书_v1.2.md`（Stage 1-4 设计）
- 本 README 含 Stage 5 增量说明

---

## 一、固件文件（直接烧录）

| 板 | 文件 | 大小 | MD5 | 烧录地址 |
|----|------|------|------|---------|
| **主板** STM32F103VET6 | `main_stage5_FINAL.bin` | 26960 B | `c36fad37a3e4a12e4af56c29d1ebd0f1` | `0x08000000` |
| **屏幕板** GD32F303RCT6 | `screen_stage5_FINAL.bin` | 5244 B | `b6307c333eaa6f4100de7afeb19dd10b` | `0x08000000` |

ELF 文件附带（含调试符号，烧录可不用）：
- `main_stage5_FINAL.elf`
- `screen_stage5_FINAL.elf`

⚠️ **请使用本目录的 Stage 5 版本，不要用 Stage 4 版本**（已更新解决 5 项后续需求）。

---

## 二、烧录命令（同 Stage 4 包一致）

### 主板
```
si SWD
speed 4000
device STM32F103VE
connect
h
loadbin <临时路径>\main_stage5_FINAL.bin 0x08000000
verifybin <临时路径>\main_stage5_FINAL.bin 0x08000000
r
g
q
```

### 屏幕板（⚠️ device 必须 GD32F303RC）
```
si SWD
speed 1000
device GD32F303RC
connect
h
loadbin <临时路径>\screen_stage5_FINAL.bin 0x08000000
verifybin <临时路径>\screen_stage5_FINAL.bin 0x08000000
r
g
q
```

---

## 三、Stage 5 增量改动（相对 Stage 4）

### 改动 5.1 — LEDA8 (PA15) 解绑照明灯 + 改"风机活动总指示"
- 原：跟 light_status bit1（CN3 故障时代替照明灯指示）
- 新：跟"等效 PTC 风机 duty ≥ 1"亮，与照明灯**完全无关**
- 物理意义：PA15 = 任何风机活动指示（用户档位 / 加热 / 新风都触发）

### 改动 5.2 — LED8/9/10 改基于"等效 PTC duty"显示
- 原（Stage 2）：仅按 fan_speed_actual 累加（用户没设档位时不亮，即使加热风机在 80% 转）
- 新：屏幕板镜像主板 max3 仲裁逻辑，从 0x01 包推算等效 PTC duty
  ```
  等效 duty = max(
      fan_speed_actual_duty,    // 0/30/60/100 (来自 byte 8)
      fresh_air ? 100 : 0,       // byte 19 bit1
      heating ? 80 : 0           // byte 16 bit 0 = BSP_RELAY_PTC_IO
  )
  ```
- LED8 PA4 on if eff ≥ 30
- LED9 PB9 on if eff ≥ 60
- LED10 PD2 on if eff ≥ 100
- LEDA8 PA15 on if eff ≥ 1

### 改动 5.3 — 编码器顺时针 = 增加（旧版逆时针 = 增加）
- 改 `enc_table` 全部 +/- 互换
- 主板 1 处改动，屏幕板不受影响

### 改动 5.4 — 数码管 E 段问题
- **不在固件改动范围**（用户已确认硬件解决）
- 软件 SEG_FONT 不动

---

## 四、烧录后验证清单（**Stage 5 增量**）

### 改动 5.1+5.2 验证（LED 指示）

| 操作 | 期望 LED 状态 |
|------|-------------|
| 全闲（不加热、不开新风、fan=0）| LED8/9/10 + PA15 全灭 |
| **CN3 按下切换照明灯** | **PA15 不变**（不再跟照明灯）|
| CN8 按 1 次 (fan=1) | LED8 + PA15 亮，LED9/LED10 灭 |
| CN8 按到 fan=2 | LED8 + LED9 + PA15 亮，LED10 灭 |
| CN8 按到 fan=3 | LED8 + LED9 + LED10 + PA15 全亮 |
| 仅按新风 (CN6, fan=0) | LED8 + LED9 + LED10 + PA15 + LEDA7 全亮（新风=100% 等效 3 档）|
| **仅加热（设温度比环境高）+ fan=0**（关键）| **LED8 + LED9 + PA15 亮**, LED10 灭（80% 不到满速）|
| 加热 + 新风 + fan=0 | LED8/9/10 全亮（新风>=100 优先）|

### 改动 5.3 验证（编码器方向）
- 顺时针旋编码器 → 当前编辑项数值 **增加**
- 逆时针旋 → 数值 **减少**
- 之前是反的（逆时针增加）— 改完后顺时针增加

### 已知不在 Stage 5 改动范围
- iPad APP 端 6 项 bug（详见确认书 v1.2 第 8 节）

---

## 五、所有 Stage 改动累计 (1~5)

| Stage | 范围 | 主要改动 |
|-------|------|---------|
| 1 | CN11 双发 + CN3/CN8 重映射 | screen UART1 双发, KEY_ID_MAP[7]=0x0B, 主板 0x82 加 case 0x0B |
| 2 | LED8/9/10 + GPIOD | screen 加 GPIOD 寄存器 + LED_Fan_Init + LEDA_Update 累加显示 |
| 3 | PTC 风机 max3 仲裁 | pwm_set_ptc_arbiter, temp_control 申请 safety_min, ControlTask 末尾统一调 |
| 4 | HMI 7 项 + 闪烁 + 协议复用 | HmiPage 8 项, send_timer_ctrl, hmi_seed_from_display 扩展, 闪烁改写, CO2 0x81 case 0x06 |
| 5 | LEDA8 + LED 跟 PTC duty + 编码器方向 | LEDA_Update 改成等效 duty 推算, enc_table 反转 |

总改动：~6 文件，+400/-50 行（Stage 1-5 累计）。

---

## 六、回滚

按 Stage 1-4 的回滚方案（`firmware/_pre_rework_backup_20260505_151421/`）即可。Stage 5 是 Stage 4 的小增量，无独立回滚点（如需回到 Stage 4 状态，烧 `firmware/_post_stage4_20260505_153022/main_stage4_FINAL.bin`）。

---

## 七、Stage 1-4 备份目录列表

```
firmware/_pre_rework_backup_20260505_151421/  ← 改动前
firmware/_post_stage1_20260505_151901/        ← Stage 1 完成
firmware/_post_stage4_20260505_153022/        ← Stage 4 完成
firmware/_post_stage5_20260505_210540/        ← 本目录, Stage 5 完成 (推荐烧此版本)
```

---

**交付完成**。
