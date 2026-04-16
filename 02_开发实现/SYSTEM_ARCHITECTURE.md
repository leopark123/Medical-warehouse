# HDICU-ZKB01A 医疗仓控制系统 — 完整架构文档

> 最后更新: 2026-04-16
> 基于: 功能表.pdf + 原理图.pdf + LED屏.pdf + 主控ic.pdf + 固件代码

---

## 一、系统总览

新生儿/宠物培养箱控制系统，双MCU架构：

```
┌──────────────────────┐         UART          ┌──────────────────────┐
│      屏幕板            │◄─────────────────────►│       主板            │
│  GD32F303RCT6         │  屏幕UART2(PA2/PA3)   │  STM32F103VET6       │
│  裸机, 72MHz          │  → 主板UART1(PA9/PA10) │  FreeRTOS, 72MHz     │
│                       │  115200 8N1            │                      │
│  ■ 2×TM1640 数码管    │                        │  ■ 9路继电器          │
│  ■ 9个外接按键        │                        │  ■ 4路MOSFET风机      │
│  ■ 旋转编码器         │                        │  ■ 4路照明LED(PE10-13)│
│  ■ 按键指示LED        │                        │  ■ 3路护理等级LED     │
│  ■ CO2进度条LED       │                        │  ■ 5路UART传感器      │
└──────────────────────┘                        │  ■ 4路NTC ADC        │
                                                │  ■ 蜂鸣器             │
                                                │  ■ 电磁铁门锁         │
                                                └──────────────────────┘
```

---

## 二、主板硬件接口完整清单

### A. MCU引脚完整映射 (STM32F103VET6, LQFP100)

#### PA口
| 引脚 | 信号名 | 功能 | 方向 | 代码状态 |
|------|--------|------|------|---------|
| PA0 | NTC-ADC | NTC温度传感器1 | ADC输入 | ✅ ntc_sensor.c |
| PA1 | NTC-ADC | NTC温度传感器2 | ADC输入 | ✅ ntc_sensor.c |
| PA2 | UART2-TX | iPad通信TX | AF输出 | ✅ ipad_protocol.c |
| PA3 | UART2-RX | iPad通信RX | AF输入 | ✅ ipad_protocol.c |
| PA4 | NTC-ADC | NTC温度传感器3 | ADC输入 | ✅ ntc_sensor.c |
| PA5 | NTC-ADC | NTC温度传感器4 | ADC输入 | ✅ ntc_sensor.c |
| PA8 | — | 未使用 | — | — |
| PA9 | UART1-TX | 屏幕板通信TX | AF输出 | ✅ screen_protocol.c |
| PA10 | UART1-RX | 屏幕板通信RX | AF输入 | ✅ screen_protocol.c |
| PA11 | AD-3.3V | 模拟参考 | — | — |
| PA13 | SWD | 调试SWDIO | AF | ✅ 保留 |
| PA14 | SWC | 调试SWCLK | AF | ✅ 保留 |

#### PB口
| 引脚 | 信号名 | 功能 | 方向 | 代码状态 |
|------|--------|------|------|---------|
| PB0 | HULI02-IO | 护理等级2 LED | GPIO输出 | ✅ tasks.c ControlTask |
| PB1 | HULI01-IO | 护理等级1 LED | GPIO输出 | ✅ tasks.c ControlTask |
| PB2 | — | 未使用 | — | — |
| PB3 | BUZZER | 蜂鸣器 | GPIO输出 | ✅ tasks.c AlarmTask |
| PB4 | WH-IO | 雾化继电器 (12V) | GPIO输出 | ✅ relay_driver[8] |
| PB5 | GY-IO | **制氧机工作时输出高电平** | GPIO输出 | 🟡 待实现 |
| PB6 | ZY-IO | 制氧机信号→Q32→CN15 | GPIO输出 | ✅ 确认空着不用 |
| PB7 | O2-IO | O2电磁阀继电器 (12V) | GPIO输出 | ✅ relay_driver[4] |
| PB8 | ZIY-IO | 紫外消毒灯继电器 (220V) | GPIO输出 | ✅ relay_driver[3] |
| PB9 | RED-IO | 红外灯继电器 (220V) | GPIO输出 | ✅ 确认空着不用 |
| PB10 | UART3-TX | CO2传感器TX | AF输出 | ✅ co2_sensor.c |
| PB11 | UART3-RX | CO2传感器RX | AF输入 | ✅ co2_sensor.c |
| PB12 | LED01-IO | **压缩机指示灯（启动亮/关闭灭）** | GPIO输出 | 🟡 待实现 |
| PB13 | KEY1-IO | 检测输入（网名KEY1,实际液位/尿液） | GPIO输入 | ⚠️ 待确认 |
| PB14 | KEY3-IO | 液位检测 | GPIO输入上拉 | ✅ SensorTask |
| PB15 | KEY2-IO | 尿液检测 | GPIO输入上拉 | ✅ SensorTask |

