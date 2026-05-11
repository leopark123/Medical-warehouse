# HDICU iPad APP 联调文档 — Stage 8 redo v4.3

**适用固件**: 主板 `main_stage8_redo_v4_3_FINAL.bin` (MD5 `6a5997fdf5c3d690d97d8480b602c601`)  
**v4.4 待烧** (代码已 commit, 新增 0x0C 命令, 等下次有板烧入)  
**生效日期**: 2026-05-07  
**协议版本**: v2.1 (帧格式不变, 字段语义有调整) + v4.4 新增 0x0C 命令

---

## 0. TL;DR (5 分钟版)

如果你之前对接过 v2.1 协议, **协议帧格式没动**, 但下面 6 处字段语义 / 行为变了, 必须看:

| # | 影响 | 一句话 |
|---|---|---|
| 1 | nursing_level | 范围 1~3 → **0~4** (5 档), 0=全灭, 4=双灯 |
| 2 | nursing 出厂默认 | 1 → **0** (上电护理灯全灭) |
| 3 | inner_cycle vs fresh_air | 双向互斥, 同时写 1 → 主板**静默清 cycle** |
| 4 | open_o2=1 行为 | 之前强制开新风, **现在强制关一切** (纯氧隔离模式) |
| 5 | o2_accumulated 累加口径 | 之前只算手动开放供氧, **现在算 O2 阀总开启时长** (含自动+外部) |
| 6 | UV / 雾化 vs 自动 O2 闭环 | 互锁让步时, 主板会**静默清 `enable_o2_ctrl`** |

---

## 1. 协议帧不变, 直接复用之前的实现

下面这些**没变**, APP 端代码不用动:

- 帧格式: `AA <CMD> <LEN> <DATA...> <CS> 55`, CS = (0xAA + CMD + LEN + DATA bytes) & 0xFF
- 命令码: 0x01/0x02 (读参数), 0x03/0x04 (写参数), 0x05/0x06 (读体征), 0x09/0x0A (限值), 0x0B (恢复出厂)
- 帧长度: 0x03 写参数 30 字节 / 0x02 响应 46 字节 / 0x06 响应 20 字节 等
- 字段排布: byte 偏移完全一样
- cancel_flags 语义: 0=不更新, 1=更新

**只有少数字段的"取值范围"或"主板写入后行为"变了**, 详见下文。

---

## 2. 字段变化详细说明

### 2.1 nursing_level — 5 档

#### 旧 (v3 及之前)
- 范围: **1, 2, 3** (3 档)
- 越界: APP 发 0 或 4 → 主板 OOB 拒绝, 整个 0x03 帧错误
- 出厂默认: 1
- 物理灯: 1 档点 PB1, 2 档点 PB0, 3 档点 PC5

#### 新 (v4.3)
- 范围: **0, 1, 2, 3, 4** (5 档)
- 越界: 仅当 `nursing > 4` 时拒绝 (0~4 全部接受)
- 出厂默认: **0** (上电护理灯全灭)
- 物理灯映射 (基于硬件丝印, 跟 BSP 命名顺序对调):

| 档位 | 亮的灯 | 含义 |
|---|---|---|
| 0 | (全灭) | 关闭护理 |
| 1 | PB0 | 一档 |
| 2 | PB1 | 二档 |
| 3 | PC5 | 三档 |
| 4 | PB1 + PC5 | 最高档, 双灯亮 |

#### APP 端建议
1. **UI 控件**: 改成 5 档选择 (radio / dropdown), 默认 0
2. **0x03 写参数** byte 13: nursing_level, 现在合法值是 0~4
3. **0x02 读响应** byte 13: 同上, 屏幕显示要支持 0
4. **0x0B 恢复出厂**后再读 0x02, nursing_level 现在会是 **0** (之前是 1)
5. **物理按键**用户单击 KEY1 也会循环 0→1→2→3→4→0, APP 显示要跟随

---

### 2.2 inner_cycle vs fresh_air — 双向互斥

