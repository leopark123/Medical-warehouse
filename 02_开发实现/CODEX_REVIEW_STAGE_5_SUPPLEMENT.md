# Codex 审查请求补充 — Stage 5（追加 LEDA8/LED-PTC-duty/编码器方向）

**审查日期**：2026-05-05（Stage 5 增量）
**关联**：`02_开发实现/CODEX_REVIEW_STAGE_1_4_REWORK.md`（Stage 1-4 主审查，本文件是补充）
**最终交付包**：`firmware/_post_stage5_20260505_210540/`

---

## 一、Stage 5 改动摘要

3 处改动，~30 行：

| 改动 | 文件 | 行数 |
|------|------|------|
| 5.1 LEDA8 (PA15) 解绑照明灯 + 改"风机活动总指示" | screen main.c LEDA_Update | -5 / +5 |
| 5.2 LED8/9/10 改基于"等效 PTC duty"显示（含加热 80%）| screen main.c LEDA_Update | -10 / +30 |
| 5.3 编码器顺时针 = 增加 | main board tasks.c | -5 / +9 |

**驱动需求**：
1. CN3 按下切照明灯时 PA15 不应跟着亮（旧逻辑残留）
2. 加热时 PTC 风机自动 80% 转，LED8/LED9 也要亮（让用户知道风机在转）
3. 加热 + 用户没设档位时 PA15 也要亮（同上）
4. 编码器物理方向与人直觉一致（顺时针=正向增加）

---

## 二、改动详细 + 证据

### 5.1 — LEDA8 解绑照明灯

#### 旧实现（Stage 4 之前）
```c
/* LEDA8 PA15: CN8 照明灯(替代CN3) — light_status bit1 */
if (light_st & (1 << 1)) GPIOA_BSRR = (1 << (15 + 16));
else                     GPIOA_BSRR = (1 << 15);
```

#### 新实现（Stage 5）
合并到下面 LED8/9/10 等效 duty 逻辑里，统一驱动：
```c
/* LEDA8 PA15 — 风机活动总指示 (任何 ≥ 1) */
if (eff_duty >= 1) GPIOA_BSRR = (1 << (15 + 16));
else               GPIOA_BSRR = (1 << 15);
```

**审查点 R5.1**：
- 确认完全删除了原 `light_st & (1 << 1)` 跟随分支（无残留）
- LEDA8 仅在 LED8/9/10 之后被驱动一次，不会被多处覆盖

---

### 5.2 — LED8/9/10 基于"等效 PTC duty"

#### 镜像主板 max3 仲裁（[pwm_driver.c pwm_set_ptc_arbiter](firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.c)）

主板 PWM 输出 = max(safety_min, fresh_duty, user_duty)。

屏幕板从 0x01 包推算：
- byte 8 = `fan_speed_actual` (0~3) → user_duty (0/30/60/100)
- byte 19 bit 1 = `SW_BIT_FRESH_AIR` → fresh_duty (0 or 100)
- byte 16-17 bit 0 = `BSP_RELAY_PTC_IO` → safety_min (0 or 80, 加热 = PTC 继电器 ON)

```c
uint8_t fan      = d[8];
uint8_t heating  = (relay_st & (1U << 0)) ? 1 : 0;
uint8_t fresh    = (switch_st & (1U << 1)) ? 1 : 0;
uint8_t user_duty = (fan == 0) ? 0 :
                    (fan == 1) ? 30 :
                    (fan == 2) ? 60 : 100;
uint8_t eff_duty = user_duty;
if (fresh   && 100 > eff_duty) eff_duty = 100;
if (heating &&  80 > eff_duty) eff_duty = 80;
```

#### 完整场景验证矩阵（与主板 Stage 3 表一致）

| heating | fresh | fan | user_duty | eff_duty | LED8 (≥30) | LED9 (≥60) | LED10 (≥100) | PA15 (≥1) |
|:-------:|:-----:|:---:|:---------:|:--------:|:----------:|:----------:|:------------:|:---------:|
| 0 | 0 | 0 | 0 | **0** | 灭 | 灭 | 灭 | 灭 |
| 0 | 0 | 1 | 30 | **30** | 亮 | 灭 | 灭 | 亮 |
| 0 | 0 | 2 | 60 | **60** | 亮 | 亮 | 灭 | 亮 |
| 0 | 0 | 3 | 100 | **100** | 亮 | 亮 | 亮 | 亮 |
| 0 | 1 | 任意 | * | **100** | 亮 | 亮 | 亮 | 亮 |
| **1** | **0** | **0** | **0** | **80** | **亮** | **亮** | **灭** | **亮** |
| 1 | 0 | 1 | 30 | **80** | 亮 | 亮 | 灭 | 亮 |
| 1 | 0 | 3 | 100 | **100** | 亮 | 亮 | 亮 | 亮 |
| 1 | 1 | 0 | 0 | **100** | 亮 | 亮 | 亮 | 亮 |

