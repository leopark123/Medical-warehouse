# Stage 8 重做 — 编码器按客户流程图重写

**生成时间**: 2026-05-07 12:15
**基于**: Stage 8 v3 源码 (`stage8-v3-snapshot` git tag, 备份目录 `_pre_encoder_redesign_20260507_115421/`)
**经过**: 5 轮 Codex 审查 + 客户流程图确认 + 客户答复 (D1=b 撤销 / D2=A 不做 CO2 / 修正3=seed_done)
**双板必须同烠**

---

## 🔴 烠录前必读

1. **双板协议变更**:
   - 删: `0x82 KeyAction(0x0A, 0x05)` 取消短鸣事件
   - 加: `0x83 TimerType=0x03 Cmd=0x04` 氧气计时长按 (清累计+关 O2 阀)
   - 加: `0x82 KeyAction(虚拟 KeyID 0x0C/0x0D/0x0E, action=0x02)` 撤销时清 enable
2. 烠录前 **必须先烠 Stage 7 .bin 一次** (`_post_stage7_swj_fix_20260506_134251/`) 让现场恢复, 然后再烠本版
3. **不要单板烠** Stage 8 重做版 (旧主板 + 新屏幕板会因协议不一致出现异常)

---

## 二进制产物

| 文件 | 大小 | MD5 |
|---|---|---|
| `main_stage8_redo_FINAL.bin` | 27856 B | `e217b7ce3d8c3347300182401163351d` |
| `main_stage8_redo_FINAL.elf` | 61116 B | — |
| `screen_stage8_redo_FINAL.bin` | 5656 B | `112f4c0355359106c36272d5051bf9b6` |
| `screen_stage8_redo_FINAL.elf` | 16812 B | — |

**Size 增量** (vs Stage 8 v3):
- 主板: 27824 → 27856 B (+32, 净增小)
- 屏幕板: 5504 → 5656 B (+152, 主要是 hmi_revert_current_set_page + apply_encoder_delta 重写)

---

## 一、编码器流程 (按客户最新流程图)

### 状态机

```
LIVE 页
  │
  │ 单击 ───┐
  │ 长按 → 清供氧累计 (Stage 7 行为保留)
  │
  ▼
温度闪烁 ─单击─→ 湿度闪烁 ─单击─→ O2闪烁 ─单击─→ [CO2跳过] ─单击─→ 雾化闪烁 ─单击─→ 消毒闪烁 ─单击─→ 氧气计时闪烁 ─单击─→ LIVE
```

### 单击行为
- **任意页 → 翻下一页** (Stage 7 行为, 不 commit, 仅切页面)
- LIVE → SET_TEMP
- SET_TEMP → SET_HUMID → SET_O2 → [SET_CO2 跳过] → SET_FOG → SET_DISINFECT → 氧气计时 → LIVE

### 旋转行为 (流程图: "旋转, 数字增加或减小")
- **温/湿/O2/CO2 SET 页**: 改 shadow + **立即下发主板** (Stage 7 行为, send_param_set)
  - 主板 0x81 自动 enable_xxx_ctrl=1 (闭环启动)
- **雾化/消毒 SET 页**: 改 shadow + **立即启动继电器** (流程图字面: "旋转, 计时增加同时雾化继电器启动")
  - send_timer_ctrl(type, 0x01, sec) 启动倒计时
- **氧气计时页 / LIVE 页**: 不响应旋转

### 长按行为 (流程图按页面分情况)
- **LIVE 页** → 清供氧累计 (Stage 7 b 方案保留)
- **温/湿/O2 SET 页** → **撤销 + 回滚** (D1=b):
  - send_param_set(原 setpoint) 回滚到进入 SET 页瞬间的值
  - 若原 enable=0, 再发虚拟 KeyID 0x0C/0x0D/0x0E action=0x02 让主板 enable 也回到 0
  - 退回 LIVE 页
- **雾化/消毒 SET 页** → 关继电器 + 时间清 0:
  - send_timer_ctrl(type, 0x02, 0) 停止
  - 退回 LIVE
- **氧气计时页** → 清累计 + 关 O2 阀 (Stage 8 重做新增):
  - send_timer_ctrl(0x03, 0x04, 0) 主板清累计 + 清手动开放 + 禁用 O2 闭环
  - O2 阀在下一个 ControlTask (≤200ms) 关闭 (oxygen_control 重新仲裁)
  - **不影响外部 PD8/PB6 硬件供氧请求** (硬件优先级保留)
  - 退回 LIVE

