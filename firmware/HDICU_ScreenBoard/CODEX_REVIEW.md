# Codex 审查任务：屏幕板固件问题排查

## 项目背景
医疗仓（新生儿培养箱）双MCU系统，屏幕板 GD32F303RCT6（裸机），主板 STM32F103VET6（FreeRTOS）。
屏幕板负责：2×TM1640数码管显示 + 9个外接按键扫描 + 旋转编码器 + UART通信。

## 当前问题
屏幕板固件 `main.c` 存在以下已确认的问题，需要你**全面审查并指出所有bug**：

### 问题1：按键检测完全失效
- **现象**：原理图上KEY2（照明灯）通过CN3（XH2.54-2针座）连接MCU
- **电路**：3V3 → R2(10kΩ上拉) → CN3一脚 → R1(1kΩ) → MCU GPIO；CN3另一脚 → GND
- **操作**：短接CN3两针（模拟按键按下），应拉低GPIO到GND
- **结果**：GPIO无变化，按键事件未触发
- **用户提供的引脚分配**：KEY2=PB13（但可能不准确！）
- **已尝试**：GPIO pin change detector扫描GPIOA/B/C全部引脚，短接CN3后**没有任何引脚变化**

### 问题2：调试显示覆盖问题
- `update_display_from_data()` 在末尾调用 `TM1640_Update()`
- 调试代码在之后写入 `s_u9_buf`/`s_u1_buf` 并再次调用 `TM1640_Update()`
- 但100ms后下一轮 `update_display_from_data()` 会先用 `memset(s_u9_buf, 0, 16)` 清空buffer再写入正常数据
- 所以调试信息只显示约100ms就被覆盖，闪烁难以辨认
- 温度位置本应显示 `---`（无变化时），但实际不是

### 问题3：GPIO初始化顺序冲突
- `TM1640_Init()` 配置 PB3/PB4/PB6/PB7 为推挽输出
- `Key_Init()` 之后配置 PB2 为输入（编码器A相）
- 但 `Key_Init()` 的 `GPIOB_CRL` 修改**只改了PB2的位**，没有保护PB3/PB4/PB6/PB7的配置
- 需要确认 `Key_Init()` 是否意外破坏了TM1640的GPIO配置

### 问题4：RCC时钟使能可能遗漏
- GPIOA 在 `UART1_Init()` 中通过 `RCC_APB2ENR |= (1<<2)` 使能
- GPIOB 在 `Debug_LED_Init()` 中通过 `RCC_APB2ENR |= (1<<3)` 使能
- GPIOC 在 `Debug_LED_Init()` 中通过 `RCC_APB2ENR |= (1<<4)` 使能
- **问题**：这些RCC使能在init函数间分散，如果调用顺序变化可能丢失
- 需要确认所有GPIO端口的时钟都在使用前已使能

## 请审查以下方面

1. **GPIO CRL/CRH 寄存器操作**：
   - 每个init函数是否正确使用了 read-modify-write 模式
   - 是否有函数意外覆盖了其他函数设置的引脚配置
   - 特别注意：`GPIOB_CRL` 被 `TM1640_Init()` 和 `Key_Init()` 两个函数修改

2. **GPIO IDR 读取**：
   - `key_read_raw()` 的引脚映射是否与 `Key_Init()` 的配置一致
   - 是否所有引脚都正确配置为输入上拉模式

3. **显示缓冲区竞争**：
   - `update_display_from_data()` 和调试代码都写 `s_u9_buf`
   - `memset(s_u9_buf, 0, 16)` 会清掉之前的调试值
   - 调试代码的显示时机是否正确

4. **初始化顺序依赖**：
   - `main()` 调用顺序：`Debug_LED_Init` → `UART1_Init` → `UART2_Init` → `TM1640_Init` → `Key_Init`
   - 后面的init是否破坏前面的配置
   - 特别是 `Key_Init()` 对 `GPIOB_CRL` 的修改是否影响 TM1640 的 PB3/PB4

5. **按键完全无响应的根因分析**：
   - 短接CN3后GPIOA/B/C全部IDR无变化 → 说明什么？
   - 可能的原因：引脚被配置为输出？引脚被其他外设复用？RCC未使能？硬件问题？
   - 如果引脚分配本身就是错的（PB13不是KEY2），代码层面能做什么？

6. **GD32F303 vs STM32 兼容性**：
   - GD32F303RCT6 的寄存器地址和STM32F10x兼容，但有些细微差异
   - AFIO_MAPR 的 SWJ_CFG 位配置是否正确释放了需要的引脚
   - 输入上拉的ODR设置是否在GD32上正确工作

7. **AFIO重映射**：
   - `AFIO_MAPR` 设置 SWJ_CFG=010（只保留SWD，释放JTAG）
   - 这释放了 PB3(JTDO) 和 PB4(JNTRST)
   - 但是否还有其他引脚被JTAG/SWD占用需要释放？
   - PB2 在某些GD32芯片上可能是BOOT1引脚，是否影响输入读取？

## 文件位置
- 屏幕板固件：`firmware/HDICU_ScreenBoard/main.c`（1173行）
- 链接脚本：`firmware/HDICU_ScreenBoard/GD32F303RC_FLASH.ld`
- 启动文件：`firmware/HDICU_ScreenBoard/startup.s`

## 期望输出
1. 列出所有发现的bug，按严重程度排序
2. 对每个bug给出具体的修复方案（行号+代码）
3. 特别关注"短接CN3后没有任何GPIO变化"这个现象的根因
4. 如果是引脚分配错误，建议如何通过代码手段确认正确的引脚
