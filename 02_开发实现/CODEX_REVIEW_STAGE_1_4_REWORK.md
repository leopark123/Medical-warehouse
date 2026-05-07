# Codex 审查请求 — 屏幕板编码器 / 风速三档 / HMI 扩展（Stage 1-4 改造）

**审查日期**：2026-05-05
**审查范围**：双板（STM32F103VET6 主板 + GD32F303RCT6 屏幕板）
**改动总量**：6 文件，~+360 行 -50 行
**关联文档**：`02_开发实现/HDICU_屏幕板主板改动确认书_v1.2.md`
**未实测**：固件已编译通过，**未烧录验证**，待 Codex 通过后交付烧录人员

---

## 一、审查目标

请你（Codex）以**最严苛**的工程视角审查本次 4 个 Stage 的改动，重点关注：

1. **安全关键路径**（Stage 3 PTC 风机仲裁）— 8 种场景的逻辑等价性 + race condition
2. **协议向后兼容**（不破坏现有 iPad / 屏幕板交互）
3. **资源/时序冲突**（GPIO 时钟、UART TX 阻塞、TM1640 GRID 闪烁）
4. **边界条件**（HMI 步长溢出、CO2 5000 上限、fog/disinf 3600 上限、PTC duty 100 上限）
5. **代码可读性 / 注释完整性**

---

## 二、改动文件总览

| 文件 | Stage | 行数变化 |
|------|-------|---------|
| `firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.h` | 3 | +20 -3 |
| `firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.c` | 3 | +27 -9 |
| `firmware/HDICU_MainBoard/Control/temp/temp_control.c` | 3 | +9 -10 |
| `firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c` | 1, 4 | +16 |
| `firmware/HDICU_MainBoard/App/tasks/tasks.c` | 3 | +18 -10 |
| `firmware/HDICU_ScreenBoard/main.c` | 1, 2, 4 | +290 -25 |

回滚锚点：`git tag pre-encoder-rework` (commit `4e14d63`)

---

## 三、Stage 1 审查点 — CN11 双发 + CN8 风速键

### 改动摘要
- `uart_primary_send` 改成同时调 `uart1_send` (CN11) + `uart2_send` (CN12)
- 删除 `uart1_send` 的 `__attribute__((unused))` 标记
- `KEY_ID_MAP[7] = 0x0B`（CN8 从"照明灯替代"改为"风速键"）
- 主板 `case 0x82 0x0B` 单击 → `fan_speed = (fan_speed + 1) % 4`

### 请你重点审查
- **R1.1** `uart1_send` 阻塞性：函数有 10ms/字节超时，**双发后单帧 send 时间是否会显著增加**？send_frame 的最大帧 len 估算（包括 0x01 显示 26B+overhead → 约 30B；0x06 体征类似）。在 100ms 主循环里是否安全？
- **R1.2** `case 0x0B` 没有自己的 `app_data_lock/unlock`，但它在外层 `0x82` 的 lock 范围内（参见 `screen_protocol.c:184` `app_data_lock()` + `screen_protocol.c:261` `app_data_unlock()`）— 确认 fan_speed 写入是被外层锁保护的。
- **R1.3** 0x0B 与 iPad 协议 0x03 byte 12 fan_speed 的写入路径冲突？iPad 也写 setpoint.fan_speed，物理按键也写 — 是否需要"先写后冲突"的语义防护？
- **R1.4** KEY_ID_MAP[1] CN3 = 0x02 在 CN3 硬件未修复时会怎样（防护性问题）？

---

## 四、Stage 2 审查点 — LED8/9/10 + GPIOD 启用

### 改动摘要
- 加 `GPIOD_BASE/CRL/BSRR` 寄存器宏定义
- 加 `LED_Fan_Init()` 函数（启用 GPIOD 时钟 + 配置 PA4/PB9/PD2 为 OUTPUT_PP_2MHz + 默认 HIGH=off）
- `LEDA_Update()` 末尾加 LED8/9/10 累加显示（fan>=1/2/3）
- `main()` 加 `LED_Fan_Init()` 调用（在 LEDA_Init 之后）

