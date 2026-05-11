# Codex Review: v4.4 → v4.3 回滚验证

请快速确认这次回滚是否**完整**且**干净**, 没漏 / 没误伤已稳定的部分。

---

## 0. 背景

你之前审过 v4.2 → Pass, v4.3 (上电护理 0) 在线运行正常。
之后我们加了 v4.4 (新增 0x0C IPAD_CMD_CTRL_SWITCH 命令), **代码已 commit + push 但 bin 没烧** (硬件在外)。

用户决定: **"不改协议"**, 把 v4.4 的协议改动撤掉, 回到 v4.3 状态, 后续在 v4.3 上做别的改动 (用户还没说是什么)。

---

## 1. 回滚方式 (用 git revert, 不是 reset)

```
git revert --no-edit a22a3be    # 撤 v4.4 Codex review doc commit
git revert --no-edit f2bd202    # 撤 v4.4 代码 + 文档 + 归档目录 commit
```

**没用 `reset --hard`**, 因为:
- v4.4 已推到 origin (`a22a3be`), reset 后要 force push 影响 git 历史
- revert 保留完整 git 历史 (能追溯当时改了啥又为啥撤), 没 force push 风险
- 远程仓库可能有人 clone 过 v4.4, revert 不破坏他们的本地历史

---

## 2. git log 演进 (verify)

```
233cd8a  Revert "feat(v4.4): 0x0C IPAD_CMD_CTRL_SWITCH..."  ← v4.4 代码撤回
472faec  Revert "docs: v4.4 Codex 审查 prompt"               ← v4.4 docs 撤回
a22a3be  docs: v4.4 Codex 审查 prompt                         ← (原 v4.4 docs)
f2bd202  feat(v4.4): 0x0C IPAD_CMD_CTRL_SWITCH...             ← (原 v4.4 code)
87ae862  chore: Stage 8 redo v3-v4.3 推送 + 仓库清理          ← v4.3 + 清理 (保留)
158f2f8  chore(v4.3): 上电护理灯全灭                          ← v4.3 (保留)
52981d1  fix(v4.2): Codex P1 — UV+自动O2 互斥...              ← v4.2 (保留)
27e5284  fix(v4.1): Codex Block + Concern 全部修复            ← v4.1 (保留)
a4f38b2  feat: Stage 8 redo v4 — 5 issues 阶段性修复          ← v4 (保留)
```

**HEAD = 233cd8a**, 远程 `origin/main` 同步。

---

## 3. 撤回内容清单 (应该被回滚的)

| 项 | 文件 | 状态 |
|---|---|---|
| `IPAD_CMD_CTRL_SWITCH 0x0C` 常量 + 子命令 type/action 常量 | `protocol_defs.h` | ✓ 已撤 |
| `static void send_error_response(...)` forward declaration | `ipad_protocol.c` | ✓ 已撤 |
| `handle_ctrl_switch(...)` 函数 (~60 行) | `ipad_protocol.c` | ✓ 已撤 |
| `dispatch_command` 中 `case IPAD_CMD_CTRL_SWITCH` | `ipad_protocol.c` | ✓ 已撤 |
| APP 联调文档 § 9 (0x0C 协议规格 + 测试用例 5 条) | `HDICU_iPad_APP_联调文档_v4.3.md` | ✓ 已撤 |
| v4.4 归档目录 (含 `.bin` `.elf` `.map` `HANDOFF_README.md`) | `firmware/_post_stage8_redo_v4_4_*/` | ✓ 已删 |
| Codex v4.4 审查 prompt | `02_开发实现/CODEX_REVIEW_STAGE_8_REDO_V4_4.md` | ✓ 已删 |

### 验证证据 (grep 后)
```
$ grep -E "IPAD_CMD_CTRL_SWITCH|IPAD_CTRL_TYPE" protocol_defs.h
(空, 已撤)

$ grep -c "handle_ctrl_switch\|IPAD_CMD_CTRL_SWITCH" ipad_protocol.c
0  (无引用)

$ ls firmware/ | grep v4_4
(无, 已删)
```

---

## 4. **不应被回滚的内容** (确认保留)

### v4.3 改动 (commit 158f2f8)
- `app_data.c`: `setpoint.nursing_level = 0`, `control.nursing_level_actual = 0` (上电护理灯全灭)
- `ipad_protocol.c` factory_reset: `nursing_level = 0`

### 87ae862 清理 + 推送 commit
- 删 20 个旧 backup 目录 (`_pre_*`, `_post_stage[1-7]_*`)
- 加 `.gitignore` 排除 `firmware/_pre_*/` 等
- 加 5 份 Codex review 历史文档 (v3-v4.2)
- 加 APP 联调文档 v4.3
- 加 5 套诊断工具 (`tools/diag_*`)
- 强制 add v2 PASSED + v4.3 .bin/.elf

### v4 / v4.1 / v4.2 全部修复
- `interlock.c` Rule 2 (UV vs auto O2)
- `interlock.c` Rule 4 (纯氧隔离)
- `interlock.c` Rule 5 (雾化 vs 全 O2 路径)
- `interlock_can_start_fogging/uv` 基于 `relay_status & O2_IO`
- `interlock_can_start_cooling` OPEN_O2 直接 return false
- `tasks.c` safety_min 基于 `relay_status & PTC_IO`
- `tasks.c` OPEN_O2 时 `fan_speed_actual = 0`
- `tasks.c` 5 档物理灯映射
- `screen_protocol.c` case 0x01 5 档循环
- `screen_protocol.c` case 0x05 nursing OOB ≤ 4
- `screen_protocol.c` case 0x06 长按 0→1 清 `o2_accumulated`
- `screen_protocol.c` case 0x07 长按 + 互斥
- `screen_protocol.c` case 0x08 单击 + 互斥
- `ipad_protocol.c` nursing OOB ≤ 4
- `ipad_protocol.c` cycle+fresh 互斥归一化
- `control_timers.c` 累加基于 `relay_status & O2_IO`
- `app_data.h` 5 档注释

### v3 修复
- `screen_protocol.c` case 0x06 长按 0→1 清 `o2_accumulated` (跟 v4.x 一致)
- `screen_protocol.c` case 0x07 改长按

### v2 修复
- `ntc_sensor.c` NTC 物理范围检查 [-40, 60]°C
- `control_timers.c::start_fog/start_disinfect` 同步 setpoint

---

## 5. 远程一致性

```
origin/main = 233cd8a  ← 已 push, 跟本地 HEAD 一致
```

### Tag 保留 (没动)
- `stage8-redo-v4-3` → 158f2f8 (v4.3 锚点)
- `stage8-redo-v4-4-PENDING-FLASH` → f2bd202 (历史标记, 代码已撤; 保留方便追溯)
- 其他 v2/v3/v4 tag 全保留

---

## 6. 请 Codex 回答

按以下格式简短回答, 不需要长:

```
[Pass / Block / Concern]

A. 回滚完整性: v4.4 协议改动是否全部撤回, 没残留?
B. 回滚干净度: 是否误删了 v3/v4/v4.1/v4.2/v4.3 的修复?
C. git 历史可追溯: revert 而非 reset, 历史完整?
D. 远程同步: origin 与本地一致?
E. 是否可以基于当前 HEAD (233cd8a) 继续做新改动?

[备注] (如有 Concern)
```

如全 Pass, 用户立即开始描述"两个问题"做 v4.5。
