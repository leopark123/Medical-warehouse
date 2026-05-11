# Codex Review: Stage 8 redo v4.4 — 0x0C IPAD_CMD_CTRL_SWITCH

请审查这一个**新增协议命令**的实现, 重点找:
- 边界 / 越界处理是否完整
- 跟现有 0x03 写参数 / 0x0B 出厂复位 / interlock 互锁的交互
- 医疗设备安全语义 (关掉温/湿/O2 控制后的副作用)
- C 代码层面的 lint / race / 风格问题

---

## 0. 前情提要 (你已审过的上下文)

你之前审查过 v3 → v4 → v4.1 → v4.2, 反馈见:
- `CODEX_REVIEW_STAGE_8_REDO_V4.md`
- `CODEX_REVIEW_STAGE_8_REDO_V4_1_FIX.md`

最终 v4.2 你说 "Pass, 可以烧". 用户烧了 v4.3 (在 v4.2 之上仅改 nursing 上电默认 1→0), 反馈正常。

**当前线上**: v4.3 (`stage8-redo-v4-3`, MD5 `6a5997fdf5c3d690d97d8480b602c601`).

**v4.4 这次审查的范围**: 在 v4.3 之上**只加 1 个新命令** 0x0C, 不动任何已有逻辑。

**状态**: 主板**bin 尚未烧录** (硬件不在手里), 但代码已 commit + push (`f2bd202`), tag `stage8-redo-v4-4-PENDING-FLASH`。希望你审过 OK 后, 下次硬件回来就能烧。

---

## 1. v4.4 改动背景

iPad APP 反馈: 它**没法关闭温控**。问题是协议设计语义:
- 写 0x03 `cancel_flags.temp = 1` (要更新温度) → 主板**自动开启** `enable_temp_ctrl = 1`
- 写 0x03 `cancel_flags.temp = 0` (不更新温度) → 主板**不动** enable_temp_ctrl (保留原状态)
- 没有任何一个 0x03 写法能让 enable_temp_ctrl 从 1 变 0
- 唯一关闭路径: 屏幕板编码器在 SET_TEMP 页长按 (虚拟 KeyID 0x0C) 或 iPad 0x0B 出厂复位
- APP 没法触发这两条

→ 加 `0x0C IPAD_CMD_CTRL_SWITCH` 命令, APP 直接控制 enable 标志。

---

## 2. 协议规格

### 请求帧 (APP → 主板, 7 字节含 header/CS/tail)
```
AA 0C 02 <type> <action> <CS> 55
```

| Byte | 字段 | 取值 |
|---|---|---|
| 0 | header | 0xAA |
| 1 | cmd | 0x0C (IPAD_CMD_CTRL_SWITCH) |
| 2 | data_len | 0x02 |
| 3 | type | 0x01=温控 / 0x02=湿控 / 0x03=O2控 / 0xFF=全部 |
| 4 | action | 0x00=关 / 0x01=开 |
| 5 | CS | (0xAA + 0x0C + 0x02 + type + action) & 0xFF |
| 6 | tail | 0x55 |

### 成功响应 (主板 → APP, 7 字节)
复用现有 `IPAD_RSP_WRITE_ACK` (0x04):
```
AA 04 02 <IPAD_WRITE_OK=0x00> <IPAD_ERR_NONE=0x00> <CS> 55
```

### 错误响应
- **长度错** (data_len ≠ 2): `AA 04 02 IPAD_WRITE_CMD_ERR IPAD_ERR_NONE CS 55`
- **type / action 越界**: `AA FF 02 IPAD_EXCEPT_PARAM_OOB IPAD_EPOS_LEN CS 55` (复用 0xFF 错误帧)

---

## 3. 实现 — 完整 diff

### 改动 A: `Protocol/common/protocol_defs.h` (加常量)