### 请你重点审查
- **R2.1** PA4/PB9/PD2 在屏幕板 GD32F303RC 上的**外设复用**：PA4 默认 ADC1_IN4/SPI1_NSS/DAC_OUT1，PB9 默认 I2C1_SDA/TIM4_CH4/CAN_TX，PD2 默认 UART5_RX/TIM3_ETR/SDIO_CMD。本工程中是否有任何代码使用这些外设？grep 全代码确认。
- **R2.2** GPIOD 时钟启用顺序：`LED_Fan_Init()` 在 `LEDA_Init()` 之后调用，但 LEDA_Init 已经启用 A/B/C 时钟，LED_Fan_Init 又重复启用 + 加 D。是否有重复启用的副作用？或更整洁是合并初始化？
- **R2.3** LEDA_Update 调用频率（100ms 间隔，main loop 中）— 加了 3 个 BSRR 写入，性能影响可忽略，但确认没有在 ISR 里被调用。
- **R2.4** 累加显示语义"fan>=1 LED8 亮"在 fan=0 时三颗都灭 — 与 LEDA1-8 active-low 保持一致 (BSRR `1<<(pin+16)` = LOW = ON)。

---

## 五、Stage 3 审查点 ⚠️ **安全关键** — PTC 风机三方 max 仲裁

### 改动摘要
- 加 `pwm_set_ptc_arbiter(safety_min, fresh_duty, user_duty)` — 三方 max3
- `pwm_fan_level_duty[4] = {0, 30, 60, 100}` 公开供 ControlTask 用
- `pwm_set_fan_speed()` 改为空 stub（X 方案：PE5/PC13 解绑 fan_speed）
- `temp_control.c` 删除所有 `pwm_set_fan2_duty(...)` 直接调用（4 处）
- `tasks.c ControlTask` 删除 fresh_air `pwm_set_fan2_duty(100)` 直接覆盖；末尾统一调 arbiter

### 请你重点审查（最关键）

#### R3.1 — max3 逻辑等价性 8 种场景

请验证 `pwm_set_ptc_arbiter()` 输出与下表一致：

| temp_state | fresh_air | fan_speed | 期望 PTC duty | 物理意义 |
|:----------:|:---------:|:---------:|:-------------:|---------|
| IDLE | OFF | 0 | 0 | 关 |
| IDLE | OFF | 1 (30) | 30 | 用户低档 |
| IDLE | OFF | 3 (100) | 100 | 用户高档 |
| IDLE | ON  | 任意 | 100 | 新风强制 |
| HEATING | OFF | 0 | **80** | 安全最低（**关键**）|
| HEATING | OFF | 1 (30) | 80 | 安全 > 用户低档 |
| HEATING | OFF | 3 (100) | 100 | 用户 ≥ 安全 |
| **HEATING** | **ON** | **0** | **100** | **新风 > 安全（max3 修正点）** |
| COOLING | * | * | (依赖 fresh+fan) | safety_min=0 |

#### R3.2 — temp_control fail-safe 路径（重要）

`temp_control.c:19-32` sensor invalid fail-safe 之前调用 `pwm_set_fan2_duty(0)` 强制关 PTC 风机。**改动后删了**，依赖 ControlTask 末尾仲裁（temp_state=IDLE → safety_min=0）。

**问题**：若此时 `fresh_air=1`，PTC 风机仍会 100% 转。这是新行为。
**判断**：sensor invalid 时只关加热（继电器），风机继续支持新风模式 — 是否符合临床预期？还是应该**强制关一切**？

#### R3.3 — Race condition

`temp_state` 在 `temp_control_update()` 内被写，在 ControlTask 末尾仲裁中被读。同一任务内顺序执行无 race。但 `humidity_control_update()` 是否会修改 `temp_state`？grep 确认。

`switch_status & SW_BIT_FRESH_AIR` 在 `interlock_apply()` 后被设置，仲裁在它之后读 — 顺序对。请确认。

#### R3.4 — 与互锁 (interlock.c) 的协调

`interlock.c:108-117` 有 "open_o2 active 时禁止加热" 的规则，会清掉 PTC 继电器。但本次仲裁基于 `temp_state == HEATING` 计算 safety_min — interlock 清继电器后 temp_state 还是 HEATING 吗？如果是，会出现"PTC 继电器关了但风机还在 80% 转"的奇怪状态（不危险，但浪费）。

请检查：interlock 是否会同时把 temp_state 改回 IDLE？

