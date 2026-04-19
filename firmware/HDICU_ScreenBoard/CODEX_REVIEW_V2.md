# Codex 审查任务：屏幕板固件全面审查（第二轮）

## 项目背景
医疗仓（新生儿培养箱）双MCU系统：
- **主板** STM32F103VET6 (FreeRTOS, 6个任务) — 传感器采集、控制决策、报警、通信
- **屏幕板** GD32F303RCT6 (裸机main loop) — 显示 + 按键 + UART通信
- 两板通过UART 115200通信，协议帧格式：`AA 55 [CMD] [LEN] [DATA...] [CS] ED`

## 当前状态
已完成首轮调试，以下功能已实测确认工作：
- ✅ TM1640数码管显示（U9上排：温湿度/O2/CO2，U1下排：计时器）
- ✅ 主板0x01数据包接收解析和显示刷新（100ms）
- ✅ 心跳0x84发送（1s），主板不再报COMM_FAULT
- ✅ 9个按键全部工作（active-low，20ms去抖，短按/长按检测）
- ✅ 0x82按键事件帧发送
- ✅ 编码器正交解码（A=PB2, B=PA6, Push=PA7）

## 已确认的硬件映射

### 按键CN座→MCU引脚映射（实测确认）
```
CN1=KEY1(PB12) 护理等级灯    CN3=KEY2(PB13) 照明灯
CN5=KEY3(PB14) 检查灯        CN7=KEY4(PB15) 红蓝光
CN2=KEY5(PC6)  紫外灯        CN4=KEY6(PC7)  开放式供氧
CN6=KEY7(PC8)  内循环        CN8=KEY8(PC9)  新风净化
CN9=KEY9(PA8)  报警确认
```

### TM1640引脚
```
U9(上排): DIN=PB7, SCLK=PB6
U1(下排): DIN=PB4, SCLK=PB3 (PB3/PB4需AFIO释放JTAG)
```

### UART
```
UART1(PA9/PA10): 主板通信口（当前未用，CN11有电平转换器问题）
UART2(PA2/PA3):  实际主板通信口（CN12直连3.3V）
```

### 其他
```
PC13: Debug LED（500ms闪烁）
PB2:  编码器A相（原debug LED已移走）
8MHz HSE → PLL×9 → 72MHz
IWDG: ~2s超时
```

## 通信协议

### 主板→屏幕（接收）
| CMD | 名称 | 数据长度 | 周期 | 用途 |
|-----|------|---------|------|------|
| 0x01 | DISPLAY_DATA | 26B | 100ms | 传感器+控制状态 |
| 0x04 | HEARTBEAT | 8B | 1000ms | 运行时间 |

### 0x01包字段布局
```
[0-1]  temp_avg (int16, x10)     [2] humidity%        [3] o2%
[4-5]  co2_ppm (uint16)          [6] heart_rate       [7] spo2
[8]    fan_speed                 [9] nursing_level
[10-11] fog_remaining (sec)      [12-13] disinfect_remaining (sec)
[14-15] o2_accumulated (sec)     [16-17] relay_status
[18]   light_status              [19] switch_status
[20-21] alarm_flags              [22-25] reserved
```

### 屏幕→主板（发送）
| CMD | 名称 | 数据 | 用途 |
|-----|------|------|------|
| 0x82 | KEY_ACTION | {key_id, action_type} | 按键事件 |
| 0x84 | HEARTBEAT_ACK | 空 | 心跳应答 |
| 0x85 | ALARM_ACK | {0xFF} | 报警确认 |

### 按键协议
```
key_id: 0x01-0x09(KEY1-KEY9), 0x0A(编码器按下)
action_type: 0x01=单击(松开时发), 0x02=长按(>2s)
```

## 审查文件
- **唯一源文件**：`firmware/HDICU_ScreenBoard/main.c`（1090行，裸机单文件）
- 链接脚本：`firmware/HDICU_ScreenBoard/GD32F303RC_FLASH.ld`
- 启动文件：`firmware/HDICU_ScreenBoard/startup.s`

## 首轮审查已修复的问题
1. ✅ `Key_Init()` 已显式使能GPIO A/B/C时钟（不再依赖init调用顺序）
2. ✅ `SEG_FONT` 已扩展到18项（0-F + blank + dash）
3. ✅ 调试代码已全部移除
4. ✅ `TM1640_Update()` 前向声明已添加

## 请审查以下方面（按优先级）

### P0 — 功能正确性
1. **协议帧校验和计算**：
   - 发送端 `send_frame()` 的CS计算：CS = CMD + LEN + DATA_bytes（不含AA 55头和ED尾）
   - 接收端 `parse_rx_byte()` 的CS校验是否与发送端一致
   - 与主板侧 `screen_protocol.c` 的CS计算是否匹配（主板代码在 `firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c`）

2. **0x01包数据解析字节序**：
   - `temp_x10 = (d[0] << 8) | d[1]` — 假设大端
   - 主板发送时是大端还是小端？需要与主板代码 `build_display_data()` 对照
   - 如果字节序错，温度/CO2/计时器数值会全部错乱

