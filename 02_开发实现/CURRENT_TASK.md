# CURRENT_TASK.md

> 每轮开始前更新此文件，完成后归档到 AUDIT_LOG.md

---

## 批次编号
Batch 06 — 传感器+iPad协议联调完成

## 任务名称
传感器4路联调 + iPad协议6/6通过 + 10项安全修复 + IWDG看门狗 + JFC103自适应

## 对应开发阶段
P4 (联调)

## 最新进展（2026-04-12）

### 传感器联调结果 — 全部通过
- NTC: 27.7°C ✅
- CO2: 1384-1570ppm ✅
- O2: 21.0%, 湿度39.4% ✅
- JFC103: HR=64 SpO2=95 ✅

### iPad协议联调：6/6通过

### 10项安全修复（全部完成）
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

### 其他完成项
- Makefile加$(CFLAGS_EXTRA)
- BUILD_DEPLOY.md debug UART改UART4(CN16)
- JFC103自适应启动：Phase1发0x8A→收帧后Phase2停发→超时10s重发

### 硬件发现
- CN4旁电阻未短接→5VS-IPAD无电→115200乱码。外接5V后正常。
- JFC103持续发0x8A会reset算法→HR/SpO2永远0。自适应策略解决。

### 编译与测试
- 0错误, ~22.8KB
- 101 passed, 0 failed

## 下一步
1. 屏幕板联调（CN1/UART1）
2. CN4电阻焊接（短接5VS-IPAD供电）
3. 整机测试