#### R3.5 — pwm_set_fan_speed stub 的副作用

旧实现驱动 PE5+PC13。改成空 stub 后，**PE5 和 PC13 谁来管？**

- 检查 grep `pwm_set_fan1_duty` 和 `pwm_set_fan3_duty` 在代码里的所有调用点
- 这两个函数还存在但调用变少了（仅 pwm_driver_init 时清零）
- 如果之前 PE5/PC13 因 ControlTask `pwm_set_fan_speed(fan_speed_actual)` 而被 ON，现在永远 OFF — 可能影响其他依赖（如内循环按键期望"按了内循环键 + fan_speed=2 → 内循环风机 PE5 转"）

我推断 PE5 应该由 inner_cycle 按键单独控制（之前是 PE7 推拉电磁铁，但 PE5 也叫"内循环风机"语义重叠）。请你审查这个假设。

---

## 六、Stage 4 审查点 — HMI 7 项扩展 + 闪烁

### 改动摘要
- `HmiPage_t` 从 4 项扩到 8 项（含 RESET_O2_TIME）
- 加 3 个 shadow（co2_ppm, fog_sec, disinf_sec）
- `hmi_seed_from_display` 扩展从 0x01 包 byte 4-5/10-11/12-13 取 CO2/fog/disinf 初值
- `hmi_cycle_page` 改成 `(page + 1) % HMI_PAGE_COUNT` 循环
- `hmi_apply_encoder_delta` 加 4 个新 case（含步长 100ppm、60s、60s）
- `process_rx_frame case 0x06 evt==0x01` 在 RESET_O2_TIME 页发 `0x83 type=3 cmd=3` 清供氧
- 闪烁逻辑改写：仅当前页 GRID 全灭/全亮 0.5Hz
- `send_timer_ctrl()` 辅助函数（0x83 协议）
- 主板 `screen_protocol.c case 0x81 case 0x06` 加 CO2 处理（范围 0~5000）

### 请你重点审查

#### R4.1 — HmiPage_t 枚举值与 hmi_apply_encoder_delta switch case 匹配

确保所有 7 个 SET 页面在 switch 的 ++ 和 -- 分支都有对应 case，没有漏掉。

#### R4.2 — send_timer_ctrl 协议解析

```
type=0x01/0x02 (雾化/消毒): cmd=0x01 时 duration > 0 启动, cmd=0x02 时 duration 忽略停止
type=0x03 (供氧累计): cmd=0x03 清零, duration 忽略
```

我的 hmi_apply_encoder_delta 在 SET_FOG/DISINFECT 页用 `cmd = (duration > 0) ? 0x01 : 0x02` 切换启停 — 正确？还是用户期望"调到 0 不停止，只是设置为 0 等手动启动"？

#### R4.3 — HMI_PAGE_RESET_O2_TIME 单击触发清零的时序

`process_rx_frame case 0x06 evt==0x01`：在 RESET_O2_TIME 页发 `send_timer_ctrl(0x03, 0x03, 0)` 然后 `s_hmi_page = HMI_PAGE_LIVE`。

**问题**：`hmi_cycle_page` 在其他页是切到下一页，但这里手动改 `s_hmi_page = LIVE`。如果用户**长按**编码器，会触发 evt=0x02 长按，已有 `send_key_action(0x0A, 0x02)` 也清零（旧行为兼容）。
**请审查**：单击 vs 长按 两条路径是否会互相干扰（双触发清零）？

#### R4.4 — 闪烁与 LIVE 显示的 GRID 位冲突

`update_display_from_data` 已经把所有 GRID 位都写过一遍（实测值），然后才执行闪烁逻辑覆盖编辑页。

**关键**：闪烁覆盖时 `blink_off=1` 写 0x00 关掉那几位 — 下一帧 100ms 后 LIVE 实测值会被重写**回去**（因为 `update_display_from_data` 每次重新写所有位）。所以闪烁的"灭"实际只持续到下一帧 LIVE 写入。

**问题**：100ms 刷新 vs 500ms 闪烁周期 → 实际看到的"灭"窗口比 500ms 短。这是 bug 还是可接受？

#### R4.5 — hmi_seed_from_display 数据源

