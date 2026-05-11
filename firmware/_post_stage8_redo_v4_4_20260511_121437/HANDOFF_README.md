# Stage 8 redo v4.4 — 0x0C 控制开关命令 (待烧)

## 状态
**未烧录** — 板子在外, 等下次接触板子时一并烧入。MD5 已记录。

## 烧录文件
- 主板: `main_stage8_redo_v4_4_FINAL.bin` (MD5 `f852ccc8d8a93fa8194cf4ddf3009d70`)
- 屏幕板: 沿用 v1 build, **不需重烧**

## v4.4 新增 (1 处)
**新增 0x0C 命令 IPAD_CMD_CTRL_SWITCH** — APP 直接开/关 enable_xxx_ctrl

帧格式: `AA 0C 02 <type> <action> <CS> 55`
- type: 0x01=温控 / 0x02=湿控 / 0x03=O2控 / 0xFF=全部
- action: 0x00=关 / 0x01=开
- 响应: 0x04 IPAD_RSP_WRITE_ACK (跟 0x03 一样)

详见 `02_开发实现/HDICU_iPad_APP_联调文档_v4.3.md` § 9.

## 烧录命令 (将来)
```cmd
loadbin main_stage8_redo_v4_4_FINAL.bin 0x08000000
```

## 回滚
```cmd
loadbin .../v4_3/main_stage8_redo_v4_3_FINAL.bin 0x08000000
```