#### 旧
- 两个字段独立, APP 可以一帧同时设 cycle=1 + fresh=1
- 物理按键 KEY6/KEY7 单击各自 toggle, 也可同时 1

#### 新 (v4.1 + v4.2)
- **同时不能为 1**, 任何写入 1 的动作都会强制清对方
- 优先级: **fresh_air 优先**

#### 主板归一化规则 (你必须知道, 否则会困惑)

##### 屏幕板按键
- 长按 KEY6 (CN4 内循环) → 翻转 `inner_cycle`. 0→1 时**自动清 `fresh_air = 0`**
- 单击 KEY7 (CN6 新风) → 翻转 `fresh_air`. 0→1 时**自动清 `inner_cycle = 0`**

##### iPad 0x03 写参数
APP 一帧同时设 `cycle=1 + fresh=1` 时, **主板写入后强制 `cycle = 0`** (跟 KEY7 行为一致, fresh 优先)。

```
APP 发: cancel_flags 全=1, cycle=1, fresh=1
主板内: setpoint.inner_cycle = 1; setpoint.fresh_air = 1;
       if (fresh_air && inner_cycle) inner_cycle = 0;   ← 静默归一化
APP 读 0x02 看到: cycle=0, fresh=1
```

#### APP 端建议
1. **UI 设计**: 把 inner_cycle 和 fresh_air 做成**互斥按钮组** (类似 radio), 而不是两个独立 toggle. 选项: "内循环 / 外循环-新风 / 关闭" 三态。
2. **写参数前**: 检查当前 UI 状态, 不要同时设两个 1 (主板会静默修改, APP 显示会跟用户操作不一致)
3. **写参数后**: 立即读 0x02 验证, 看主板是否归一化, 同步 UI

---

### 2.3 open_o2 = 1 — 纯氧隔离模式

#### 旧 (v3 及之前)
进入 open_o2 mode 时, 主板 interlock Rule 4 行为:
- O2 阀 ON
- **强制开新风** (`fresh_air = 1`)
- 强制关 inner_cycle
- 关加热 / 雾化 / UV
- **不强制关**: 制冷 / 除湿 / 加湿 / 风机 / 风速

APP 视角: 写 open_o2=1 后读回, fresh_air 会自动变 1.

#### 新 (v4.3)
进入 open_o2 mode = **完全隔离**, 只剩 O2 阀:

| 操作 | 旧 | 新 (v4.3) |
|---|---|---|
| O2 阀 (BSP_RELAY_O2_IO bit 4) | ON | ON ✓ |
| 新风 (`fresh_air`) | **强制 1** | **强制 0** |
| 内循环 (`inner_cycle`) | 强制 0 | 强制 0 |
| 加热 (PE1 / PE0) | 关 | 关 ✓ |
| 雾化 (PB4) | 关 | 关 ✓ |
| UV (PB8) | 关 | 关 ✓ |
| 制冷 / 除湿 (压缩机 PE2) | **不动** | **强制关** |
| 加湿器 (PE4) | **不动** | **强制关** |
| 空调外风机 (PE3) | **不动** | **强制关** |
| PTC 风机 PWM (PE6/PE9) | 100% | **0% 自动停转** |
| 风速档位 (`fan_speed_actual`) | 跟 setpoint | **强制 0** |

#### APP 视角

APP 写 `open_o2 = 1` 后, 读 0x02 看到的字段会大量变化:
```
原: fan_speed=2, inner_cycle=1, fresh_air=0, open_o2=1
读回: fan_speed=2 (setpoint 保留), fresh_air=0 (没变), inner_cycle=0 (被清),
     fan_speed_actual=0 (新加的, 强制为 0)
```

注意: **`setpoint.fan_speed` 不会被改** (用户原档位保留), 退出 OPEN_O2 后会恢复显示。`fan_speed_actual` 才是物理实际的风速。0x02 响应里返回的是 actual 还是 setpoint, 看协议手册定义。