#### PC口
| 引脚 | 信号名 | 功能 | 方向 | 代码状态 |
|------|--------|------|------|---------|
| PC5 | HULI03-IO | 护理等级3 LED | GPIO输出 | ✅ tasks.c ControlTask |
| PC8-PC9 | — | 未使用 | — | — |
| PC10 | UART4-TX | O2传感器TX | AF输出 | ✅ o2_sensor.c |
| PC11 | UART4-RX | O2传感器RX | AF输入 | ✅ o2_sensor.c |
| PC12 | UART5-TX | JFC103 TX | AF输出 | ✅ jfc103_sensor.c |
| PC13 | FENGJI-NEI2-IO | 空调内风机2 | GPIO输出 | ✅ pwm_driver.c |

#### PD口
| 引脚 | 信号名 | 功能 | 方向 | 代码状态 |
|------|--------|------|------|---------|
| PD2 | UART5-RX | JFC103 RX | AF输入 | ✅ jfc103_sensor.c |
| PD0-1, PD3-15 | — | 未使用 | — | — |

#### PE口
| 引脚 | 信号名 | 功能 | 方向 | 代码状态 |
|------|--------|------|------|---------|
| PE0 | JIARE-IO | 底部加热继电器 (220V) | GPIO输出 | ✅ relay_driver[1] |
| PE1 | PTC-IO | PTC加热继电器 (220V) | GPIO输出 | ✅ relay_driver[0] |
| PE2 | YASUO-IO | 空调压缩机继电器 (220V) | GPIO输出 | ✅ relay_driver[7] |
| PE3 | FENGJI-IO | 空调外风机继电器 (220V) | GPIO输出 | ✅ relay_driver[6] |
| PE4 | JIASHI-IO | 加湿器继电器 (220V) | GPIO输出 | ✅ relay_driver[5] |
| PE5 | FENGJI-NEI-IO | 空调内风机1 | GPIO输出 | ✅ pwm_driver.c |
| PE6 | FENGJI-PTC-IO | PTC风机使能 | GPIO输出 | ✅ pwm_driver.c |
| PE7 | MAGNET-IO | 推拉电磁铁=**内/外循环切换** | GPIO输出 | 🟡 待实现 |
| PE9 | PWM1 | PTC风机PWM速度 | TIM PWM | ✅ pwm_driver.c |
| **PE10** | **LED1** | **照明灯颜色1 → U32 高电平亮** | GPIO输出 | 🟡 待实现 |
| **PE11** | **LED2** | **照明灯颜色2 → U32 高电平亮** | GPIO输出 | 🟡 待实现 |
| **PE12** | **LED3** | **照明灯颜色3 → U32 高电平亮** | GPIO输出 | 🟡 待实现 |
| **PE13** | **LED4** | **照明灯颜色4 → U32 高电平亮** | GPIO输出 | 🟡 待实现 |
| PE14-15 | — | 未使用 | — | — |

### B. 继电器输出 (9路, relay_driver.c)