### 5 秒超时
- SET 页 5 秒未操作 → 自动回 LIVE
- **超时不撤销** (旋转已立即下发, setpoint 已生效)
- 仅长按才是真正的"撤销 + 回滚"

---

## 二、保留 Stage 8 v3 改动 (与编码器无关)

✅ 蜂鸣精简 (B1-B7 静音, 仅 B8 屏幕通信故障响)
✅ PE5/PC13 跟 PE2 压缩机
✅ NTC 3s 滑动平均滤波
✅ 红蓝光软互斥 (light_ctrl_normalize)
✅ enable_temp/humid/o2_ctrl 字段 (写参数自动启用)
✅ 上电默认关 actuator (enable=0)
✅ 0x01 显示帧扩展 byte 26-37 setpoint + byte 38 enable bitmap (含 protocol_version=0x1)
✅ 0x0B 出厂复位清 enable + alarm latch
✅ AlarmTask 报警跟随 enable

---

## 三、删除 Stage 8 v3 编码器副作用

❌ `cancel_beep_until_tick` 字段 (ControlState_t)
❌ AlarmTask buzzer 三优先级 cancel 分支 (回二优先级)
❌ 0x82 KeyAction(0x0A, action=0x05) 取消短鸣事件
❌ `hmi_commit_current_page()` 函数
❌ `hmi_invalidate_shadow()` 函数
❌ shadow_valid 拦截用户操作的逻辑
❌ 5s 超时清 shadow_valid

---

## 四、新增 Stage 8 重做内容

⚙ **协议**:
- 0x83 TimerType=0x03 Cmd=0x04 (氧气计时长按: 清累计+关阀)
- 0x82 虚拟 KeyID 0x0C/0x0D/0x0E action=0x02 (撤销时清对应 enable)

⚙ **屏幕板字段**:
- `s_setpoint_seed_done` (改名自 s_hmi_shadow_valid, 仅控制是否下发)
- `s_orig_temp_x10 / s_orig_hum_x10 / s_orig_o2_x10` (撤销回滚备份)
- `s_orig_enable_temp / humid / o2` (撤销回滚备份)

⚙ **屏幕板函数**:
- `hmi_revert_current_set_page()` 撤销当前 SET 页 (回滚 setpoint + enable)

⚙ **主板逻辑**:
- 0x82 case 0x0C/0x0D/0x0E action=0x02 → 清对应 enable_xxx_ctrl=0
- 0x83 case 0x03 cmd=0x04 → control_timers_reset_o2_accum + open_o2=0 + enable_o2_ctrl=0

---

## 五、关键设计决策 (Codex 五审最终)

| 决策 | 答复 | 实现 |
|---|---|---|
| 长按"取消"语义 (D1) | b 撤销 (回滚 setpoint+enable) | `hmi_revert_current_set_page` |
| CO2 SET 页 (D2) | A 留 Stage 9 | `hmi_cycle_page` 跳过 SET_CO2 |
| shadow_valid 改名 | 是 | `seed_done` 仅控制下发 |
| 温度达标行为 (C2) | 持续保温 | temp_control 维持 Stage 7 滞环, 不动 |
| 雾化/消毒旋转 (C3) | 旋转即启动 | send_timer_ctrl 立即下发 |
| H1: 撤销恢复 enable (Codex v4) | 双值回滚 | 屏幕板 send_param_set + 虚拟 KeyID |
| H2: 氧气关阀语义 (Codex v4) | 关手动+禁闭环, external 不动 | 0x83 cmd=0x04 实现 |
| H3: LIVE 页持续同步 (Codex v5) | 是 | hmi_seed_from_display 仅 LIVE 同步 |

---

## 六、烠录步骤

### Step 1: 现场先烠 Stage 7 (恢复)
```cmd
JLink:
  device STM32F103VE / GD32F303RC
  loadbin _post_stage7_swj_fix_20260506_134251/main_stage7_FINAL.bin 0x08000000
  loadbin _post_stage7_swj_fix_20260506_134251/screen_stage7_FINAL.bin 0x08000000
```
现场设备恢复 Stage 7 行为 (旋转即下发, 长按清供氧累计)。

### Step 2: 双板同烠 Stage 8 重做
```cmd
JLink:
  loadbin _post_stage8_redo_20260507_121517/main_stage8_redo_FINAL.bin 0x08000000
  loadbin _post_stage8_redo_20260507_121517/screen_stage8_redo_FINAL.bin 0x08000000
```

---

## 七、烠录后测试矩阵

