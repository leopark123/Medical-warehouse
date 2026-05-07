/**
 * @file    pwm_driver.c
 * @brief   Fan control driver
 * @note    Hardware engineer confirmed (2026-04-09):
 *          - Only PTC fan needs speed control, via PE9 (PWM1)
 *          - PE5 (内循环风机), PE6 (PTC风机使能), PC13 (空调内风机): all ON/OFF
 *
 *          Stage 6 (2026-05-05) 重构: 软件 PWM → 硬件 PWM @ 15 kHz.
 *            旧实现: TIM6 ISR @ 10 kHz, 软件计数 100 步 → PWM 100 Hz.
 *                    100 Hz 在人耳可闻范围, 婴儿患者可听见 buzz 声.
 *            新实现: TIM1_CH1 (PE9 with full remap) 硬件 PWM @ 15 kHz.
 *                    72 MHz / 48 / 100 = 15000 Hz 整数除尽,
 *                    100 步分辨率 (0~100% with 1% 颗粒度).
 *                    CPU 0% ISR 开销, MOSFET 开关损耗 < 5 mW.
 *
 *          AFIO TIM1 全重映射: TIM1_CH1=PE9.
 *            副作用: TIM1_CH2=PE11, CH3=PE13 等也被映射, 但 PE10-PE13 GPIO mode
 *            仍为 Output (照明灯), AFIO 只决定 AF 复用映射, 不强制激活.
 *
 *          PTC fan control: PE6=enable(ON/OFF) + PE9=speed(hardware PWM).
 *          Both must be HIGH for fan to run. pwm_set_fan2_duty() handles both.
 */

#include "pwm_driver.h"
#include "bsp_config.h"
#include "stm32f1xx_hal.h"

/* Duty cycle mapping for fan speed levels (Stage 3: 公开供 ControlTask 用) */
const uint8_t pwm_fan_level_duty[4] = {
    0,      /* OFF */
    30,     /* LOW: 30% duty */
    60,     /* MID: 60% duty */
    100,    /* HIGH: 100% duty */
};

/* Stage 6: TIM1 hardware PWM handle */
static TIM_HandleTypeDef s_htim1;

void pwm_driver_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* === Stage 6: 启用必需时钟 === */
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();

    /* === PE5 + PE6 GPIO Output (内循环风机 + PTC 风机使能) === */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = BSP_FAN_NEI_PIN | BSP_FAN_PTC_EN_PIN;
    HAL_GPIO_Init(GPIOE, &gpio);

    /* === PC13 GPIO Output (空调内风机) === */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;       /* PC13 limitation */
    gpio.Pin = BSP_FAN_NEI2_PIN;
    HAL_GPIO_Init(BSP_FAN_NEI2_PORT, &gpio);

    /* All non-PWM fans off */
    HAL_GPIO_WritePin(BSP_FAN_NEI_PORT,    BSP_FAN_NEI_PIN,    GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BSP_FAN_PTC_EN_PORT, BSP_FAN_PTC_EN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BSP_FAN_NEI2_PORT,   BSP_FAN_NEI2_PIN,   GPIO_PIN_RESET);

    /* === Stage 6 / Stage 7 修复: TIM1 全重映射 → TIM1_CH1 = PE9 ===
     * AFIO_MAPR bits[7:6] = TIM1_REMAP[1:0]
     *   00 = no remap (PA8/9/10/11)
     *   01 = partial
     *   11 = full remap (PE7-15: CH1=PE9, CH2=PE11, CH3=PE13, CH4=PE14)
     *
     * 重要: 仅 AF 映射切换. PE10-PE13 (照明灯) 保持 GPIO Output 模式不受影响,
     * 因为 GPIO mode 由 GPIOE_CRH 控制, AFIO 只影响 AF 模式时的复用选择.
     *
     * !!! Stage 7 关键修复 (2026-05-06): 修复 Stage 6 的致命 bug !!!
     * STM32F103 reference manual: SWJ_CFG[2:0] bits at 24:26 are
     * "write-only (when read, the value is undefined)".
     * Stage 6 用了 RMW (read AFIO->MAPR, modify, write back), 把 SWJ_CFG
     * 的"未定义"读值写回, 可能写出 SWJ_CFG=100 = 禁用 SWD+JTAG, 导致 JLink
     * 永远无法连接, 板子需 BOOT0 ISP 才能挽救.
     *
     * Stage 7 修复: RMW 时显式覆盖 SWJ_CFG = 010 (NOJTAG, SWD only),
     * 与 main.c 的 __HAL_AFIO_REMAP_SWJ_NOJTAG() 保持一致.
     * 这样无论 SWJ_CFG 读返回什么值, 写回都强制是 010, SWD 永远可用. */
    {
        uint32_t mapr = AFIO->MAPR;
        mapr &= ~((0x3UL << 6) | (0x7UL << 24));   /* 清 TIM1_REMAP + SWJ_CFG */
        mapr |=  ((0x3UL << 6) | (0x2UL << 24));   /* 设 TIM1=11, SWJ_CFG=010 (NOJTAG) */
        AFIO->MAPR = mapr;
    }

    /* === PE9 配置为 AF Push-pull (TIM1_CH1 输出) === */
    gpio.Mode  = GPIO_MODE_AF_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin   = BSP_FAN_PTC_PWM_PIN;       /* PE9 */
    HAL_GPIO_Init(BSP_FAN_PTC_PWM_PORT, &gpio);

    /* === TIM1 PWM 配置: 72 MHz / 48 / 100 = 15 kHz === */
    s_htim1.Instance               = TIM1;
    s_htim1.Init.Prescaler         = 47;    /* 72 MHz / 48 = 1.5 MHz tick */
    s_htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    s_htim1.Init.Period            = 99;    /* 1.5 MHz / 100 = 15 kHz */
    s_htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    s_htim1.Init.RepetitionCounter = 0;
    s_htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&s_htim1);

    /* === TIM1_CH1 PWM Mode 1: CNT < CCR1 时 HIGH === */
    {
        TIM_OC_InitTypeDef oc = {0};
        oc.OCMode       = TIM_OCMODE_PWM1;
        oc.Pulse        = 0;                /* 初始 duty = 0 */
        oc.OCPolarity   = TIM_OCPOLARITY_HIGH;
        oc.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
        oc.OCFastMode   = TIM_OCFAST_DISABLE;
        oc.OCIdleState  = TIM_OCIDLESTATE_RESET;
        oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
        HAL_TIM_PWM_ConfigChannel(&s_htim1, &oc, TIM_CHANNEL_1);
    }

    /* === 启动 PWM 输出 (内部会设 BDTR.MOE=1, advanced timer 必须) === */
    HAL_TIM_PWM_Start(&s_htim1, TIM_CHANNEL_1);
}