```c
// 在 IPAD_CMD_FACTORY_RESET 后加
#define IPAD_CMD_CTRL_SWITCH        0x0C    /* v4.4: 控制开关 (enable_xxx_ctrl). Data len=2, response=0x04 */

/* v4.4 (2026-05-07) — 0x0C 子命令: type + action */
#define IPAD_CTRL_SWITCH_DATA_LEN   2
#define IPAD_CTRL_TYPE_TEMP         0x01    /* enable_temp_ctrl */
#define IPAD_CTRL_TYPE_HUMID        0x02    /* enable_humid_ctrl */
#define IPAD_CTRL_TYPE_O2           0x03    /* enable_o2_ctrl */
#define IPAD_CTRL_TYPE_ALL          0xFF    /* 全部 3 个一起 */
#define IPAD_CTRL_ACTION_OFF        0x00
#define IPAD_CTRL_ACTION_ON         0x01
```

### 改动 B: `Protocol/ipad/ipad_protocol.c` (新增 handler + forward decl + dispatch)

**Forward declaration** (放在 handle_ctrl_switch 之前):
```c
/* Forward declaration — handle_ctrl_switch 需要 send_error_response (定义在下面) */
static void send_error_response(uint8_t error_type, uint8_t error_pos);
```

**新增函数 handle_ctrl_switch** (~60 行):
```c
/**
 * @brief Handle 0x0C IPAD_CMD_CTRL_SWITCH — enable/disable control loops
 *        v4.4 (2026-05-07): APP 可直接开/关 温/湿/O2 控制闭环, 不必走 0x0B 恢复出厂.
 *        Data: [0]=type (0x01/0x02/0x03/0xFF), [1]=action (0x00 关 / 0x01 开)
 *        Response: 0x04 IPAD_RSP_WRITE_ACK (跟 0x03 一致 ack 格式)
 */
static void handle_ctrl_switch(const uint8_t *data, uint8_t len)
{
    uint8_t ack[2];

    /* 长度检查 */
    if (len != IPAD_CTRL_SWITCH_DATA_LEN) {
        ack[0] = IPAD_WRITE_CMD_ERR;
        ack[1] = IPAD_ERR_NONE;
        uint16_t flen = frame_build_ipad(s_tx_buf, IPAD_RSP_WRITE_ACK, ack, 2);
        bsp_uart_ipad_send(s_tx_buf, flen);
        return;
    }

    uint8_t type   = data[0];
    uint8_t action = data[1];

    /* action 取值合法性 */
    if (action != IPAD_CTRL_ACTION_OFF && action != IPAD_CTRL_ACTION_ON) {
        send_error_response(IPAD_EXCEPT_PARAM_OOB, IPAD_EPOS_LEN);
        return;
    }
    uint8_t enable_val = (action == IPAD_CTRL_ACTION_ON) ? 1 : 0;

    /* type 取值合法性 */
    if (type != IPAD_CTRL_TYPE_TEMP &&
        type != IPAD_CTRL_TYPE_HUMID &&
        type != IPAD_CTRL_TYPE_O2 &&
        type != IPAD_CTRL_TYPE_ALL) {
        send_error_response(IPAD_EXCEPT_PARAM_OOB, IPAD_EPOS_LEN);
        return;
    }

    AppData_t *d = app_data_get();
    app_data_lock();
    if (type == IPAD_CTRL_TYPE_TEMP || type == IPAD_CTRL_TYPE_ALL) {
        d->setpoint.enable_temp_ctrl  = enable_val;
    }
    if (type == IPAD_CTRL_TYPE_HUMID || type == IPAD_CTRL_TYPE_ALL) {
        d->setpoint.enable_humid_ctrl = enable_val;
    }
    if (type == IPAD_CTRL_TYPE_O2 || type == IPAD_CTRL_TYPE_ALL) {
        d->setpoint.enable_o2_ctrl    = enable_val;
    }
    app_data_unlock();

    /* 注意: 不清 alarm latch (跟 0x0B 出厂复位不同). 用户想清 latch 发 0x85 ack 或按 KEY9. */

    /* ACK */
    ack[0] = IPAD_WRITE_OK;
    ack[1] = IPAD_ERR_NONE;
    uint16_t flen = frame_build_ipad(s_tx_buf, IPAD_RSP_WRITE_ACK, ack, 2);
    bsp_uart_ipad_send(s_tx_buf, flen);
}
```

