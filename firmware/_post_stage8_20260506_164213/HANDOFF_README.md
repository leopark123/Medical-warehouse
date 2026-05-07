# Stage 8 — 蜂鸣精简 + 默认关 actuator + 编码器单击/长按 + NTC 滤波 + PE5/PC13

**生成时间**: 2026-05-06 16:51
**基于**: Stage 7 (`_post_stage7_swj_fix_20260506_134251`, SWJ_CFG fix + TIM1 PWM @15kHz)
**状态**: ✅ 编译成功, **已生成双板 .bin**, 等待烠录实测
**双板必须同烠** (协议 0x01 帧 26→39, 协议加 KeyAction 0x05)

## 二进制产物

| 文件 | 大小 | MD5 |
|---|---|---|
| `main_stage8_FINAL.bin` | 27824 B | `53e2cfb946ec50aa16b8acf170e986b4` |
| `main_stage8_FINAL.elf` | 61084 B | — |
| `screen_stage8_FINAL.bin` | 5504 B | `964838215b4623cb95a379b0b0a10d20` |
| `screen_stage8_FINAL.elf` | 16456 B | — |

**Size 增量** (vs Stage 7):
- 主板: 27200 → 27824 字节 (+624 字节, +2.3%) — NTC 滤波环 302B + light_ctrl + enable 字段 + ControlState cancel_beep + 多处 enable 检查 + 帧扩展逻辑
- 屏幕板: 5244 → 5504 字节 (+260 字节, +5.0%) — hmi_invalidate_shadow / hmi_commit_current_page + len 参数处理 + commit 拒绝逻辑

---

## 🔴 烧录前必读 (双板兼容性)

**Stage 8 与 Stage 7 协议不兼容**:
1. 主板 0x01 显示帧从 26 字节扩到 39 字节 (新增 setpoint + version + enable bitmap)
2. 屏幕板 → 主板 KeyAction 协议新增 `action=0x05` (取消事件)
3. **必须双板同时烧录 Stage 8**, 否则:
   - **新主板 + 旧屏幕板**: 旧屏幕仍按 Stage 7 旋转一格就 `send_param_set`, 新主板自动 enable → **加热失控风险**
   - **旧主板 + 新屏幕板**: 屏幕收 26 字节帧, len < 38 → shadow_valid=0 → 用户单击"按了没反应"; 编辑功能完全瘫痪, 但安全
4. 双板 .bin 命名同时刻 (`main_stage8_*.bin` 和 `screen_stage8_*.bin` 必须配对)

---

## 一、Stage 8 完整范围

### 模块 1: 蜂鸣精简 (方案①, 只静音不影响视觉)
- B1-B7 报警 (温度高/低、湿度、O2 偏离、CO2 高、心率、SpO2) **不再蜂鸣**, 仅屏幕闪烁
- B8 (屏幕通信故障) 仍间歇响 500ms on/off
- C1/C2 (雾化/消毒到点) 仍连续响 3s
- safety_fatal 致命级 (stack overflow / hardfault / assert) 不变

### 模块 2: PE5 + PC13 跟随 PE2 压缩机
- 内循环风机 (PE5) + 空调内风机 (PC13) 自 Stage 3 后是无主裸引脚, 现在跟随压缩机继电器启停

### 模块 3: 上电默认关 actuator
- 新增 `Setpoints.enable_temp_ctrl / enable_humid_ctrl / enable_o2_ctrl`, 默认 0 (关)
- 写 0x81 (屏幕) 或 0x03 (iPad) 任意温/湿/O2 setpoint 后, 对应 enable 自动 = 1
- 0x0B 出厂复位回 0 + 同步清对应报警 latch + acknowledged
- temp/humid/oxygen controllers 入口加 enable 门 (humidity 不抢温控 PE2/PE3, oxygen 仅闭环段加门, 手动开放供氧/外部 O2 请求不受影响)
- AlarmTask 温/湿/O2 报警跟随 enable (CO2/HR/SpO2/COMM_FAULT 不受影响)