3. **按键事件时序**：
   - 短按=松开时发送（不是按下时）——这符合主板预期吗？
   - 长按=按住2秒时发送——之后松开时不再发送click，逻辑是否正确
   - 40ms去抖（2x20ms）对外接XH座机械开关是否足够

4. **UART发送方向**：
   - `uart_primary_send()` 调用 `uart2_send()`（走CN12/UART2）
   - 按键事件和心跳都通过UART2发出——主板是否在UART2上监听？
   - UART1的RX中断在接收数据——主板是通过UART1还是UART2发送0x01包？
   - 如果主板TX→屏幕RX走的是UART1，但屏幕TX→主板RX走的是UART2，那双向通信的物理连接是怎样的？

5. **报警确认帧**：
   - 每5秒发送 `0x85 {0xFF}`，无条件发送
   - 这会不会导致主板认为所有报警一直被确认，从而抑制了报警功能？
   - 应该只在用户按KEY9时发送，还是周期性发送？

### P1 — 安全与鲁棒性
6. **看门狗喂狗覆盖**：
   - `IWDG_Feed()` 在main loop每轮调用
   - 如果 `TM1640_Update()`（bit-bang，16字节x8位x2芯片）耗时过长会不会导致看门狗超时？
   - 估算：每位2x tm_delay()约2us，16x8x2=256位x2us=512us，加上start/stop约600us——应该安全

7. **UART RX缓冲区溢出**：
   - 128字节环形缓冲区
   - 主板100ms发26字节+6字节帧开销=32字节/100ms=320字节/秒
   - main loop在每轮都drain缓冲区——只要main loop不阻塞超过400ms就不会溢出
   - 确认没有其他阻塞点

8. **SysTick `s_tick_ms` 溢出**：
   - `uint32_t` 在 49.7天后溢出
   - `tick_ms() - last_xxx >= period` 这种写法对无符号溢出是安全的——确认所有时间比较都用这种模式

9. **`send_frame()` 缓冲区大小**：
   - `uint8_t buf[64 + 6]` = 70字节栈分配
   - `FRAME_MAX_DATA = 64`，加上6字节帧头尾 = 70字节——刚好
   - 但 `len` 参数是 `uint8_t`，最大255——`if (len > FRAME_MAX_DATA) return` 保护了吗？

### P2 — 代码质量
10. **注释准确性**：
    - 文件头注释 `UART: UART1 115200 to main board via CN3` — **CN3是KEY2按键座，不是UART口！** 需要更正
    - 确认其他注释是否过时

11. **未使用代码**：
    - `uart1_send()` 是否还在使用？
    - `encoder_read_delta()` 定义了但未调用——未来HMI用，可以保留但应标记
    - SEG_FONT的hex字符(A-F)是否还有代码使用？

12. **display_digits() 边界条件**：
    - `ndig` 最大6（供氧HH:MM:SS），`digits[6]` 数组大小足够
    - 负值处理：`val < 0` 时 leftmost digit 显示dash，但 `abs_val` 也在计算——是否有 INT32_MIN 边界问题？
    - `dp_pos` 类型是 `int8_t`，与 `uint8_t i` 比较——符号比较警告？

13. **U1 GRID8/GRID9 交换**：
    - 供氧秒位 GRID8=个位 GRID9=十位（交换）
    - 这是硬件走线决定的还是软件workaround？有注释说明但需确认

### P3 — 性能与可维护性
14. **TM1640 bit-bang 时序**：
    - `tm_delay()` 用 `volatile` 循环10次约1us@72MHz
    - TM1640 datasheet最小时序要求是多少？是否有余量？
    - 如果HSE失败回退到8MHz HSI，延时会变长到约9us——仍然安全吗？

15. **Main loop 任务调度**：
    - 4个定时任务（blink/key_scan/heartbeat/display）各自用独立的 `last_xxx` 计时器
    - 如果某一轮 main loop 花了较长时间（比如TM1640更新），多个任务可能在同一轮同时触发
    - 这是否会导致看门狗超时或显示异常？

## 需要交叉审查的主板代码
以下主板文件与屏幕板的交互直接相关，如果发现屏幕板的协议实现有疑问，请参照这些文件：
- `firmware/HDICU_MainBoard/Protocol/screen/screen_protocol.c` — 主板侧帧构建/解析
- `firmware/HDICU_MainBoard/Protocol/common/protocol_defs.h` — 帧格式常量
- `firmware/HDICU_MainBoard/Protocol/common/protocol_frame.c` — 通用帧解析器
- `firmware/HDICU_MainBoard/App/tasks/tasks.c` — CommScreenTask（发送0x01/0x04，接收0x82-0x85）

## 期望输出格式
```
### [P级别] 标题
- **文件**：main.c:行号
- **问题**：描述
- **影响**：功能/安全/质量
- **修复建议**：具体代码修改
- **置信度**：xx%
```

P0 = 功能错误（必须修）
P1 = 安全隐患（应该修）
P2 = 代码质量（建议修）
P3 = 优化建议（可选）