#### APP 端建议
1. **OPEN_O2 模式 UI**: 显示"纯氧供氧中, 其他控制已暂停"
2. **不要再发**温度 / 湿度 / 风速等控制命令 (主板会接受 setpoint 但实际被互锁清掉, 用户困惑)
3. **退出 OPEN_O2** (`open_o2=0`) 后, 各控制器在下一拍恢复 (~200ms), APP 等几百毫秒再读 0x02 验证

---

### 2.4 o2_accumulated — 累加口径变化

#### 旧
- 累加条件: `setpoint.open_o2 == 1` 时, 每秒 +1
- 含义: "**手动开放供氧**累计时长"
- 不算: 自动闭环 (target_o2 > sensor.o2 → SUPPLYING) / 外部供氧机请求 (PD8 / PB6)

#### 新 (v4)
- 累加条件: **`relay_status & BSP_RELAY_O2_IO` (bit 4) 为 1 时**, 每秒 +1
- 含义: "**O2 阀总开启时长**"
- 包含: 手动开放供氧 + 自动 target_o2 闭环 + 外部供氧机 (PD8/PB6) 请求

#### 影响 APP 显示
- 之前显示"手动开放供氧累计 X 时长", **现在含义已扩大**
- 建议改文字标签为"O2 阀启用累计 X 时长"或类似
- 读取偏移没变 (0x02 响应中 byte 18-19, uint16 秒)

#### 计时清零路径
| 路径 | 清零条件 | 适用版本 |
|---|---|---|
| 屏幕板编码器在 LIVE 页长按 (KEY_ID 0x0A action 0x02) | 立即清 | v2 起 |
| 屏幕板编码器在 O2 计时页长按 (0x83 type=3 cmd=4) | 清 + 关 open_o2 + 清 enable_o2_ctrl | v2 起 |
| 屏幕板长按 KEY5 (CN2) 0→1 边沿 | 清, 重新计时 | v3 起 |
| iPad 0x83 type=3 cmd=3 (TIMER_CTRL 清零) | 清 | v2 起 |
| iPad 0x0B 恢复出厂 | **不清** o2_accumulated | v2 起 (历史遗留) |

---

### 2.5 自动 O2 闭环可能被互锁静默禁用

#### 场景
APP 通过编码器 / 写参数让 `target_o2 > sensor.o2_percent`, 触发主板 `oxygen_control` 进入 `SUPPLYING` 状态, 自动开 O2 阀。

##### 雾化或 UV 同时在跑时 (v4.1 + v4.2)
- 主板 Rule 5 (雾化) 或 Rule 2 (UV) 互锁: **强制清 `setpoint.enable_o2_ctrl = 0`**
- 自动闭环停止, O2 阀关
- APP 端 0x02 读回会发现 `enable_o2_ctrl = 0` (你之前没设, 但被清了)

#### APP 端处理
- 写 target_o2 / 启用 enable_o2_ctrl 后, 读 0x02 验证 enable_o2_ctrl 实际值
- 如果发现 `enable_o2_ctrl=0`, 说明被互锁清了 (检查是否雾化或 UV 在跑)
- UI 提示: "雾化期间无法启用自动供氧" / "UV 消毒期间无法启用自动供氧"

---

### 2.6 雾化 / UV 启动前的 O2 阀检查 (v4.1)

#### 旧
雾化启动前 (`interlock_can_start_fogging`) 只看 `setpoint.open_o2`. 如果 APP 写 open_o2=0 + enable_o2_ctrl=1 + sensor.o2 < target_o2 (= 自动闭环触发开 O2 阀), 然后 APP 通过 0x83 启动雾化, 主板会**允许雾化启动**, 之后才被 Rule 5 关掉, 残留几百毫秒"O2 阀+雾化"状态。

#### 新 (v4.1+)
- `interlock_can_start_fogging`: 检查 `relay_status & BSP_RELAY_O2_IO`, 任何 O2 阀路径开启都拒绝雾化启动
- 同样 UV: `interlock_can_start_uv` 也加了

