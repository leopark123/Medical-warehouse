# Codex Review: Stage 8 redo v4.1 — 你上轮 Block/Concern 的修复确认

请快速审查以下 5 处修复, 确认是否真正堵住了你上轮指出的问题。如果 Pass, 用户立即烧录测试。

---

## 你上轮的判断 (摘要)

> **Concern / 不建议直接烧给用户做正式验证。** v4 的方向基本对, 但至少有 1 个安全相关 Block:
> 1. **B1**: OPEN_O2 下 PTC 风机仍可能 80% (temp_state 滞后)
> 2. **B2**: iPad nursing 仍 1~3, APP 发 0/4 被拒
> 3. **B3**: O2 阀全路径互锁缺失 (`O2_STATE_SUPPLYING` 没纳入 fogging/UV 互斥)
> 4. **D**: `interlock_can_start_cooling` OPEN_O2 分支等价于 `return false`, 建议简化
> 5. **G**: KEY6/KEY7 单向互斥不完整 (用户回答**双向**)

---

## 修复实现 (基于 v4 commit `a4f38b2`, tag `stage8-redo-v4-pre-codex`)

### B1 修复: PTC safety_min 改基于 relay_status

文件 `firmware/HDICU_MainBoard/App/tasks/tasks.c` (around line 273-284)

```c
/* Stage 8 redo v4.1 (2026-05-07) Codex Block 1 修复:
 * 原: safety_min = (temp_state == HEATING) ? 80 : 0
 * 改: safety_min = (relay_status & PTC) ? 80 : 0
 * 原因: temp_state 跟 relay_status 之间有 1 拍同步延迟. 进入 OPEN_O2 当拍,
 * Rule 4 清掉了 PTC relay bit, 但 temp_state 仍可能保持 HEATING (要等下一轮
 * temp_control_update 才更新到 IDLE), 导致 PTC 风机 PWM 误开 80%. */
{
    uint8_t safety_min =
        (d->control.relay_status & (1U << BSP_RELAY_PTC_IO)) ? 80 : 0;
    uint8_t fresh_duty =
        (d->control.switch_status & SW_BIT_FRESH_AIR) ? 100 : 0;
    uint8_t fan = d->control.fan_speed_actual;
    if (fan > 3) fan = 3;
    uint8_t user_duty = pwm_fan_level_duty[fan];
    pwm_set_ptc_arbiter(safety_min, fresh_duty, user_duty);
}
```

**验证**: 进入 OPEN_O2 当拍, Rule 4 清 `BSP_RELAY_PTC_IO` bit (line 99) → 同周期 `relay_status & PTC = 0` → safety_min = 0 → PTC 风机 PWM = max3(0, 0, 0) = 0 ✓

`humid_state` 这个字段你提到只影响下一轮状态机, 我没改 (跟你建议"作为后续清理"一致)。

### B2 修复: iPad nursing OOB 0~4

文件 `firmware/HDICU_MainBoard/Protocol/ipad/ipad_protocol.c:221`

```c
// 改前:
if (!err_code && (nursing < 1 || nursing > 3))    err_code = IPAD_ERR_NURSING_OOB;

// 改后:
if (!err_code && nursing > 4)                     err_code = IPAD_ERR_NURSING_OOB;
```

**验证**: APP 发 nursing=0/1/2/3/4 都接受, =5 拒绝, 跟主板 KEY1 循环 0~4 一致。

### B3 修复: O2 阀互锁全路径覆盖

文件 `firmware/HDICU_MainBoard/Control/interlocks/interlock.c`

#### B3a Rule 5 加 SUPPLYING (line 64-84):

```c
const bool fogging_active = relay_is_on(*r, BSP_RELAY_WH_IO);
const bool open_o2_requested = (d->control.switch_status & SW_BIT_OPEN_O2) != 0;
const bool external_o2_demand = d->sensor.o2_master_demand || d->sensor.o2_req_demand;
/* v4.1 Codex Block 3: 自动 O2 闭环 (target_o2 < sensor.o2 → SUPPLYING) 也算 O2 阀活动 */
const bool auto_o2_supplying = (d->control.o2_state == O2_STATE_SUPPLYING);

if (fogging_active && (open_o2_requested || external_o2_demand || auto_o2_supplying)) {
    d->control.switch_status &= ~SW_BIT_OPEN_O2;
    d->setpoint.open_o2 = 0;
    /* v4.1: 自动闭环也强制让步, 避免 oxygen_control 下一拍又开阀 */
    if (auto_o2_supplying) {
        d->setpoint.enable_o2_ctrl = 0;
    }
    /* O2 valve: 强制关 (雾化期间任何 O2 路径都不允许) */
    relay_clear(r, BSP_RELAY_O2_IO);
    triggered = true;
}
```

**变化点**:
- 触发条件加 `auto_o2_supplying`
- 触发后清 `enable_o2_ctrl` 让自动闭环不再下一拍重开
- O2 阀**无条件关** (之前有 `if (o2_state != SUPPLYING)` 漏洞)

#### B3b Pre-start 检查改基于 relay_status:

```c
bool interlock_can_start_fogging(const AppData_t *d)
{
    if (relay_is_on(d->control.relay_status, BSP_RELAY_ZIY_IO)) return false;
    /* v4.1: 任何 O2 阀活动都禁雾化 (不只是手动 SW_BIT_OPEN_O2) */
    if (relay_is_on(d->control.relay_status, BSP_RELAY_O2_IO)) return false;
    return true;
}

bool interlock_can_start_uv(const AppData_t *d)
{
    /* v4.1: 任何 O2 阀活动都禁 UV */
    if (relay_is_on(d->control.relay_status, BSP_RELAY_O2_IO)) return false;
    if (relay_is_on(d->control.relay_status, BSP_RELAY_WH_IO)) return false;
    return true;
}
```