**dispatch_command 加 case**:
```c
case IPAD_CMD_FACTORY_RESET:
    /* 0x0B (v2.1) → 恢复出厂默认 */
    handle_factory_reset();
    break;

case IPAD_CMD_CTRL_SWITCH:              // ← 新增
    /* 0x0C (v4.4) → 开/关 温/湿/O2 控制闭环 */
    handle_ctrl_switch(data, len);
    break;

default:
    send_error_response(...);
```

### 改动 C (文档): `02_开发实现/HDICU_iPad_APP_联调文档_v4.3.md` 加第 9 节

加了:
- 9.1 问题背景
- 9.2 方案 C — Pseudo-Pause (写 target = sensor, v4.3 兼容)
- 9.3 方案 A — 0x0C 协议规格 + 测试用例 5 条
- 9.4 APP 端 UI / 状态同步建议 + v4.3/v4.4 兼容性说明

---

## 4. 不变化的 (回归基线)

- **v2/v3/v4/v4.1/v4.2/v4.3 全部修复保留** — 没动 ntc_sensor / control_timers / interlock / tasks / screen_protocol / app_data 任何 1 行
- 屏幕板**完全未动**, MD5 不变 (`112f4c0355359106c36272d5051bf9b6`), 不需要重烧屏幕板
- g_app_data struct **未动**, 大小仍 116B
- 没引入新依赖 / 新头文件

---

## 5. 编译产物

- bin: `firmware/_post_stage8_redo_v4_4_20260511_121437/main_stage8_redo_v4_4_FINAL.bin`
- MD5: `f852ccc8d8a93fa8194cf4ddf3009d70`
- text 27864 (+96 vs v4.3 27768), data 104, bss 37880
- g_app_data 116B 不变

---

## 6. 给 Codex 的具体审查点

### Q1: 协议设计合理性
- 0x0C 命令码空闲, 跟 0x01-0x0B 不冲突 ✓
- data_len=2 跟其他 (0x03=30, 0x09=18, 0x0B=0) 区别明显, 不会误识别
- 复用 0x04 ACK / 0xFF 错误响应, 不引入新响应类型 ✓
- type=0xFF 用作"全部" 是否会跟 0xFF 错误响应混淆? (实际上 0xFF 是命令字段, 这里是 data[0], 不混淆, 但**值的选择**是否合理? 建议 0x04 或 0x07 更清晰?)

### Q2: handle_ctrl_switch 实现
- 长度错走 IPAD_WRITE_CMD_ERR (0x04 ACK 含 CMD_ERR 标志), type/action 错走 0xFF 错误 — 两条路径是否一致风格?
- forward declaration 在文件中间, C 代码风格是否可接受?
- 锁的粒度: 整个 setpoint 写入在 `app_data_lock()` 内, 跟 handle_factory_reset 一致 ✓
- 没有清 alarm latch — 是否有遗漏? (跟 v3+ stage 8 设计: enable=0 后 AlarmTask else 分支会自然清 cnt, latch 旧值保留, 用户 0x85 ack 才清)

### Q3: 跟现有写参数 (0x03) 的交互
- 顺序问题: APP 先发 0x0C type=temp action=off (关温控), 再发 0x03 cancel_flags.temp=1 (写 target_temp). 第二个会把 enable_temp_ctrl 重新设 1, **覆盖** 0x0C 的关闭。
- 这是否需要文档明确? 还是协议层面应该有保护? (我现在只在文档 9.4 提了"顺序很重要")

### Q4: 跟互锁系统的交互
- 开 enable_o2_ctrl 后, oxygen_control 进 SUPPLYING → 但如果同时雾化在跑, v4.1 Rule 5 会**静默清** enable_o2_ctrl = 0
- 这是预期 (Codex 之前批准的"互锁主动让步")
- APP 端文档已说明 (9.4 提示要读 0x02 验证)
- 0x0C **没有**额外保护或返回特殊错误码告诉 APP "你设的 enable 立刻被互锁清了". APP 只能轮询发现。**是否需要返回特殊状态?**