#### APP 端处理
- 启动雾化失败时 (0x83 没收到预期响应或屏幕板没显示倒计时), 检查 O2 阀是否开 (读 0x02 byte 16-17 relay_status, bit 4)
- UI 提示: "O2 阀启用中, 无法启动雾化"

---

## 3. 物理按键行为变化 (UI 显示需跟随)

### 3.1 KEY1 (CN1, 护理等级) — 5 档循环
```
单击 → 0 (全灭) → 1 (PB0) → 2 (PB1) → 3 (PC5) → 4 (PB1+PC5) → 0
```
APP 端如果有"按键模拟"功能, 调用现有 0x82 KEY_ID=0x01 action=0x01 即可, 主板自动循环。

### 3.2 KEY5 (CN2, 开放供氧) — 长按 2 秒
- **单击不响应** (防误触, v3 起)
- **长按 2 秒** 翻转 `setpoint.open_o2`
- **0→1 边沿**: 清 `o2_accumulated = 0`, 重新计时
- **1→0**: 不清 (保留累计值)

### 3.3 KEY6 (CN4, 内循环) — 长按 2 秒 + 互斥
- **单击不响应** (v3 改, 防上电误触发)
- **长按 2 秒** 翻转 `setpoint.inner_cycle`
- **0→1 边沿**: 清 `setpoint.fresh_air = 0` (互斥)

### 3.4 KEY7 (CN6, 新风) — 单击 + 互斥
- **单击** 翻转 `setpoint.fresh_air`
- **0→1 边沿**: 清 `setpoint.inner_cycle = 0` (互斥)

### 3.5 KEY8 (CN8, 风速档位)
- **单击** 循环 `setpoint.fan_speed`: 0 → 1 → 2 → 3 → 0

### 3.6 LEDA5 (PC13, 开放供氧灯) — 阀实际开就亮
- 显示语义: **跟随 O2 阀状态** (`relay_status & bit 4`)
- 包含: 手动 open_o2 + 自动闭环 + 外部 demand
- 注意: 灯亮**不一定**等于"用户主动开了开放供氧"
- APP 显示: 如果 APP 也有"开放供氧"指示, 建议跟 LEDA5 一致 (查 byte 17 bit 4)

---

## 4. APP 端的"被动归一化"清单 (重要)

主板会**静默修改** APP 写入的 setpoint 字段。APP 必须在写入后**立即读回**才能看到真实值。

| APP 写入 | 主板可能静默修改 | 触发条件 |
|---|---|---|
| `cycle=1, fresh=1` | `cycle = 0` | 总是 (互斥归一化) |
| `open_o2 = 1` | `fresh_air = 0`, `inner_cycle = 0` | 进入 OPEN_O2 mode 后 (interlock Rule 4) |
| `enable_o2_ctrl = 1` + 雾化 OR UV 在跑 | `enable_o2_ctrl = 0` | Rule 5 / Rule 2 让步 |
| 任何让 `open_o2 = 1` 的写入 + 雾化在跑 | `open_o2 = 0` | Rule 5 (雾化优先) |

**最佳实践**:
1. 写入后 200ms 内读回验证
2. 发现归一化时, 同步 UI 显示真实状态
3. 不要假设 APP 写入后 setpoint 一定保持

---

## 5. 已知未变化的 (APP 不用动)

| 类别 | 状态 |
|---|---|
| 帧格式 (header / CS / 长度) | 完全不变 |
| 0x03 写参数 30 字节布局 | 不变 (注意 nursing 范围扩大) |
| 0x02 响应 46 字节布局 | 不变 |
| 0x06 体征响应 20 字节 | 不变 |
| 0x05 读体征请求 | 不变 (轮询频率建议每秒 1 次) |
| 0x09 / 0x0A 限值 | 不变 |
| 0x0B 恢复出厂 | 行为微调 (nursing 默认 0), 字段不变 |
| 错误码 | 不变 |