byte 10-11 是 fog_remaining（剩余秒数，会随倒计时减少），不是 setpoint.fog_time。这意味着用户进 SET_FOG 页时看到的**初值是"剩余秒数"**，不是"上次设的总时长"。

**正确？**：用户调编码器时 += 60，会把"剩余"变大，然后协议发 `start_fog(new_duration)` 重新启动 timer — 但实际上 fog_time setpoint 也变了。这个语义可能让用户困惑（"我之前设 5 分钟，现在剩 1 分钟，调一下变 2 分钟，但实际是从 2 分钟开始重新倒数")。

**建议**：seed 是否应该从主板 setpoint.fog_time 而非 control.fog_remaining？但 0x01 包没有 fog_time 字段（仅 fog_remaining），需要协议层加 setpoint 字段才能正确 seed。

请评估：当前实现是否够用？协议是否需要扩展？

#### R4.6 — CO2 setpoint 范围

主板 `case 0x06 if (value <= 5000)` 接受 0~5000。屏幕板编码器步长 100，shadow 类型 uint16，无下限检查（隐含 0）。

**问题**：CO2 阈值 = 0 是否合理？现实中 CO2 阈值应该 > 大气浓度（~400 ppm）。建议加最小值检查？

#### R4.7 — 编码器旋转触发协议发送频率

每旋转一格立即发 `send_param_set` 或 `send_timer_ctrl`。如果用户快速旋转 100 格，主板 100 次 setpoint 写入。

主板侧每次 setpoint 改动都会触发：
- 0x81 → 立即写 setpoint，下一控制周期 (200ms) 控制循环响应
- 0x83 → 立即调 control_timers_start_xxx，可能反复重启 timer（用户调 fog 步长 60s 时可能反复重启）

**请审查**：是否需要 debounce / 仅在用户停止旋转后才发送？

---

## 七、跨阶段审查点

### R-X.1 — 全文件 grep `pwm_set_fan_speed` 调用
确保改成 stub 后没有依赖它实际驱动 PE5/PC13 的代码。

### R-X.2 — 全文件 grep `pwm_set_fan2_duty` 调用
确保只剩 `pwm_set_ptc_arbiter` 一处调用（控制路径单点）。

### R-X.3 — 全文件 grep `setpoint.fan_speed` 写入
应该只有：
- iPad 0x03 写参数 (handle_write_params)
- 屏幕板 0x82 case 0x0B (新加, Stage 1)
- 屏幕板 0x81 ParamID 0x04 (从 iPad 协议反向，但屏幕板没用)
- 0x0B 恢复出厂

### R-X.4 — 协议字节布局
核对 `screen_protocol.c screen_send_display_data()` 的 0x01 包 26 字节 布局，与 `hmi_seed_from_display()` 解析的 byte 索引（0/1/2/3/4/5/10/11/12/13）一致。

---

## 八、对比 git tag pre-encoder-rework 的 diff

```bash
git diff pre-encoder-rework HEAD -- firmware/
```

请你也直接读 diff，找出我可能在描述里漏掉的改动。

---

## 九、期望反馈格式

请按以下格式回复：

### 通过 / 不通过 / 需修正

对每个 R 编号，给一句话结论：
- ✅ R3.1 max3 逻辑等价 — 全部 9 行场景验证通过
- ❌ R4.5 fog_remaining 作 seed 不正确 — 必须改用 setpoint.fog_time
- ⚠️ R3.2 sensor invalid 时风机继续支持 fresh_air — 需产品确认是否符合预期

### 必修问题
列出 P0/P1 必须修的 bug，按文件:行号给修改建议。

### 建议改进
列出可选优化，按优先级排。

### 整体评价
3-5 句总结：是否可以交付烧录？还是必须先修某些 bug？

---

## 十、附：审查上下文文件

请优先读以下文件理解全貌：
- `02_开发实现/HDICU_屏幕板主板改动确认书_v1.2.md` — 完整改动设计
- `firmware/_post_stage4_20260505_153022/HANDOFF_README.md` — 烧录人员说明
- `firmware/_pre_rework_backup_20260505_151421/BACKUP_MANIFEST.md` — 备份清单

代码改动现在在 working tree（未 commit），要看完整 diff：
```bash
cd F:\小项目\医疗仓
git diff pre-encoder-rework HEAD -- firmware/
```

---

**审查请求结束**。期待严苛反馈。