### Q5: 跟 0x0B 出厂复位的对比
- 0x0B 清 enable + 清 setpoint + 清 alarm latch + 清 Flash
- 0x0C 只清 enable, 其他全保留
- 用户从 APP 想"软关闭" (保留 setpoint 数值, 只是不要触发) → 0x0C 正合适
- 用户从 APP 想"全部归零" → 仍走 0x0B
- 两个命令语义清晰分离 ✓

### Q6: 医疗设备安全语义
- 关闭温控 = 失去温度环境控制, 婴儿/动物可能温度漂移. APP 端必须有 UI 警示 (我已在文档建议)
- 全部关 (type=0xFF action=off) 一次关掉温/湿/O2 三个闭环, 等于"急停所有自动控制". 是否危险? 是否需要二次确认?
- 但**手动开放供氧 (setpoint.open_o2)** 不在这命令范围, 紧急供氧路径不受 0x0C 影响 ✓

### Q7: C 代码 lint
- `uint16_t flen = frame_build_ipad(...)` 返回值用了一次, 跟 handle_factory_reset 模式一致 ✓
- 没有 buffer 越界风险 (data[0], data[1] 在 len ≥ 2 检查后访问)
- 没有 NULL 指针风险 (data 由 frame_parser 保证非 NULL)
- 编译 -Wall -Wextra 通过, 没新增 warning

---

## 7. 测试用例 (APP 端联调时跑)

| TC | 输入 | 期望响应 | 期望主板状态 |
|---|---|---|---|
| TC-1 | `AA 0C 02 01 00 B9 55` (关温控) | `AA 04 02 00 00 B0 55` (OK) | `enable_temp_ctrl=0` |
| TC-2 | `AA 0C 02 03 01 BC 55` (开 O2 控) | `AA 04 02 00 00 B0 55` | `enable_o2_ctrl=1` |
| TC-3 | `AA 0C 02 FF 00 B7 55` (全部关) | `AA 04 02 00 00 B0 55` | 三个全 0 |
| TC-4 | `AA 0C 02 FF 01 B8 55` (全部开) | `AA 04 02 00 00 B0 55` | 三个全 1 |
| TC-5 | `AA 0C 02 05 00 BD 55` (非法 type) | `AA FF 02 02 03 AF 55` (PARAM_OOB) | 不变 |
| TC-6 | `AA 0C 02 01 02 BB 55` (非法 action) | `AA FF 02 02 03 AF 55` | 不变 |
| TC-7 | `AA 0C 01 01 AC 55` (长度错=1) | `AA 04 02 02 00 B2 55` (CMD_ERR) | 不变 |
| TC-8 | 0x03 cancel_temp=1 后立即 0x0C type=temp off | 0x0C ACK | enable_temp_ctrl 最终为 0 (0x0C 在 0x03 之后, 覆盖) |
| TC-9 | 0x0C type=temp off 后立即 0x03 cancel_temp=1 | 0x03 ACK | enable_temp_ctrl 最终为 1 (0x03 在 0x0C 之后, 重新开启 — 这是协议设计的"写 setpoint 即开闭环") |

---

## 请 Codex 回答

按以下格式:

```
[整体判断]  Pass / Concern / Block

Q1 (协议设计): ...
Q2 (handle 实现): ...
Q3 (跟 0x03 交互): ...
Q4 (跟互锁交互): ...
Q5 (跟 0x0B 对比): ...
Q6 (安全语义): ...
Q7 (C lint): ...

[建议改动] (如有 Block / Concern)
- 具体修法

[是否建议下次拿到板子立即烧 v4.4]: Y / N
```

如果 Pass, 用户拿到板子后直接烧 `main_stage8_redo_v4_4_FINAL.bin`, 不需要再改。