| # | GPIO | 信号名 | PCB丝印/器件 | 负载 | 电压 | 驱动IC | 触发来源 | 状态 |
|---|------|--------|-------------|------|------|--------|---------|------|
| 0 | PE1 | PTC-IO | PTC (U8) | PTC陶瓷加热片 | 220V | ULN2003A(U5) | temp_control HEATING | ✅ |
| 1 | PE0 | JIARE-IO | 底部加热 (U12) | 底部加热 | 220V | ULN2003A(U5) | temp_control HEATING | ✅ |
| 2 | PB9 | RED-IO | 红外灯 (U10) | 红外灯 | 220V | ULN2003A(U5) | **无业务逻辑** | ❌ |
| 3 | PB8 | ZIY-IO | 紫外灯 (U14) | UV消毒灯 | 220V | ULN2003A(U5) | 消毒定时器 | ✅ |
| 4 | PB7 | O2-IO | O2阀门 (U16) | O2电磁阀 | 12V | ULN2003A(U5) | oxygen_control | ✅ |
| 5 | PE4 | JIASHI-IO | 加湿 (U18) | 加湿器 | 220V | ULN2001(U34) | humidity_control | ✅ |
| 6 | PE3 | FENGJI-IO | 空调外风机 (U20) | 外风机 | 220V | ULN2001(U34) | 制冷/除湿 | ✅ |
| 7 | PE2 | YASUO-IO | 空调压缩机 (U23) | 压缩机 | 220V | ULN2001(U34) | 制冷/除湿 | ✅ |
| 8 | PB4 | WH-IO | 雾化 (U30) | 雾化气泵 | 12V | ULN2001(U33) | 雾化定时器 | ✅ |

### C. MOSFET直驱输出

| PCB丝印 | 信号名 | GPIO | 功能 | 状态 |
|---------|--------|------|------|------|
| 空调风机1 | FENGJI-NEI-IO | PE5 | 空调内风机1 ON/OFF | ✅ |
| PTC风机 | FENGJI-PTC-IO + PWM1 | PE6(使能) + PE9(PWM) | PTC散热风机 | ✅ |
| 空调风机2 | FENGJI-NEI2-IO | PC13 | 空调内风机2 ON/OFF | ✅ |
| 推拉电磁铁 | MAGNET-IO | PE7 | 门锁电磁铁 | ❌ 未实现 |

### D. 照明输出 (U32连接器, 6针)

| U32引脚 | 信号名 | GPIO | light_ctrl位 | 功能 | 状态 |
|---------|--------|------|-------------|------|------|
| 高低电平1 | LED1 | PE10 | bit0 | 检查灯 | ❌ 未实现 |
| 高低电平2 | LED2 | PE11 | bit1 | 照明灯 | ❌ 未实现 |
| 高低电平3 | LED3 | PE12 | bit2 | 蓝光 | ❌ 未实现 |
| 高低电平4 | LED4 | PE13 | bit3 | 红光 | ❌ 未实现 |
| GND | — | — | — | 地 | — |
| 5V | — | — | — | 电源 | — |

### E. 护理等级LED (U29连接器, 4针)

| U29引脚 | 信号名 | GPIO | 功能 | 状态 |
|---------|--------|------|------|------|
| 颜色1 | HULI01-IO | PB1 | 护理等级1 | ✅ |
| 颜色2 | HULI02-IO | PB0 | 护理等级2 | ✅ |
| 颜色3 | HULI03-IO | PC5 | 护理等级3 | ✅ |
| 12V | — | — | 电源 | — |

### F. UART/传感器接口

| CN号 | PCB丝印 | 引脚定义 | UART | MCU引脚 | 波特率 | 对接设备 | 状态 |
|------|---------|---------|------|---------|--------|---------|------|
| CN3 | 串口1 LED屏幕 | 5V TX RX G | UART1 | PA9/PA10 | 115200 | 屏幕板 | ✅ |
| CN4 | 串口2 iPad | 5V RX TX G | UART2 | PA2/PA3 | 115200 | iPad APP | ✅ |
| CN5 | 串口3 CO2 | 5V TX RX G | UART3 | PB10/PB11 | 9600 | MWD1006E | ✅ |
| CN16 | 串口4 O2 | 5V TX RX G | UART4 | PC10/PC11 | 9600 | OCS-3RL | ✅ |
| CN1 | 串口5 指尖体征检测 | 5V TX RX G | UART5 | PC12/PD2 | 38400 | JFC103 | ✅ |

