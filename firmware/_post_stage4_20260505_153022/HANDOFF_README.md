# 烧录交付包 — Stage 1~4 全部完成

**生成时间**：2026-05-05 15:30:22
**状态**：4 个 Stage 改动全部编译通过，**未实测**（待烧录人员验证）
**对应文档**：`02_开发实现/HDICU_屏幕板主板改动确认书_v1.2.md`

---

## 一、固件文件（直接烧录）

| 板 | 文件 | 大小 | MD5 | 烧录地址 |
|----|------|------|------|---------|
| **主板** STM32F103VET6 | `main_stage4_FINAL.bin` | 26960 B | `27bc447d089deaf21cd4fd4e0c05f956` | `0x08000000` |
| **屏幕板** GD32F303RCT6 | `screen_stage4_FINAL.bin` | 5140 B | `3489ae04362590fee3f027c570885b2a` | `0x08000000` |

ELF 文件附带（含调试符号，烧录可不用）：
- `main_stage4_FINAL.elf` — 主板调试符号
- `screen_stage4_FINAL.elf` — 屏幕板调试符号

---

## 二、烧录命令（JLink Commander）

### 主板
```
si SWD
speed 4000
device STM32F103VE       ← 主板必须用此名
connect
h
loadbin <临时路径>\main_stage4_FINAL.bin 0x08000000
verifybin <临时路径>\main_stage4_FINAL.bin 0x08000000
r
g
q
```

### 屏幕板
```
si SWD
speed 1000
device GD32F303RC        ← ⚠️ 屏幕板必须用 GD32 名, 不能用 STM32 (会触发 mass erase + RDP 锁死!)
connect
h
loadbin <临时路径>\screen_stage4_FINAL.bin 0x08000000
verifybin <临时路径>\screen_stage4_FINAL.bin 0x08000000
r
g
q
```

**⚠️ 屏幕板烧录必读**：device 名**必须是 `GD32F303RC`**。用 STM32F303RC 名字会触发 JLink 自动 unsecure 流程，写错 OB 字节，导致 RDP Level 1 锁死，恢复需要手动 SWD 写 OB 寄存器（见 memory `feedback_jlink_gd32.md`）。

---

## 三、烧录前后验证清单

### 烧录前
- [ ] JLink V9（或更高）已正确连到目标板 SWD 口（PA13/PA14/3V3/GND）
- [ ] 板子电源稳定（VTref 应显示 ~3.3V）
- [ ] 文件 MD5 校验匹配上表

### 烧录后启动验证
- [ ] **主板** — 启动 5 秒内蜂鸣器**不响**（如响是 POST 自检失败，立刻断电）
- [ ] **屏幕板** — 启动 1 秒内**所有数码段全亮**（启动灯测）→ 灭 → 进入 LIVE 显示
- [ ] **CN12 串口监控**（PC USB-TTL 接 CN12，115200 8N1 HEX 模式）→ 应该看到屏幕板每 100ms 发一个 `AA 55 01 ...` 显示数据包

### 物理功能测试（参照确认书 v1.2 第 7 节）

#### Stage 1 验证
- [ ] **CN3 物理按键**：按一下 → 主板照明灯 PE11 切换（看 U32 第 2 颗 LED 切换）
- [ ] **CN8 物理按键**：连按 4 次 → 风速 0→1→2→3→0 循环
- [ ] **CN12 监听有数据** + **CN11 也有数据**（双发，硬件已修后）

#### Stage 2 验证
- [ ] CN8 按一次 (fan=1) → **LED8 (PA4) 亮**
- [ ] CN8 按一次 (fan=2) → **LED8 + LED9 (PB9) 都亮**
- [ ] CN8 按一次 (fan=3) → **LED8 + LED9 + LED10 (PD2) 三颗都亮**
- [ ] CN8 按一次 (fan=0) → **三颗全灭**

#### Stage 3 验证（**安全关键**）
按以下场景实测物理风机声音/转速：
- [ ] 仅加热（设温度比环境高 10°C）+ fan=0 → PTC 风机 ~80% 转
- [ ] 加热 + fan=1 → PTC 风机 ~80%（安全 > 用户）
- [ ] 加热 + fan=3 → PTC 风机 ~100%（用户 > 安全）
- [ ] 仅新风（按 CN6）+ fan=0 + 不加热 → PTC 风机 100%
- [ ] **加热 + 新风 + fan=0 → PTC 风机 100%**（max3 修正点，必测！）
- [ ] 全关 → PTC 风机 0%

#### Stage 4 验证
- [ ] 按编码器 → HMI 页面循环 8 项（LIVE→温→湿→O2→CO2→雾化→消毒→清供氧→LIVE）
- [ ] 在某编辑页旋转编码器 → 数值变化 + **当前编辑项 GRID 闪烁**（其他位实测值正常显示不受影响）
- [ ] 编码器调温度 → JLink 读 `target_temp` 应变化
- [ ] 编码器调 CO2 → JLink 读 `target_co2` 应变化
- [ ] 编码器调雾化 → 雾化器（U30）启动倒计时
- [ ] 旋转到清供氧页 + 按编码器键 → `o2_accumulated` 清零

---

## 四、回滚方案（如出问题）

### 烧前级回滚（不动板子）
```bash
git checkout pre-encoder-rework -- <文件路径>
```

### 烧后级回滚（板子已烧坏，回旧版固件）
位置：`firmware/_pre_rework_backup_20260505_151421/`
- `main_firmware_baseline.bin` — 原主板固件 (MD5 `c9671b1b`)
- `screen_fw_baseline.bin` — 原屏幕板固件 (MD5 `bf88a066`)

照上面"烧录命令"流程，把 `_FINAL.bin` 换成 `_baseline.bin` 即可。

---

## 五、源码改动总览（git diff stat）

```
firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.h     |  +20 -3
firmware/HDICU_MainBoard/Drivers/pwm/pwm_driver.c     |  +27 -9
firmware/HDICU_MainBoard/Control/temp/temp_control.c  |  +9 -10
firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c | +16
firmware/HDICU_MainBoard/App/tasks/tasks.c            |  +18 -10
firmware/HDICU_ScreenBoard/main.c                     | +290 -25
```

主要新增 / 改动：
- 主板：`pwm_set_ptc_arbiter()` 三方 max 仲裁 + screen 0x82 case 0x0B + 0x81 case 0x06
- 屏幕板：UART1 双发 + KEY_ID_MAP[7]=0x0B + LED_Fan_Init/LEDA_Update + HmiPage_t 8 项 + 7 个新 shadow + 闪烁逻辑改写

---

## 六、已知风险

| 风险 | 缓解 |
|------|------|
| Stage 3 PTC 仲裁安全关键 | 必须按"#### Stage 3 验证"6 种场景实测 |
| Stage 4 闪烁可能与 0x01 包刷新冲突 | 闪烁仅覆盖编辑页 GRID，其他位由 0x01 包正常更新（无冲突）|
| CN11 硬件未修则双发无效 | CN12 仍发，主板可回退到只用 CN12（无回归）|
| LED8/9/10 硬件接错引脚 | 已确认原理图证据 (PA4/PB9/PD2) |
| HMI 7 页太多用户找不到 | 5s 无操作自动回 LIVE 页（保持现状逻辑）|

---

## 七、未在本次改动范围

iPad APP 端 6 项 bug 待 APP 开发方修复（详见 `02_开发实现/HDICU_屏幕板主板改动确认书_v1.2.md` 第 8 节"已知不在本次改动范围"）。

---

**交付完成**。任何问题联系本文档作者（项目固件负责人）。