/* Fan 1: PE5 内循环风机 — ON/OFF only */
void pwm_set_fan1_duty(uint8_t percent)
{
    HAL_GPIO_WritePin(BSP_FAN_NEI_PORT, BSP_FAN_NEI_PIN,
                      percent ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Fan 2: PTC风机 — PE6 (enable ON/OFF) + PE9 (TIM1_CH1 hardware PWM)
 * Stage 6 (2026-05-05): 改为硬件 PWM @ 15 kHz, CCR1 直接 = duty (0~100).
 * When percent > 0: PE6=ON + CCR1=percent
 * When percent = 0: PE6=OFF + CCR1=0 (双重保险) */
void pwm_set_fan2_duty(uint8_t percent)
{
    if (percent > 100) percent = 100;

    /* PE6 enable: 与 duty 同步 */
    HAL_GPIO_WritePin(BSP_FAN_PTC_EN_PORT, BSP_FAN_PTC_EN_PIN,
                      percent ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* PE9 PWM: TIM1_CH1 CCR1 = duty (0~100, 与 Period+1=100 对应)
     * CNT 0~99, CCR1=duty: CNT < CCR1 时 HIGH, 否则 LOW
     *   CCR1=0   → 永远 LOW (0%)
     *   CCR1=30  → 30 个 tick HIGH, 70 个 LOW (30%)
     *   CCR1=100 → CNT(0~99) 永远 < 100, 永远 HIGH (100%) */
    __HAL_TIM_SET_COMPARE(&s_htim1, TIM_CHANNEL_1, percent);
}

/* Fan 3: PC13 空调内风机 — ON/OFF only */
void pwm_set_fan3_duty(uint8_t percent)
{
    HAL_GPIO_WritePin(BSP_FAN_NEI2_PORT, BSP_FAN_NEI2_PIN,
                      percent ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Stage 3 (2026-05-05) 改: 函数体改为空 stub.
 * 旧实现驱动 PE5/PC13 (内循环+空调内风机), 但二者只能 ON/OFF, 与"风速三档"语义不符.
 * X 方案: PE5/PC13 与 fan_speed 解绑; PTC 风机三档由 pwm_set_ptc_arbiter() 统一控制.
 * PE5/PC13 现在由其他逻辑单独管理 (本次改造不动它们). */
void pwm_set_fan_speed(uint8_t level)
{
    (void)level;
    /* No-op: PTC 风机由 pwm_set_ptc_arbiter() 统一控制. */
}

/* Stage 3 新增: PTC 风机三方 max 仲裁
 * 输出 = max(safety_min, fresh_duty, user_duty), 写入 pwm_set_fan2_duty.
 * 验证矩阵 (确认书 v1.2 表 3.4):
 *   IDLE/COOL + 无新风 + fan=0       → safety=0,  fresh=0,   user=0   → 0%   关
 *   IDLE/COOL + 无新风 + fan=1 (30%) → safety=0,  fresh=0,   user=30  → 30%
 *   IDLE/COOL + 无新风 + fan=3 (100%)→ safety=0,  fresh=0,   user=100 → 100%
 *   IDLE/COOL + 新风开 + fan=任意    → safety=0,  fresh=100, user=*   → 100% 新风
 *   HEATING   + 无新风 + fan=0       → safety=80, fresh=0,   user=0   → 80%  安全
 *   HEATING   + 无新风 + fan=1 (30%) → safety=80, fresh=0,   user=30  → 80%  安全>用户
 *   HEATING   + 无新风 + fan=3 (100%)→ safety=80, fresh=0,   user=100 → 100% 用户
 *   HEATING   + 新风开 + fan=0       → safety=80, fresh=100, user=0   → 100% 新风>安全 (修正点)
 */
void pwm_set_ptc_arbiter(uint8_t safety_min, uint8_t fresh_duty, uint8_t user_duty)
{
    uint8_t duty = safety_min;
    if (fresh_duty > duty) duty = fresh_duty;
    if (user_duty  > duty) duty = user_duty;
    if (duty > 100) duty = 100;   /* 防御: 即使输入超界也限幅 */
    pwm_set_fan2_duty(duty);
}

/* Stage 6 删除: TIM6_IRQHandler 和 HAL_TIM_PeriodElapsedCallback (旧软件 PWM).
 * 当前 PE9 由 TIM1 硬件 PWM 直接驱动, CPU 无 ISR 参与. */