### G. 检测输入

| CN号 | PCB丝印 | GPIO | 功能 | 电平 | 状态 |
|------|---------|------|------|------|------|
| CN12 | 尿液检测 高低电平 | PB15 | 尿液/漏液检测 | 输入上拉,低=检测到 | ✅ |
| CN2 | 液位检测 高低电平 | PB14 | 水箱液位检测 | 输入上拉,低=液位低 | ✅ |
| NTC01-04 | NTC温度传感器 | PA0/PA1/PA4/PA5 | 4路温度ADC | ADC | ✅ |

### H. 供氧/制氧信号

| CN号 | PCB丝印 | 引脚 | GPIO | 功能 | 状态 |
|------|---------|------|------|------|------|
| CN6 | 总供氧机信号 | 12V NC NC GND | — | 总供氧机电源/控制 | ❓ 待确认 |
| CN8 | 供氧机信号 | 3.3V GND | — | 供氧机信号 | ❓ 待确认 |
| CN15 | 制氧机信号 | 2pin XH | PB6(ZY-IO)→Q32 | 制氧机通断 | ❌ 未实现 |

### I. 其他

| 器件/CN | 功能 | GPIO | 状态 |
|---------|------|------|------|
| CN14 | 空调指示灯 | PB12(LED01-IO)? | ❓ 待确认 |
| CN11 | 备用2pin | — | ❓ 待确认 |
| CN17 | 旋转编码器(主板侧) | — | 屏幕板侧使用 |

---

## 三、屏幕板硬件接口完整清单

### A. MCU引脚映射 (GD32F303RCT6, LQFP64)

#### 按键输入
| GPIO | 信号名 | CN座 | 按键功能 | key_id | 状态 |
|------|--------|------|---------|--------|------|
| PB12 | KEY1 | CN1 | 护理等级 | 0x01 | ✅ |
| PB13 | KEY2 | CN3 | 照明灯 | 0x02 | ✅ |
| PB14 | KEY3 | CN5 | 检查灯 | 0x03 | ✅ |
| PB15 | KEY4 | CN7 | 红蓝光 | 0x04 | ✅ |
| PC6 | KEY5 | CN2 | 紫外灯 | 0x05 | ✅ |
| PC7 | KEY6 | CN4 | 开放式供氧 | 0x06 | ✅ |
| PC8 | KEY7 | CN6 | 内/外循环 | 0x07 | ✅ |
| PC9 | KEY8 | CN8 | 新风净化 | 0x08 | ✅ |
| PA8 | KEY9 | CN9 | 报警确认 | 0x09 | ✅ |

#### 编码器
| GPIO | 功能 | 状态 |
|------|------|------|
| PB2 | 编码器A相 | ✅ |
| PA6 | 编码器B相 | ✅ |
| PA7 | 编码器按下 (key_id=0x0A) | ✅ |

#### TM1640显示驱动
| GPIO | 功能 | TM1640芯片 |
|------|------|-----------|
| PB7 | DIN | U9 (上排:温度/湿度/O2/CO2) |
| PB6 | SCLK | U9 |
| PB4 | DIN | U1 (下排:雾化/消毒/供氧计时) |
| PB3 | SCLK | U1 |

#### UART通信
| GPIO | 功能 | 状态 |
|------|------|------|
| PA9/PA10 | UART1 TX/RX (CN11, 5V电平转换) | ✅ 备用(转换器故障) |
| PA2/PA3 | UART2 TX/RX (CN12, 3.3V直连) | ✅ **当前主通信口** |

#### 按键指示LED (面板按钮旁绿色LED)
| 信号名 | LED编号 | 功能 | GPIO | 状态 |
|--------|---------|------|------|------|
| LEDA5 | LED305 | 按键指示灯(绿色) | 待确认 | ❌ 未实现 |
| LEDA6 | LED329 | 按键指示灯(绿色) | 待确认 | ❌ 未实现 |
| LEDA7 | LED338 | 按键指示灯(绿色) | 待确认 | ❌ 未实现 |
| LEDA8 | LED368 | 按键指示灯(绿色) | 待确认 | ❌ 未实现 |

