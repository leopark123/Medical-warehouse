# AUDIT_LOG.md — Codex 审计记录

> 每轮 Codex 审计完成后，将结论追加到此文件。

---

## Batch 04-R1 — GPIO网表对齐+安全修复 首轮审查
日期：2026-04-09
阶段：P3
验收项：GPIO 16处修正 + 安全/并发/ADC/Flash修复

### 结论：PASS WITH RISKS (1 FAIL → 回归修复)

### Critical（必须修复，否则BLOCK）
- screen_protocol.c:211 未配对的app_data_unlock() — action==0x01加锁但0x02不加锁，
  导致长按路径"未加锁先解锁"。**已在R2中修复。**

### Major（应修复，否则PASS WITH RISKS）
- 无新增

### Minor（建议修复，不影响结论）
- relay_driver_set()读改写原子性 — 主路径用apply()不用set()，风险低
- ADC3_IRQHandler止血式处理 — 不完美但不崩
- Flash 20ms临界区 — save频率低(600s一次)，可接受
- 压缩机继电器所有权耦合 — 行为可预测

### 必须修复项（Claude Code 下轮执行）
- screen_protocol.c lock提到if/else外层 ✅ 已完成

### 审计范围
- 修改文件：bsp_config.h, relay_driver.c/h, pwm_driver.c/h, ntc_sensor.c,
  temp_control.c, humidity_control.c, control_timers.c, uart_driver.c,
  adc_driver.c, flash_storage.c, screen_protocol.c, main_pcb_test.c,
  main_diag.c, ACCEPTANCE_PLAN.md, PCB_TEST_PROCEDURE.md, HARDWARE_PIN_MATRIX.md
- 是否越界：否
- 测试覆盖：25个.c语法检查通过，Python测试84/84（协议层，不覆盖GPIO）

---

## Batch 04-R2 — screen_protocol回归修复 + 终审
日期：2026-04-09
阶段：P3
验收项：回归修复验证 + 全项目终审

### 结论：PASS WITH RISKS — GO for PCB test firmware

### Critical
- 无

### Major
- 无

### Minor
- bsp_config.h:9 旧注释"PE4 is FENGJI-NEI2-IO" — 已修正
- temp_control.c:70 注释"PE3 PTC fan" — 已改为"PE6 PTC fan"
- bsp_config.h:118 过期TODO — 已清理
- PROJECT_RULES.md:201 PWM旧引脚 — 已更新为PE5/PE6/PC13

### 建议验证项（人工硬件验证）
1. PC13 PWM波形边沿质量（示波器）
2. PB14/PB15实际功能（万用表追线）
3. HSE晶振启动（上电后看SystemCoreClock输出）
4. 9路继电器通路（万用表 MCU→ULN→继电器线圈）

### 审计范围
- 28项检查：27 PASS / 1 CONCERN / 0 FAIL
- 全部25个.c + 18个.h + 7个.md + 3个.py + Makefile/LD/启动文件
- GPIO全量交叉验证：0处残留旧值

---

## Batch 04-R3 — 根目录文档终审
日期：2026-04-09
阶段：P3
验收项：根目录PROJECT_RULES/CURRENT_TASK/AUDIT_LOG一致性

### 结论：PASS

### 修复内容
- PROJECT_RULES.md:201 PWM引脚从PE2/PE3/PE4更正为PE5/PE6/PC13
- CURRENT_TASK.md 从"PCB未到货"更新为"PCB测试固件就绪"
- AUDIT_LOG.md 从空模板填入3轮审计记录

### 已知开放风险（不阻塞上电）
1. PC13硬件边沿 — 待实测
2. PB14/PB15功能归属 — 待确认
3. Flash 20ms临界区 — 正式固件长跑风险
4. 压缩机继电器耦合 — 可预测但不优雅
5. 护理灯驱动缺失 — GPIO对但代码未接入
6. 新增外设未接入 — 6个引脚无驱动

---

## Batch 04-R4 — PB4/JTAG根因修复 + PCB逐路继电器验证
日期：2026-04-11
阶段：P3
验收项：WH(PB4)继电器不工作根因排查与修复

### 结论：PASS — 软件根因已修复