| # | 验证项 | 方法 | 预期 |
|---|---|---|---|
| T1 | 上电 LIVE 显示 | 上电 5s | 温/湿/O2 显示数字 (不是 0) |
| T2 | 单击翻页 | LIVE → 单击 → SET_TEMP → 单击 ... | 5 个 SET 页循环 (CO2 跳过) |
| T3 | 旋转温度 | SET_TEMP 旋转 +5 格 | 温度数字立即变 +0.5°C, 主板加热启动 |
| T4 | 旋转雾化 | SET_FOG 旋转 +1 格 | 雾化时间 1 分钟, 继电器立即启动倒计时 |
| T5 | 旋转氧气计时 | SET_RESET_O2 页旋转 | 不响应 (流程图字面) |
| T6 | 长按 LIVE | LIVE 长按 2s | 供氧累计清零 (Stage 7 b 方案) |
| T7 | 长按温度 SET 撤销 | SET_TEMP 旋 +5 格, 长按 2s | 退回 LIVE, 温度回到旋前原值, enable 也回原值 |
| T8 | 长按雾化 SET 关停 | SET_FOG 旋启动, 长按 2s | 退回 LIVE, 雾化继电器关, 时间显示 0 |
| T9 | 长按氧气计时关阀 | RESET_O2 页长按 2s | 退回 LIVE, 供氧累计清零, O2 阀关 (除非 PD8 硬件请求) |
| T10 | 5s 超时不撤销 | SET 页旋后等 6s | 退回 LIVE, **setpoint 保持** (不撤销) |
| T11 | 蜂鸣静音 | 设温度异常等 12s | 屏幕闪 TEMP_LOW, **不响** |
| T12 | 蜂鸣 B8 保留 | 拔屏幕板线 5s | 间歇响 |
| T13 | 雾化到点响 3s | SET_FOG 设 1 分钟, 等到 0 | 连续响 3s |
| T14 | LIVE 页 setpoint 同步 | iPad 改温度 setpoint, 屏幕在 LIVE 页 | 屏幕 LIVE 页温度数字立即变 (H3 持续同步) |
| T15 | SET 页编辑保护 | iPad 改 setpoint, 屏幕在 SET_TEMP 页 | 屏幕 SET_TEMP shadow 不被覆盖 (H3 SET 不同步) |
| T16 | seed_done 保护 | 上电 100ms 内立即旋转编码器 | shadow 改但**不下发主板** (seed_done=0) |

---

## 八、已知遗留 (Stage 9 候选)

1. **target_co2 仍是 placeholder**: AlarmTask CO2 报警仍用固定 5000ppm 阈值, target_co2 写入但不生效
2. **CO2 SET 页跳过**: 屏幕板单击翻页跳过, 客户暂时看不到 CO2 设置
3. **ALARM_O2_CO2 共位**: O2 偏离和 CO2 超标共用 alarm_flags bit3, iPad/屏幕分不清
4. **Stage 9 工作**: 拆 ALARM_O2 + ALARM_CO2 位 + 启用 SET_CO2 页 + target_co2 真实生效

---

## 九、回滚方案

如 Stage 8 重做现场出新问题:
```cmd
JLink: loadbin _post_stage7_swj_fix_20260506_134251/main_stage7_FINAL.bin 0x08000000
JLink: loadbin _post_stage7_swj_fix_20260506_134251/screen_stage7_FINAL.bin 0x08000000
```
回到 Stage 7 行为 (B1-B7 蜂鸣会响, PE5/PC13 裸引脚, NTC 抖动可见)。

如要回到 Stage 8 v3:
```cmd
JLink: loadbin _post_stage8_20260506_164213/main_stage8_FINAL.bin 0x08000000
JLink: loadbin _post_stage8_20260506_164213/screen_stage8_FINAL.bin 0x08000000
```

---

## 十、给烠录人员的话

1. **必须双板同烠** (协议变更, 不兼容 Stage 7/Stage 8 v3)
2. 烠完上电:
   - 屏幕显示温/湿/O2 实时值
   - actuator 默认关 (用户写参数才启动)
3. **测试编码器**:
   - 单击 = 翻页 (与 Stage 7 一致)
   - 旋转温/湿/O2 = 立即生效, 看主板加热/制冷动作 (与 Stage 7 一致, 但启动了 enable)
   - 旋转雾化/消毒 = 立即倒计时
   - 长按温/湿/O2 SET = 撤销, 看温度回原值
   - 长按雾化/消毒 SET = 关停继电器
   - 长按氧气计时 = 清累计 + 关 O2 阀
   - 长按 LIVE = 清供氧累计

---

**本文档结束**.