#### 其他
| GPIO | 功能 | 状态 |
|------|------|------|
| PC13 | Debug LED (500ms闪烁) | ✅ |

### B. TM1640显示映射

#### U9 上排 (DIN=PB7, SCLK=PB6)
| GRID | 显示内容 | 数据来源(0x01包) |
|------|---------|----------------|
| 0-2 | 舱内温度 (如 25.6) | d[0-1] temp_avg x10, 带小数点 |
| 3-4 | 舱内湿度 (如 50) | d[2] humidity % |
| 5-6 | 氧浓度 (如 21) | d[3] o2_percent % |
| 7-10 | CO2浓度 (如 3721) | d[4-5] co2_ppm |

#### U1 下排 (DIN=PB4, SCLK=PB3)
| GRID | 显示内容 | 数据来源(0x01包) |
|------|---------|----------------|
| 0-1 | 雾化剩余(分钟) | d[10-11] fog_sec÷60 |
| 2-3 | 消毒剩余(分钟) | d[12-13] dis_sec÷60 |
| 4-9 | 供氧计时(时:分:秒) | d[14-15] o2_sec, GRID8↔9交换 |
| 10 | 报警指示 | alarm_flags≠0则全亮 |
| 11-12 | 预留 | 0x00 |

---

## 四、按键 → 主板 → 物理输出 完整链路

### 信号流

```
用户短接CN座(屏幕板)
  → Key_Scan() 20ms去抖
  → send_frame(0x82, {key_id, action_type})
  → UART2(PA2) TX 发出
  → 主板 UART1(PA10) RX 接收
  → ISR → xQueueSend → CommScreenTask
  → screen_protocol_rx_byte() → 帧解析
  → dispatch_screen_command()
  → 修改 setpoint / relay_status / timer
  → ControlTask(200ms)
    → temp_control / humidity_control / oxygen_control
    → interlock_apply() (6条安全规则)
    → relay_driver_apply(bitmap) → HAL_GPIO_WritePin
    → ULN2003A/ULN2001 → 继电器动作
```

### 每个按键的完整链路

| 按键 | CN座 | key_id | 主板修改 | 物理输出GPIO | 输出连接器 | 状态 |
|------|------|--------|---------|-----------|----------|------|
| KEY1 护理等级 | CN1 | 0x01 | nursing_level循环1→2→3 | PB1/PB0/PC5 | U29(颜色1/2/3) | ✅ |
| KEY2 照明灯 | CN3 | 0x02 | light_ctrl bit1翻转 | PE11(LED2) → U32 | U32(高低电平2) | 🟡 待实现GPIO驱动 |
| KEY3 检查灯 | CN5 | 0x03 | light_ctrl bit0翻转 | PE10(LED1) → U32 | U32(高低电平1) | 🟡 待实现GPIO驱动 |
| KEY4 红蓝光 | CN7 | 0x04 | light_ctrl bit2+3翻转 | PE12+PE13(LED3+4) → U32 | U32(高低电平3+4) | 🟡 待实现GPIO驱动 |
| KEY5 紫外灯 | CN2 | 0x05 | 启停消毒定时器 | PB8(ZIY-IO) | 紫外灯(U14) | ✅ |
| KEY6 开放式供氧 | CN4 | 0x06 | open_o2翻转+互锁 | PB7(O2-IO) | O2阀门(U16) | ✅ |
| KEY7 内/外循环 | CN6 | 0x07 | inner_cycle翻转 | PE7(MAGNET-IO) 推拉电磁铁 | 电磁铁连接器 | 🟡 待实现GPIO驱动 |
| KEY8 新风净化 | CN8 | 0x08 | fresh_air翻转 | PE9(PWM)+PE6 PTC风机调速 | PTC风机 | 🟡 待实现联动逻辑 |
| KEY9 报警确认 | CN9 | 0x09 | acknowledged=true + 0x85帧 | PB3(蜂鸣器停) | 蜂鸣器 | ✅ |
| 编码器单击 | — | 0x0A | HMI通知 | 无 | — | ✅ |
| 编码器长按 | — | 0x0A+long | o2_accumulated=0 | 无 | — | ✅ |