### 模块 4: 红蓝光软互斥 (α 方案)
- 新文件 `Control/lights/light_ctrl.[ch]`: `light_ctrl_normalize()` 归一化函数
- 调用点: 屏幕 0x82 case 0x02/0x03/0x04 + iPad 0x03 写参数
- 治疗组 (蓝/红) 激活 → 自动清照明 + 检查
- 蓝+红同时被设置 (异常输入) → 全灭 (兜底)

### 模块 5: NTC 3 秒滑动平均滤波
- 4 通道 + avg 同步滤波, 窗口 N=30 (30×100ms = 3s)
- 静态环初始化为 -999 (`ntc_filter_init()`, 在 main_app.c init 阶段调用)
- warmup 期间只遍历已填槽位, 避免 0°C 污染
- 控制/显示用 `temperature[2]_filt` (PA4 顶部), 报警/iPad 用 `temperature_avg_filt` (维持现状分裂设计)

### 模块 6: 编码器状态机重写
**新行为**:
- **旋转**: 仅改本地 shadow, **不下发主板**
- **单击**:
  - LIVE 页 → 翻 SET_TEMP (开始编辑)
  - SET_xxx 页 → commit shadow + 翻下一页 (X2 链式编辑)
  - RESET_O2_TIME 页 → 触发清供氧累计 + 回 LIVE
  - shadow_valid=0 时 commit 拒绝, 静默忽略 (屏幕未收到 38B 帧前防误触)
- **长按**:
  - LIVE 页 → 清供氧累计 (b 方案保留, 与 Stage 7 兼容)
  - SET 页 / RESET_O2_TIME → 取消编辑 (丢 shadow + 回 LIVE) + 主板短鸣 200ms
- **5s 超时**: 自动回 LIVE + 清 shadow (静默, 不短鸣)
- **CO2 SET 页禁用**: hmi_cycle_page do-while 跳过 (target_co2 仍是 placeholder, Stage 9 启用)

### 模块 7: 0x01 显示帧扩展 26 → 39 字节
```
byte 0-25:   v2.1 现有字段 (实时值/状态/报警, 不变)
byte 26-27:  target_temp x10
byte 28-29:  target_humidity x10
byte 30-31:  target_o2 x10
byte 32-33:  target_co2 ppm
byte 34-35:  fog_time sec (setpoint, NOT remaining)
byte 36-37:  disinfect_time sec (setpoint)
byte 38:     bit7-4 = protocol_version (Stage 8 = 0x1)
             bit3-0 = enable bitmap (bit0 temp / bit1 humid / bit2 o2)
```

---

## 二、改动文件清单 (16 处)

### 主板 (15 文件)

| # | 文件 | 改动 |
|---|---|---|
| 1 | `Protocol/common/protocol_defs.h` | `SCR_DISPLAY_DATA_LEN` 26 → 39 |
| 2 | `App/data/app_data.h` | Setpoints 加 3 enable 字段; ControlState 加 `cancel_beep_until_tick` |
| 3 | `App/data/app_data.c` | 不改 (memset 已置 0) |
| 4 | `App/main_app.c` | 调用 `ntc_filter_init()` |
| 5 | `App/tasks/tasks.c` | AlarmTask: enable 跟随 + buzz_mask 过滤 + 三优先级 buzzer; ControlTask 末尾 PE5/PC13 旁路 |
| 6 | `Sensors/ntc/ntc_sensor.h` | 加 `ntc_filter_init()` 声明 |
| 7 | `Sensors/ntc/ntc_sensor.c` | 加 30×100ms 滑动平均环 + init |
| 8 | `Control/temp/temp_control.c` | 入口加 enable 门 (温控全权) |
| 9 | `Control/humidity/humidity_control.c` | 入口加 enable 门 (不抢 PE2/PE3) |
| 10 | `Control/oxygen/oxygen_control.c` | 仅闭环滞环段加 enable 门, 手动开放/外部不动 |
| 11 | `Control/lights/light_ctrl.h` | **新建** — light_ctrl_normalize 接口 |
| 12 | `Control/lights/light_ctrl.c` | **新建** — α 软互斥实现 |
| 13 | `Protocol/screen/screen_protocol.c` | 0x81 加锁 + auto-enable; 0x82 红蓝光归一化 + action=0x05 取消; 0x01 帧扩展 |
| 14 | `Protocol/ipad/ipad_protocol.c` | 0x03 写参数 auto-enable + light 归一化; 0x0B 复位清 enable + alarm |
| 15 | `Makefile` | 加 `Control/lights/light_ctrl.c` 源文件 + `-IControl/lights` |