**审查点 R5.2**：
- 推算逻辑与主板 Stage 3 `pwm_set_ptc_arbiter()` 完全一致（同 max3）
- 所有 9 行场景的 LED 状态符合用户需求"加热时 LED8/9 亮 + PA15 亮"
- relay_status 字节序：屏幕板用 `(d[16] << 8) | d[17]` (big-endian)，主板 screen_protocol.c:97-99 也是这个顺序 — 一致
- 加热判定用 `relay_status bit 0` (BSP_RELAY_PTC_IO = 0) 是最准确的"主板物理上加热中"信号
- ⚠️ 思考：加热中用 80 作为安全阈值是 Stage 3 主板硬编码值。如果以后主板改了，屏幕板要同步改。建议 Stage 6 把 80 提到协议或共享头文件作为常量？

---

### 5.3 — 编码器 enc_table 反转

#### 旧表（[tasks.c:495-500](firmware/HDICU_MainBoard/App/tasks/tasks.c)）
```c
static const int8_t enc_table[16] = {
     0, +1, -1,  0,
    -1,  0,  0, +1,
    +1,  0,  0, -1,
     0, -1, +1,  0
};
```

#### 新表（Stage 5）
```c
static const int8_t enc_table[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0
};
```

所有 +/- 互换 — 等价于 `delta = -delta_old`。

**审查点 R5.3**：
- 反转表是数学上等价于"对每个 delta 取负"（这点重要，因为编码器 Gray 码状态机本身没变）
- 编码器旋转 4 个状态形成一周，每个状态转换贡献 ±1（满四步= 1 click）— 表对称性保持
- 屏幕板 hmi_apply_encoder_delta 不需要改（仍按 delta>0 增 / delta<0 减）
- 影响范围：仅编码器物理旋转方向感觉，不影响其他逻辑

**疑问**：旋转 4 步 / 4 = 1 click 的逻辑（[tasks.c:544-550](firmware/HDICU_MainBoard/App/tasks/tasks.c)）`int8_t clicks = enc_accum / 4` — accum 反号后 clicks 也反号，自然就是新方向。**✅ 等价正确**。

---

## 三、与 Stage 1-4 的兼容性

| 项 | 影响 |
|---|------|
| Stage 1 (CN8 风速键 + CN11 双发) | ✅ 不冲突，CN8 触发 fan_speed 改变，Stage 5 LED 仍正确响应 |
| Stage 2 (LED8/9/10 + GPIOD) | ⚠️ Stage 5 替换了 Stage 2 的 LED 驱动逻辑（不再仅 fan 累加，改成等效 duty）— 期望行为 |
| Stage 3 (PTC max3 仲裁) | ✅ Stage 5 屏幕板侧镜像了 Stage 3 主板的 max3 计算 |
| Stage 4 (HMI 7 项 + 闪烁) | ✅ 完全独立，HMI 闪烁逻辑不动 |

---

## 四、新审查点（追加 R5）

请你（Codex）在主审查 R1-R-X 之外，额外回答：

### R5.1 — LEDA8 是否完全脱离 light_status 影响？
grep `light_st` 在 LEDA_Update 内的所有引用，确认 PA15 不再被 light_status 任何 bit 控制。

### R5.2 — 等效 duty 推算与主板仲裁一致性
对照 `pwm_set_ptc_arbiter()` 实现 vs 屏幕板新 LEDA_Update 推算，逐场景验证 9 行表是否完全一致。

### R5.3 — enc_table 反转的边界
编码器在 Gray 码序列 00 → 01 → 11 → 10 → 00（顺时针）下，新表是否给出连续 +1（而非 0/2 跳变）？画出转换图验证。

### R5.4 — 主板硬编码 "80" 的耦合风险
`tasks.c` 用 `(temp_state == HEATING) ? 80 : 0` 计算 safety_min；
屏幕板用 `heating ? 80 : 0` 同样的常量。
如果主板以后改成 70 或 90，屏幕板没同步会脱节。
**建议**：把 80 抽成 `PTC_FAN_HEATING_SAFETY_MIN_DUTY` 宏或加到协议字段。当前接受这个耦合风险吗？

### R5.5 — relay_status 字节序一致性
- 屏幕板：`(d[16] << 8) | d[17]` 解析为 uint16
- 主板发送：[screen_protocol.c:97-99] `payload[16] = (uint8_t)(s_relay_status >> 8); payload[17] = (uint8_t)(s_relay_status & 0xFF);` (big-endian)
- 检查：BSP_RELAY_PTC_IO = bit 0，所以在 uint16 的 LSB → 在 byte 17 的 LSB → 屏幕板 `(d[16] << 8) | d[17]` 解析后 `& (1 << 0)` 取的是 byte 17 的 bit 0 ✅

请验证此推理。

---

## 五、期望反馈

请按 R5.1-R5.5 给逐条 ✅/❌/⚠️ 结论 + 必修建议。

完整 diff：
```bash
cd F:\小项目\医疗仓
git diff pre-encoder-rework HEAD -- firmware/
```

---

**审查请求结束**。