---

## 五、通信协议

### 帧格式
```
AA 55 [CMD] [LEN] [DATA...] [CS] ED
CS = (CMD + LEN + DATA_bytes) & 0xFF  (不含AA 55头)
```

### 主板→屏幕 (100ms周期)
| CMD | 名称 | 数据长度 | 内容 |
|-----|------|---------|------|
| 0x01 | 显示数据 | 26B | 温度/湿度/O2/CO2/心率/血氧/风速/护理等级/计时器/继电器/灯状态/报警 |
| 0x04 | 心跳 | 8B | 总运行时间+本次运行时间 |

### 屏幕→主板
| CMD | 名称 | 数据长度 | 内容 |
|-----|------|---------|------|
| 0x82 | 按键事件 | 2B | KeyID(1B) + ActionType(1B): 0x01=单击 0x02=长按 |
| 0x83 | 计时器控制 | 4B | TimerType + Cmd + Duration |
| 0x84 | 心跳应答 | 0B | 每1秒发送 |
| 0x85 | 报警确认 | 1B | 0xFF=确认所有报警 (仅KEY9触发) |

### 0x01包字段详细 (26字节, 大端)
```
[0-1]  temp_avg (int16, x10)     [2]    humidity (%)
[3]    o2_percent (%)            [4-5]  co2_ppm (uint16)
[6]    heart_rate (bpm)          [7]    spo2 (%)
[8]    fan_speed                 [9]    nursing_level
[10-11] fog_remaining (sec)     [12-13] disinfect_remaining (sec)
[14-15] o2_accumulated (sec)    [16-17] relay_status (bitmap)
[18]   light_status             [19]   switch_status
[20-21] alarm_flags             [22-25] reserved
```

---

## 六、控制逻辑

### 温度控制 (temp_control.c)
```
if (实际 < 设定 - 1°C) → HEATING: PE1(PTC) + PE0(底热) + PE6(PTC风机) + PE9(PWM)
if (实际 > 设定 + 1°C) → COOLING: PE2(压缩机) + PE3(外风机) + PC13(内风机2)
else                   → IDLE: 全关
```

### 湿度控制 (humidity_control.c)
```
if (实际 < 设定 - 3%) → HUMIDIFY: PE4(加湿器)
if (实际 > 设定 + 3%) → DEHUMIDIFY: PE2(压缩机) + PE3(外风机)
else                  → IDLE
```

### 供氧控制 (oxygen_control.c)
```
if (实际 < 设定 - 2%) → SUPPLYING: PB7(O2阀)
if (实际 >= 设定)     → IDLE: PB7关
开放供氧模式          → OPEN_MODE: PB7常开 + 互锁强制
```

### 安全互锁 (interlock.c, 最后执行)
```
规则1: 加热 ↔ 制冷 互斥
规则2: 紫外灯 ↔ 开放供氧 互斥
规则3: 雾化 ↔ 紫外灯 互斥
规则4: 开放供氧模式 → O2阀强开, 内循环强关, 新风强开, 加热/雾化/紫外禁止
规则5: 雾化中 → 开放供氧禁止
规则6: 开放供氧中 → 加热禁止 (与规则4冗余)
```

### 报警系统 (AlarmTask, 100ms周期)
| 报警位 | 条件 | 延迟 |
|--------|------|------|
| ALARM_TEMP_HIGH | 温度 > 设定+5°C | 10秒 |
| ALARM_TEMP_LOW | 温度 < 设定-5°C | 10秒 |
| ALARM_HUMID | 湿度偏差>5% | 10秒 |
| ALARM_O2_CO2 | O2偏差>5% 或 CO2>5000ppm | 10秒 |
| ALARM_HEART_RATE | HR<50 或 HR>140 bpm | 3秒 |
| ALARM_SPO2_LOW | SpO2<85% | 3秒 |
| ALARM_COMM_FAULT | 屏幕板离线>30秒 | — |

