/**
 * @file    pwm_driver.c
 * @brief   Fan control driver
 * @note    Hardware engineer confirmed (2026-04-09):
 *          - Only PTC fan needs speed control, via PE9 (PWM1)
 *          - PE5 (内循环风机), PE6 (PTC风机使能), PC13 (空调内风机): all ON/OFF
 *
 *          Architecture:
 *          - PE9: software PWM via TIM6 ISR at 10kHz (100 steps = 100Hz)
 *          - PE5/PE6/PC13: direct GPIO write (no ISR involvement)
 *
 *          PTC fan control: PE6=enable(ON/OFF) + PE9=speed(PWM).
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

static volatile uint8_t s_ptc_duty;     /* PTC fan PWM duty (0-100) — shared with ISR */
static volatile uint8_t s_pwm_counter;  /* 0-99, incremented by TIM6 ISR */
static TIM_HandleTypeDef s_htim6;

void pwm_driver_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;

    /* PE5 (内循环风机) + PE6 (PTC风机使能) + PE9 (PTC风机PWM) on GPIOE */
    __HAL_RCC_GPIOE_CLK_ENABLE();
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = BSP_FAN_NEI_PIN | BSP_FAN_PTC_EN_PIN | BSP_FAN_PTC_PWM_PIN;
    HAL_GPIO_Init(GPIOE, &gpio);

    /* PC13 (空调内风机) on GPIOC — low speed (PC13 limitation) */
    __HAL_RCC_GPIOC_CLK_ENABLE();
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Pin = BSP_FAN_NEI2_PIN;
    HAL_GPIO_Init(BSP_FAN_NEI2_PORT, &gpio);

    /* All fans off */
    HAL_GPIO_WritePin(BSP_FAN_NEI_PORT,     BSP_FAN_NEI_PIN,     GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BSP_FAN_PTC_EN_PORT,  BSP_FAN_PTC_EN_PIN,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BSP_FAN_PTC_PWM_PORT, BSP_FAN_PTC_PWM_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BSP_FAN_NEI2_PORT,    BSP_FAN_NEI2_PIN,    GPIO_PIN_RESET);

    s_ptc_duty = 0;
    s_pwm_counter = 0;

    /* Configure TIM6 as 10kHz basic timer for PTC fan software PWM */
    __HAL_RCC_TIM6_CLK_ENABLE();

    s_htim6.Instance = TIM6;
    s_htim6.Init.Prescaler = 71;            /* 72MHz / 72 = 1MHz tick */
    s_htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_htim6.Init.Period = 99;               /* 1MHz / 100 = 10kHz overflow */
    s_htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&s_htim6);

    HAL_NVIC_SetPriority(TIM6_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(TIM6_IRQn);
    HAL_TIM_Base_Start_IT(&s_htim6);
}

/**
 * @brief Software PWM tick — called from TIM6 ISR at 10kHz
 *        Only drives PE9 (PTC fan speed). All other fans are ON/OFF.
 */
static void pwm_timer_isr(void)
{
    uint8_t cnt = s_pwm_counter + 1;
    if (cnt >= 100) cnt = 0;
    s_pwm_counter = cnt;

    uint8_t d = s_ptc_duty;

    /* PE9: PTC fan speed PWM */
    HAL_GPIO_WritePin(BSP_FAN_PTC_PWM_PORT, BSP_FAN_PTC_PWM_PIN,
                      (cnt < d) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Fan 1: PE5 内循环风机 — ON/OFF only */
void pwm_set_fan1_duty(uint8_t percent)
{
    HAL_GPIO_WritePin(BSP_FAN_NEI_PORT, BSP_FAN_NEI_PIN,
                      percent ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* Fan 2: PTC风机 — PE6(enable ON/OFF) + PE9(speed PWM)
 * When percent > 0: PE6=ON, PE9=PWM at given duty
 * When percent = 0: PE6=OFF, PE9=0 */
void pwm_set_fan2_duty(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_ptc_duty = percent;
    HAL_GPIO_WritePin(BSP_FAN_PTC_EN_PORT, BSP_FAN_PTC_EN_PIN,
                      percent ? GPIO_PIN_SET : GPIO_PIN_RESET);
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

/* ===== TIM6 Interrupt Handling ===== */

void TIM6_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&s_htim6);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        pwm_timer_isr();
    }
}