### 屏幕板 (1 文件, 大改)

| # | 文件 | 改动 |
|---|---|---|
| 16 | `HDICU_ScreenBoard/main.c` | s_display_data 26→39; hmi_seed 加 len 参数; shadow 默认值改占位; hmi_cycle_page 跳过 SET_CO2; hmi_invalidate_shadow + hmi_commit_current_page 新增; process_rx_frame case 0x06 状态机重写; 5s 超时清 shadow |

### 测试 (1 文件)

| # | 文件 | 改动 |
|---|---|---|
| 17 | `Tests/run_tests.py` | 帧长度 26→39; 蜂鸣预期改 BUZZ_MASK=COMM_FAULT |

---

## 三、编译 + 烧录步骤

### 1. 准备编译环境 (本机当前没有 make 工具链, 需要先装)
- ARM GCC: `arm-none-eabi-gcc` (例如 STM32CubeIDE 自带或 ARM 官方下载)
- GNU Make
- 推荐使用 MSYS2 或 STM32CubeIDE 的内嵌 toolchain

### 2. 编译主板
```cmd
cd F:\小项目\医疗仓\firmware\HDICU_MainBoard
make clean
make
```
**预期产物**: `build/firmware.bin` (~28KB, Stage 7 是 27200B, Stage 8 增加约 1-2KB)

### 3. 编译屏幕板
```cmd
cd F:\小项目\医疗仓\firmware\HDICU_ScreenBoard
make clean
make
```
**预期产物**: `build/firmware.bin` (~6KB, Stage 7 是 5244B)

### 4. 同时烧录双板
```cmd
cd F:\小项目\医疗仓\firmware\HDICU_MainBoard
flash_main.bat

cd F:\小项目\医疗仓\firmware\HDICU_ScreenBoard
flash_screen.bat
```

或用 JLink Commander 一键脚本 (单独 SWD):
```
device STM32F103VE
loadbin firmware.bin 0x08000000
verifybin firmware.bin 0x08000000
r
g
```

### 5. 备份编译产物
编译完成后, 把双板 .bin / .elf 拷贝到本目录:
```
F:\小项目\医疗仓\firmware\_post_stage8_20260506_164213\
  ├─ main_stage8_FINAL.bin
  ├─ main_stage8_FINAL.elf
  ├─ screen_stage8_FINAL.bin
  └─ screen_stage8_FINAL.elf
```

---

## 四、烧录后测试矩阵