蜂鸣器: 500ms开/500ms关间歇。清除条件: 参数恢复正常 **且** 用户按KEY9确认。

---

## 七、物理通信链路说明

### 设计方案 (原理图)
```
屏幕板 UART1(PA9/PA10) ── CN11 ── 5V电平转换器 ── 主板 UART1(PA9/PA10)
```

### 当前实际方案 (CN11转换器故障)
```
屏幕板 UART2(PA2/PA3) ── CN12 ── 3.3V杜邦线直连 ── 主板 UART1(PA10 RX)
```

屏幕板代码中 `uart_primary_send()` 走 UART2, 同时两个UART的RX都监听:
```c
while (uart2_rx_available()) parse_rx_byte(uart2_rx_read());  // 主路径
while (uart1_rx_available()) parse_rx_byte(uart1_rx_read());  // 备用
```

---

## 八、代码文件索引

### 主板 `firmware/HDICU_MainBoard/`
| 文件 | 功能 |
|------|------|
| main.c | 入口, HAL初始化, IWDG看门狗 |
| App/main_app.c | FreeRTOS初始化, 6任务创建, UART ISR回调 |
| App/data/app_data.h | 核心数据结构 (Setpoints/Sensor/Control/Alarm) |
| App/data/app_data.c | 数据初始化, 锁 |
| App/tasks/tasks.c | SensorTask, ControlTask, AlarmTask, CommScreenTask, CommIPadTask, SystemTask |
| Protocol/screen/screen_protocol.c | 屏幕板通信 (0x01发送, 0x82按键处理) |
| Protocol/ipad/ipad_protocol.c | iPad通信 (0x01-0x06) |
| Protocol/common/protocol_frame.c | 帧解析/构建 |
| Control/temp/temp_control.c | 温度状态机 |
| Control/humidity/humidity_control.c | 湿度状态机 |
| Control/oxygen/oxygen_control.c | 供氧状态机 |
| Control/timers/control_timers.c | 雾化/消毒/O2计时器 |
| Control/interlocks/interlock.c | 6条安全互锁 |
| Sensors/co2/co2_sensor.c | CO2 MWD1006E解析 |
| Sensors/o2/o2_sensor.c | O2 OCS-3RL解析 |
| Sensors/ntc/ntc_sensor.c | NTC ADC温度计算 |
| Sensors/jfc103/jfc103_sensor.c | JFC103心率血氧 |
| Drivers/uart/uart_driver.c | 5路UART初始化+ISR |
| Drivers/adc/adc_driver.c | ADC轮询 |
| Drivers/pwm/pwm_driver.c | 风机PWM |
| Drivers/gpio/relay_driver.c | 9路继电器GPIO映射 |
| Drivers/flash/flash_storage.c | Flash持久化运行时间 |
| BSP/bsp_config.h | 引脚定义 |

### 屏幕板 `firmware/HDICU_ScreenBoard/`
| 文件 | 功能 |
|------|------|
| main.c | 全部代码 (~1090行裸机): 时钟/UART/TM1640/按键扫描/协议/主循环 |
| startup.s | 启动文件+中断向量 |
| GD32F303RC_FLASH.ld | 链接脚本 |

---

## 九、功能实现总览 (对照功能表.pdf)