---

## 6. 测试用例 (推荐 APP 端跑)

### TC-1: nursing 5 档边界
- 写 `nursing=0` → 接受, 读回 0
- 写 `nursing=4` → 接受, 读回 4
- 写 `nursing=5` → **拒绝** (NURSING_OOB)

### TC-2: 互斥归一化
- 写 `cycle=1, fresh=0` → 读回: cycle=1, fresh=0 ✓
- 写 `cycle=0, fresh=1` → 读回: cycle=0, fresh=1 ✓
- 写 `cycle=1, fresh=1` → 读回: **cycle=0, fresh=1** (静默清 cycle)
- 主板物理: 屏幕板 LEDA6 (内循环灯) 灭, LEDA7 (新风灯) 亮

### TC-3: OPEN_O2 隔离
- 上电默认状态 (温度 / 湿度自由控制)
- 写 `open_o2 = 1`
- 等 500ms, 读 0x02
- 期望: `inner_cycle=0`, `fresh_air=0`, fan_speed_actual=0
- 主板物理: 风机停 / PTC 0% / 压缩机关 / 加湿关
- 写 `open_o2 = 0`
- 等 500ms, 读 0x02
- 期望: 各控制器恢复闭环, 跟温度 / 湿度 setpoint 决策

### TC-4: 互锁静默禁用
- 启动雾化 (0x83 type=1 cmd=1, duration=60)
- 等待 1 秒, 雾化中
- 写 `enable_o2_ctrl=1, target_o2=900`
- 等 500ms, 读 0x02
- 期望: `enable_o2_ctrl = 0` (被 Rule 5 清)
- UI 提示用户

### TC-5: O2 累计语义
- 上电 (o2_accumulated = 0)
- 启用自动闭环 (enable_o2_ctrl=1, target_o2 > sensor.o2)
- 等 30 秒
- 读 o2_accumulated
- 期望: ≥ 30 (新口径; 旧口径会是 0 因为没手动 open_o2)

### TC-6: 出厂复位
- 写参数 (nursing=3, target_temp=350)
- 发 0x0B 恢复出厂
- 读 0x02
- 期望: nursing=0 (新), target_temp=250

---

## 7. 常见问答 FAQ

### Q1: APP 之前发 nursing=2 一直工作好的, 升级后还需要改吗?
**A**: 你的 nursing=2 完全合法, 不需要改. 但如果 UI 想支持新档 0 / 4, 才要改 UI 控件。

### Q2: 写 cycle=1 + fresh=1 主板拒绝吗?
**A**: 不拒绝, 接受了但**静默**把 cycle 清 0. APP 读回看到的是修改后状态。

### Q3: 上电后我读 nursing_level 是 0, 之前是 1, 是不是 bug?
**A**: 这是 v4.3 新行为, 上电护理灯全灭。如果你的 UI 默认显示档 1, 改成默认显示档 0。

### Q4: 屏幕显示的"供氧时间"跟 APP 之前看的对不上, 怎么回事?
**A**: 累加口径变了, 现在含**所有 O2 阀开启时长** (含自动闭环 + 外部供氧机), 不只是手动开放供氧. 如果 APP 显示文字是"开放供氧时长", 改成"O2 阀启用时长"更准确。

### Q5: 我设了 open_o2=1, 然后又想开雾化, 不行?
**A**: 对, 雾化和 O2 阀互斥 (氧气 + 喷雾 = 易燃). 想开雾化, 先让 O2 阀关掉:
- 关 open_o2 (open_o2=0)
- 关 enable_o2_ctrl 或调低 target_o2 (让自动闭环不触发)
- 检查没有外部 PD8/PB6 信号请求
- 然后 0x83 启动雾化

### Q6: APP 读 0x02 看到 fan_speed_actual=0, 但我之前设的是 fan_speed=3, 怎么回事?
**A**: 检查是否 OPEN_O2 mode (`open_o2=1`). OPEN_O2 时主板强制 `fan_speed_actual=0`, 但 `setpoint.fan_speed` 保留 (= 3). 退出 OPEN_O2 (open_o2=0) 后 fan_speed_actual 恢复 3。