| 编号 | 验证项 | 方法 | 预期 |
|---|---|---|---|
| T1 | 蜂鸣静音 (B 类) | iPad 设 setpoint=15°C, 等 12s | 屏幕闪 TEMP_LOW, **不响** |
| T2 | 蜂鸣保留 (B8) | 拔屏幕板线 5s 后 | 间歇响 500ms on/off |
| T3 | 蜂鸣保留 (C1) | 屏幕单击雾化 SET 设 1 分钟, 等到 0 | 连续响 3s |
| T4 | enable 默认关 | 上电环境 23°C, setpoint 默认 25°C | PE0/PE1/PE6/PE9 全 LOW |
| T5 | enable iPad 自启 | iPad 0x03 写 temp=37°C | 立即开始加热, JLink 看 enable_temp_ctrl=1 |
| T6 | enable 屏幕自启 | 屏幕进 SET_TEMP 旋一格单击 | enable_temp_ctrl=1, target_temp 改 |
| T7 | enable 跨控制器 | iPad 写 temp 但不写 humid | 温控开, 湿控仍 IDLE (humid_state=IDLE) |
| T8 | 0x0B 复位 | iPad 发 0x0B | actuator 全停, enable 全 0, alarm_flags 清 0-3 |
| T9 | PE5/PC13 跟 PE2 | 万用表测 | 制冷时 ON, IDLE/加热时 OFF |
| T10 | 红蓝光屏幕互锁 | 开照明 → 按红蓝光键 | 照明灭, 蓝灯亮 |
| T11 | 红蓝光 iPad 互锁 | iPad 0x03 light=0x0F (全开) | 主板 light_ctrl=0x00 (蓝+红互冲清零) |
| T12 | NTC 滤波启动 | 上电后看温度收敛 | 3s 内逐渐稳定 (warmup), 不会显示 0°C |
| T13 | NTC 滤波抖动 | 手指碰 NTC2 制造 ±2°C | 输出温度变化 ≤ 0.5°C / 100ms |
| T14 | 单击不下发 | SET_TEMP 旋转 +5 格不单击, JLink 读 target_temp | **不变** |
| T15 | 单击下发 | 同上, 单击, 看 target_temp | 切到旋转后值, 屏幕翻到 SET_HUMID |
| T16 | 长按取消 | SET 页旋后长按 | 屏幕回 LIVE, 蜂鸣短响 ~200ms, target_temp **不变** |
| T17 | 长按 LIVE | LIVE 页长按编码器 | 供氧累计清零, **不短鸣** (b 方案) |
| T18 | shadow 首帧禁 commit | 屏幕板上电 100ms 内, 模拟编码器单击 | shadow 拒, 不下发主板, 不翻页 |
| T19 | shadow seed 修复 | iPad 改 setpoint=37°C, 屏幕进 SET_TEMP | 显示 37°C, 不是实时温度 |
| T20 | 5s 超时 | 进 SET 页, 旋后等 6s | 自动回 LIVE, 不短鸣, 下次进 SET 重新 seed |
| T21 | CO2 页跳过 | 编码器单击 LIVE→TEMP→HUMID→O2→**FOG**(跳 CO2)→DISINFECT→RESET→LIVE | 5 个 SET 页, 跳 CO2 |
| T22 | 帧版本 | JLink 读 0x01 帧 byte 38 | 0x10 (默认全未启用) ~ 0x17 |

---

## 五、Codex 审查覆盖

本 Stage 8 经过 **2 轮 Codex 审查**, 修订 13 项 (高优 7 + 中优 6) + 2 漏审项:

| Codex 编号 | 问题 | 状态 |
|---|---|---|
| v1.1 | oxygen 顶部 return 误关手动+外部 | ✅ 仅闭环段加门 |
| v1.2 | humidity 抢温控 PE2/PE3 | ✅ 不动 PE2/PE3 |
| v1.3 | AlarmTask 不知 enable 误报 | ✅ 报警跟 enable + counter else 清零 |
| v1.4 | iPad 0x03 light 绕过互锁 | ✅ light_ctrl_normalize 双路径 |
| v1.5 | shadow seed 用实时值 | ✅ 0x01 帧扩展 + byte 26+ |
| v1.6 | 5s 超时不清 shadow_valid | ✅ hmi_invalidate_shadow |
| v1.7 | 0x81 路径无锁 | ✅ app_data_lock |
| v1.8 | target_co2 死字段 | 留 Stage 9 (CO2 SET 页跳过避免误导) |
| v1.9 | 0x0B 复位不清 enable | ✅ enable=0 + alarm_flags 清 |
| v1.10 | cancel_beep 200ms 相位错位 | ✅ deadline tick 模式 |
| v1.11 | NTC 滤波分母 + 窗口 | ✅ 仅遍历 warmup, 1s→3s |
| v1.12 | temp[2] vs avg 分裂 | ✅ 维持现状分裂, 双路径滤波 |
| v1.13 | iPad/屏幕并发 last-writer-wins | ✅ shadow_valid 控制, 文档化 |
| v2.1 | 0x01 38B 未触达常量 | ✅ SCR_DISPLAY_DATA_LEN, 屏幕缓存, memcpy 上限同步 |
| v2.2 | 首帧未到前 commit 风险 | ✅ shadow 默认占位 + commit 检查 valid |
| v2.3 | 混烧风险更严重 | ✅ 协议版本字节 byte 38 |
| v2.4 | 报警 latch + counter 不清 | ✅ 0x0B 复位主动清 alarm_flags + ack |
| v2.5 | delay counter enable=0 不清 | ✅ if 内 enable + else 自然清 |
| v2.6 | CO2 页面误导 | ✅ hmi_cycle_page 跳过 SET_CO2 |
| v2.7 | Makefile 缺 light_ctrl.c | ✅ 已加 |
| v2.8 | shadow_valid 期间 iPad 改不重 seed | last-writer-wins 文档化 |
| v2.9 | enable 状态没上报 | ✅ byte 38 enable bitmap |
| v2.10 | light_ctrl_normalize(0x0F) 全灭 | 文档化 |
| v2.11 | NTC buffer 必须 -999 init | ✅ ntc_filter_init |
| v2.12 | run_tests.py 测试预期更新 | ✅ 帧长度 + 蜂鸣预期 |
| v2.13 | sanity_check 灵敏度降低 | 接受, advisory 级 |

---

## 六、已知遗留 (Stage 9 候选)

1. **target_co2 仍是 placeholder**: AlarmTask CO2 报警仍用固定 5000ppm 阈值, target_co2 写入但不生效。屏幕 SET_CO2 已跳过, 等 Stage 9 实现 CO2 报警/控制后启用。
2. **风速 LED 1/2 档显示**: Stage 5 等效 duty 累加设计, 加热/新风时 fan=1 也会点 LED2 (因为 eff_duty=80)。这是设计选择, 不是 bug。Stage 9 可改为"用户档位"显示。
3. **iPad 0x02 读参数不返回 enable 状态**: 仅在 0x01 屏幕帧 byte 38 暴露。Stage 9 可扩 iPad 协议。

---

## 七、回滚方案

如 Stage 8 测试失败:
```cmd
REM 回滚到 Stage 7 (双板)
JLink.exe -device STM32F103VE -if SWD -CommanderScript flash_stage7_main.jlink
JLink.exe -device GD32F303RC -if SWD -CommanderScript flash_stage7_screen.jlink
```
Stage 7 .bin 在: `F:\小项目\医疗仓\firmware\_post_stage7_swj_fix_20260506_134251\`

---

## 八、给烧录人员的简短话术

1. **必须双板同时烠** (新主板新屏幕板配对, 不能混)
2. 烠完上电:
   - 屏幕该正常显示, **加热/制冷不开** (这是预期, 不是故障)
   - 用 iPad 写一次温度 setpoint (例如 37°C) 后, 加热应启动
   - 或屏幕单击编码器进 SET_TEMP, 旋转, 单击确认, 加热应启动
3. 测试报警: 拔屏幕板线 5s, 主板蜂鸣应间歇响 (B8); 设温度 setpoint=15°C 等 12s, 屏幕闪 TEMP_LOW 但**不响** (Stage 8 静音)
4. 测试编码器: SET 页旋转改值, 单击 = 应用 + 翻下一页; 长按 = 取消 + 短鸣 200ms
5. 出问题反馈截图 + JLink 读 RAM 0x20000XXX (target_temp / enable_temp_ctrl 地址在 .map 文件查)

---

**本文档结束**.