### 根因
PB4 = STM32F103的JNTRST（JTAG复位引脚），上电默认被JTAG占用。
全工程缺少 `__HAL_AFIO_REMAP_SWJ_NOJTAG()` 调用，导致PB4的GPIO输出无效。
Codex独立审查发现此问题。

### 修复内容
所有7个含main()的文件在HAL_Init()之后加入：
```c
__HAL_RCC_AFIO_CLK_ENABLE();
__HAL_AFIO_REMAP_SWJ_NOJTAG();
```
修复文件：main.c, main_pcb_test.c, main_relay_test.c, main_wh_debug.c,
          main_diag.c, main_uart_debug.c, main_test.c

### PCB逐路继电器实测结果（修复后）
| Relay | GPIO | PCB器件 | click | LED |
|-------|------|---------|-------|-----|
| 0 PTC | PE1 | U8 | ✅ | ✅ |
| 1 JIARE | PE0 | U12 | ✅ | ✅ |
| 2 RED | PB9 | U10 | ✅ | ✅ |
| 3 ZIY | PB8 | U14 | ✅ | ✅ |
| 4 O2 | PB7 | U16 | ✅ | ✅ |
| 5 JIASHI | PE4 | U18 | ✅ | ✅ |
| 6 FENGJI | PE3 | U20 | ✅ | ✅ |
| 7 YASUO | PE2 | U23 | ✅ | ✅ |
| 8 WH | PB4 | U30旁 | ✅ | ✅ |

9/9全部通过。

### 同期确认
- HSE晶振72MHz正常（逻辑分析仪确认HSERDY=SET, PLLRDY=SET）
- CN1/CN3电平转换有信号（逻辑分析仪确认。注：CN3=UART1屏幕板，CN1=UART5 JFC103）
- tasks.c debug注释从"UART2"更正为"UART1"

---

## Batch 04-R5 — 正式固件烧录前终审
日期：2026-04-11
阶段：P3
验收项：V4终审 20项检查

### 结论：GO — 16/20 PASS, 2 CONCERN, 3 FAIL(文档)

### FAIL项（已修复）
- HARDWARE_PIN_MATRIX.md 补入9路PCB器件编号 ✅
- AUDIT_LOG.md 补入AFIO根因修复记录 ✅
- PROJECT_RULES.md 补入AFIO规则 ✅

### CONCERN项（已知，不阻塞）
- UART 5路同优先级
- TIM6 PSC硬编码在HSI回退时频率偏移

---

## Batch 05 — 10项定向修复 + 传感器/iPad联调
日期：2026-04-12
阶段：P4

### 结论：PASS — 全部联调通过

### 修复内容
- 传感器fail-safe：temp/humidity/oxygen sensor无效时清危险继电器
- 除湿加外风机：DEHUMIDIFY同时设YASUO+FENGJI
- iPad 0x03启动计时：写fog/disinfect后调control_timers_start_*()
- 屏幕0x83加锁：TIMER_CTRL分支加app_data_lock/unlock
- 计时蜂鸣提示：timer_beep_request/counter + AlarmTask统一驱动PB3
- 蜂鸣器仲裁修复：AlarmTask成为PB3唯一驱动者
- xTaskCreate逐个检查：替换&=聚合，逐个!=pdPASS
- 协议回包一致性快照：lock+局部变量+unlock
- RTOS创建检查：fatal_init_error() + NULL/pdPASS检查
- IWDG看门狗：寄存器直操PSC=64 RLR=2500 ~4s，SystemTask喂狗
- Makefile加$(CFLAGS_EXTRA)
- BUILD_DEPLOY.md debug UART改UART4(CN16)
- JFC103自适应启动：Phase1发0x8A→收帧后Phase2停发→超时10s重发

### 传感器联调结果
- NTC: 27.7°C ✅
- CO2: 1384-1570ppm ✅
- O2: 21.0%, 湿度39.4% ✅
- JFC103: HR=64 SpO2=95 ✅

### iPad协议联调：6/6通过

### 硬件发现
- CN4旁电阻未短接→5VS-IPAD无电→115200乱码。外接5V后正常。
- JFC103持续发0x8A会reset算法→HR/SpO2永远0。自适应策略解决。

### 编译与测试
- 0错误, ~22.8KB
- 101 passed, 0 failed