---

## 8. 联调步骤推荐

### 第 1 天: 兼容性回归
- 跑你之前 v2.1 的全部 APP 协议测试
- 确认 0x01/0x02/0x03/0x04/0x05/0x06/0x09/0x0A/0x0B 全部命令正常
- nursing 用 1/2/3 测, 应该全部 PASS (旧值仍合法)

### 第 2 天: 新行为
- 测试用例 TC-1 ~ TC-6 (上文)
- UI 检查: 0/4 档显示, 互斥按钮组, OPEN_O2 提示文字

### 第 3 天: 边界 / 错误处理
- 雾化 + 自动 O2 同时开 → 看 UI 是否提示
- OPEN_O2 期间写温度 / 湿度 → 看 UI 是否反馈"被互锁忽略"
- 0x0B 后 UI 是否反映 nursing=0

---

## 9. 控制开关 — 关闭 / 启用 温/湿/O2 闭环

### 9.1 问题背景

iPad APP 协议**没有专门的"关闭温控/湿控/O2 控制"命令**, 写 0x03 参数 (cancel_flags 任一非 0) 会**自动开启**对应 enable_xxx_ctrl, 没有反向关闭路径 (除了 0x0B 整个出厂复位)。

### 9.2 当前可用 (v4.3, 不需要新固件): 方案 C — Pseudo-Pause

APP 想"暂停温控"时, 把 `target_temp` 写成跟当前 `sensor.temperature_avg` 相同的值:

```
APP 读 0x02 → sensor.temperature_avg = 256 (= 25.6°C)
APP 发 0x03 → cancel_flags.temp = 1, target_temp = 256
主板: temp_control 看 |25.6 - 25.6| < 滞环 → 不动加热/制冷继电器
```

**优点**: 立刻生效, 不需要新固件  
**缺点**: 室温飘移到差距 > 滞环 (约 1°C) 时温控会重新触发。短期 (几分钟) 当"暂停"用 OK, 长期不可靠

同理可对**湿控**写 `target_humidity = sensor.humidity * 10`, **O2 控制**写 `target_o2 = sensor.o2_percent * 10`。

### 9.3 v4.4 新增 (待烧): 方案 A — 0x0C IPAD_CMD_CTRL_SWITCH

#### 协议规格

**请求** (APP → 主板, 6 字节):
```
AA 0C 02 <type> <action> <CS> 55
```

| 字节 | 名称 | 值 |
|---|---|---|
| 0 | header | 0xAA |
| 1 | cmd | 0x0C (IPAD_CMD_CTRL_SWITCH) |
| 2 | data_len | 0x02 |
| 3 | type | 0x01=温控 / 0x02=湿控 / 0x03=O2控 / 0xFF=全部 |
| 4 | action | 0x00=关 / 0x01=开 |
| 5 | CS | (0xAA + 0x0C + 0x02 + type + action) & 0xFF |
| 6 | tail | 0x55 |

**成功响应** (主板 → APP, 复用 0x04 ACK 格式, 7 字节):
```
AA 04 02 00 00 B0 55
       ↑  ↑  ↑
       OK 无错误 CS
```

**错误响应**:
- **长度错** (data_len ≠ 2): `AA 04 02 02 00 B2 55` (IPAD_WRITE_CMD_ERR)
- **type/action 越界**: `AA FF 02 02 03 AF 55` (0xFF + PARAM_OOB + EPOS_LEN)

#### 行为