### D 修复: interlock_can_start_cooling 简化

```c
bool interlock_can_start_cooling(const AppData_t *d)
{
    if (relay_is_on(d->control.relay_status, BSP_RELAY_PTC_IO)) return false;
    if (relay_is_on(d->control.relay_status, BSP_RELAY_JIARE_IO)) return false;
    /* v4.1 Codex Concern D: 简化为直接禁制冷
     * v4 中 OPEN_O2 时 Rule 4 强制清 SW_BIT_FRESH_AIR, 原 outer_on && fresh_on 永远 false. */
    if (d->control.switch_status & SW_BIT_OPEN_O2) return false;
    return true;
}
```

### G 修复: 双向互斥

文件 `firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c` case 0x08

```c
case 0x08: /* 新风净化 toggle
            * Stage 8 redo v4.1 (Codex Concern G): 双向互斥 — 开新风时清内循环 */
    if (d->setpoint.fresh_air) {
        d->setpoint.fresh_air = 0;
    } else {
        d->setpoint.fresh_air  = 1;
        d->setpoint.inner_cycle = 0;   /* 双向互斥 */
    }
    break;
```

跟 case 0x07 (KEY6 长按开内循环关新风) 配对, 任何一边 0→1 都强制清对方, 保证 inner_cycle + fresh_air 不会同时 = 1。

---

## 编译产物

- 主板 bin: `firmware/_post_stage8_redo_v4_1_20260507_210521/main_stage8_redo_v4_1_FINAL.bin`
- MD5: `d495a1f103ea3088544cfe4c200368b8`
- text 27768 (-16 vs v4, interlock 简化省指令)
- g_app_data 116B 不变 (struct 未动)
- git commit: `27e5284`, tag `stage8-redo-v4-1-CODEX-FIXED`
- v4 锁定 tag (回滚锚点): `stage8-redo-v4-pre-codex`

---

## 给 Codex 的请求 — 5 项快速复审

请逐个回答 Pass / Block, 如有 Block 给具体修法。

### Q1 (B1 修复确认)
`safety_min` 改成基于 `relay_status & BSP_RELAY_PTC_IO`, 是否完整治 OPEN_O2 race?  
还需要加 `temp_state = IDLE` 之类的同步动作吗?

### Q2 (B2 修复确认)
nursing OOB 改 `nursing > 4`, 含义是允许 0~4 全接受。  
有没有别处依赖 `nursing >= 1` 的代码 (比如默认值 / 报警阈值 / 屏幕 LEDA1 显示) 需要同步?

### Q3 (B3 修复确认)
- Rule 5 改后, 雾化运行时**自动 O2 闭环**也会被让步 (`enable_o2_ctrl = 0`)。这种用户没主动操作但 setpoint 字段被改的行为, 是否合理?
- `interlock_can_start_fogging/uv` 改成基于 `relay_status & O2_IO`, 但 `relay_status` 是 ControlTask 写的 (oxygen_control 之后), pre-start 检查在 `control_timers_start_*` 里 (屏幕板长按事件触发), 跟 ControlTask 是不同上下文。**这两个上下文可能看到的 relay_status 不一样, 是否需要加 lock?**

### Q4 (D 修复确认)
`interlock_can_start_cooling` 简化后语义跟之前一致, 还是有边界差异?

### Q5 (G 修复确认)
case 0x07 长按 (开内循环) 清 fresh_air + case 0x08 单击 (开新风) 清 inner_cycle, 这种"任何一边写 1 必清对方"是双向互斥的标准实现。还有别的地方写 setpoint.inner_cycle / fresh_air 吗 (比如 iPad 0x03 写参数 line 262-263 是双写, 可能让两个同时 1)?

---

## 测试矩阵 (Pass 后用户烧)

### 重点回归 (Codex 上轮指出的问题)
- [ ] **B1**: 进入 OPEN_O2 当拍, JLink dump PE6/PE9 PWM 占空比 = 0% (不能是 80%)
- [ ] **B2**: iPad APP 发 nursing=0 / nursing=4 都被接受, 主板按对应档点对应灯
- [ ] **B3**: 雾化运行时长按 KEY5 (开放供氧) → 应被阻止 (open_o2 setpoint 仍 0); 雾化运行时编码器调 target_o2 触发自动闭环 → 应被 Rule 5 清 enable_o2_ctrl
- [ ] **D**: 进入 OPEN_O2 后 cooling 不能启动
- [ ] **G**: 单击 KEY7 → 新风开 + 内循环灯 (LEDA6) 立即灭

### v4 新功能回归 (上轮已 Pass 的)
- [ ] OPEN_O2 期间所有风机灯 (LEDA7/8 + LED8/9/10) 全灭
- [ ] OPEN_O2 期间主板 PE6/PE9 PWM=0, PE2/PE3/PE4 全关
- [ ] 长按 KEY5 重新供氧, 计时从 0 起步
- [ ] KEY1 单击 5 档循环, 第 4 档 PB1+PC5 双亮
- [ ] 长按 KEY6 进内循环 → LEDA7 (新风) 灭

### v3 / v2 回归 (确认不退化)
- [ ] T3 PE1 闪烁仍修好
- [ ] T4/T8 雾化消毒分钟显示对

---

如全 Pass, 用户立即烧 `main_stage8_redo_v4_1_FINAL.bin`。