| # | 功能 | 传感器/输入 | 控制逻辑 | 物理输出 | 完成度 |
|---|------|-----------|---------|---------|--------|
| 1 | 舱内温度 | NTC ADC ×4 | temp_control.c ±1°C | PE0/PE1/PE2/PE3 继电器 | ✅ 100% |
| 2 | 湿度 | O2传感器湿度通道 | humidity_control.c ±3% | PE4 继电器 + 共用制冷 | ✅ 100% |
| 3 | 氧浓度 | O2传感器 UART4 | oxygen_control.c ±2% | PB7 O2阀 | ✅ 100% |
| 4 | CO2浓度 | MWD1006E UART3 | 仅监测+报警>5000ppm | 无执行器 | ✅ 100% |
| 5 | 雾化治疗 | — | control_timers 倒计时 | PB4 雾化继电器 | ✅ 100% |
| 6 | 消毒杀菌 | — | control_timers + KEY5启停 | PB8 UV继电器 | ✅ 100% |
| 7 | 供氧计时 | — | control_timers 累计 | 纯软件计数 | ✅ 100% |
| **8** | **照明4灯** | — | light_ctrl bit0-3 有状态 | **PE10-PE13 → U32 (已确认)** | **🟡 待实现GPIO驱动** |
| **9** | **内/外循环** | — | inner_cycle + 互锁逻辑 | **PE7 MAGNET-IO (已确认=推拉电磁铁)** | **🟡 待实现GPIO驱动** |
| **10** | **新风净化** | — | fresh_air + 互锁逻辑 | **PE9 PWM + PE6 (已确认=PTC风机调速)** | **🟡 待实现联动逻辑** |
| 11 | 护理等级 | — | nursing_level 1→2→3 | PB0/PB1/PC5 LED | ✅ 100% |
| 12 | 开放式供氧 | — | open_o2 + 6条互锁 | PB7 + 联动制冷/风机 | ✅ 100% |
| 13 | 风速 | — | fan_speed → PWM | PE9(PWM) + PE5/PC13 | ✅ 100% |
| 14 | 运行时间 | — | Flash持久化 | 纯软件 | ✅ 100% |
| 15 | 生命体征 | JFC103 UART5 | HR/SpO2解析 + 报警 | 无执行器 | ✅ 100% |
| 16 | 报警系统 | 所有传感器 | AlarmTask 7种报警 | PB3 蜂鸣器 | ✅ 100% |
| 17 | 屏幕通信 | — | CommScreenTask | UART1/UART2 | ✅ (物理链路临时绕行) |

---

## 十、硬件工程师确认结果 (2026-04-16)

### ✅ 已确认并待实现

| # | 项目 | 硬件工程师回复 | GPIO | 实现方案 | 工作量 |
|---|------|-------------|------|---------|--------|
| 1 | **U32照明灯4灯驱动** | 4个颜色灯，高电平亮低电平灭 | PE10=LED1, PE11=LED2, PE12=LED3, PE13=LED4 | ControlTask根据light_ctrl bit0-3驱动WritePin | BSP定义4行+ControlTask 4行 |
| 2 | **内/外循环=推拉电磁铁** | 内外循环切换控制推拉电磁铁 | **PE7 = MAGNET-IO** | ControlTask根据inner_cycle驱动PE7 | BSP定义+1行WritePin |
| 3 | **新风净化=PTC风机调速** | 新风系统可调速，用PTC风机 | PE9(PWM)+PE6(使能) 已有 | fresh_air=1时提高风机转速 | 修改pwm逻辑 |
| 4 | **PB12压缩机指示灯** | 压缩机启动亮，关闭灭 | **PB12 = LED01-IO** | ControlTask根据YASUO-IO状态驱动PB12 | BSP定义+1行WritePin |
| 5 | **PB5制氧机输出信号** | 制氧机工作时GY-IO输出高电平 | **PB5 = GY-IO** | 供氧状态SUPPLYING/OPEN时PB5高 | BSP定义+1行WritePin |
| 6 | **屏幕板设备运行指示LED** | 设备运行时绿灯亮（压缩机/雾化等工作中亮） | 屏幕板LEDA信号 | 屏幕板根据0x01包relay_status驱动 | 屏幕板代码修改 |

### ✅ 已确认不需实现

| # | 项目 | 硬件工程师回复 |
|---|------|-------------|
| 7 | PB9 RED-IO红外灯 | **红外灯没用着，空着就行** |
| 8 | PB6 ZY-IO制氧机信号 | **先空着** |

### 📋 后续迭代

| # | 项目 | 说明 |
|---|------|------|
| 9 | CN11 UART1电平转换修复 | 修复后可回退到设计方案通信链路 |
| 10 | 屏幕板HMI状态机 | 编码器参数编辑界面(旋转调值+按下确认) |
| 11 | 屏幕板指示LED GPIO确认 | LEDA5-8对应屏幕板MCU哪些引脚，需查屏幕板原理图 |