| 命令字节 | 主板执行 |
|---|---|
| type=0x01 action=0x00 | `setpoint.enable_temp_ctrl  = 0` |
| type=0x01 action=0x01 | `setpoint.enable_temp_ctrl  = 1` |
| type=0x02 action=0x00 | `setpoint.enable_humid_ctrl = 0` |
| type=0x02 action=0x01 | `setpoint.enable_humid_ctrl = 1` |
| type=0x03 action=0x00 | `setpoint.enable_o2_ctrl    = 0` |
| type=0x03 action=0x01 | `setpoint.enable_o2_ctrl    = 1` |
| type=0xFF action=0x00 | 三个全部 = 0 (一次关全部) |
| type=0xFF action=0x01 | 三个全部 = 1 (一次开全部) |

#### 注意事项

1. **不清 alarm latch**: 跟 0x0B 出厂复位不同, 0x0C 关闭时**不清** alarm_flags。如果你要清, 发 `0x85` ack 或让用户按 KEY9。
2. **跟 0x03 写参数交互**: 写 0x03 cancel_flags=1 会**自动重新开启** enable. 顺序很重要:
   - 想关掉温控: 先发 0x0C type=0x01 action=0x00
   - 之后不要再发 0x03 cancel_flags.temp=1 (否则又开起来)
3. **不影响 setpoint 数值**: 仅改 enable 标志, `target_temp/humidity/o2/co2` 不动
4. **互锁仍生效**: 即使 enable_o2_ctrl=1 设了, OPEN_O2 / 雾化 / UV 互锁规则下仍可能被静默清掉 (见 §2.5)

#### 测试用例

```
TC-CTRL-1: 关单个 (温控)
  发: AA 0C 02 01 00 B9 55
  收: AA 04 02 00 00 B0 55
  读 0x02 → enable_temp_ctrl = 0

TC-CTRL-2: 开单个 (O2 控制)
  发: AA 0C 02 03 01 BC 55
  收: AA 04 02 00 00 B0 55
  读 0x02 → enable_o2_ctrl = 1

TC-CTRL-3: 全部关 (一键全停)
  发: AA 0C 02 FF 00 B7 55
  收: AA 04 02 00 00 B0 55
  读 0x02 → enable_temp_ctrl = 0, enable_humid_ctrl = 0, enable_o2_ctrl = 0

TC-CTRL-4: 非法 type
  发: AA 0C 02 05 00 BD 55
  收: AA FF 02 02 03 AF 55 (PARAM_OOB + EPOS_LEN)

TC-CTRL-5: 非法 action
  发: AA 0C 02 01 02 BB 55
  收: AA FF 02 02 03 AF 55
```

### 9.4 APP 端实现建议

#### UI 设计
- 在"温度设置"/"湿度设置"/"氧浓度设置"页加 **"启用/关闭"** 开关
- 每个开关绑定一个 0x0C 调用
- 加"全部关"快捷按钮 (急停场景), 调用 type=0xFF action=0x00

#### 状态同步
- 0x0C 调用后立即读 0x02, 验证 enable_xxx_ctrl 实际值
- 注意: enable 可能被互锁规则清 (见 §2.5), APP 要能识别并提示

#### v4.3 vs v4.4 兼容
- v4.3 (当前烧的) 不支持 0x0C, 发 0x0C 会收到 `0xFF` 错误响应 (CMD_UNSUPPORTED)
- v4.4 (待烧) 支持。APP 检测固件版本: 发 0x0C 看是否拿到 0x04 ACK, 决定 UI 是否显示开关
- 临时方案: 用 §9.2 的 pseudo-pause (target = sensor) 兜底

---

## 10. 技术联系

- 固件版本: `stage8-redo-v4-3` (commit `158f2f8`)
- bin: `firmware/_post_stage8_redo_v4_3_*/main_stage8_redo_v4_3_FINAL.bin`
- 屏幕板: 沿用 v1, MD5 `112f4c0355359106c36272d5051bf9b6`
- 协议参考: `02_开发实现/SYSTEM_ARCHITECTURE.md` (帧格式 / 字段排布)
- 历史: 之前的 stage 8 redo v3 → v4 → v4.1 → v4.2 修复, 见 `02_开发实现/CODEX_REVIEW_STAGE_8_REDO_V*.md`

---

**文档结束**.
