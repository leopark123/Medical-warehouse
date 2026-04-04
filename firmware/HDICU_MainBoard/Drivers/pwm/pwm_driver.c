/**
 * @file    pwm_driver.c
 * @brief   Software PWM for PE2/PE3/PE4 fan control using TIM6 basic timer
 * @note    PE2/PE3/PE4 are not standard timer output channels on STM32F103VET6.
 *          We use TIM6 at 10kHz to toggle GPIOs for 100Hz PWM (100 steps).
 *
 *          TIM6 config: 72MHz / (PSC+1) / (ARR+1) = 10kHz
 *          PSC=71, ARR=99 -> 72MHz / 72 / 100 = 10kHz
 */

#include "pwm_driver.h"
#include "bsp_config.h"
#include "stm32f1xx_hal.h"

/* Duty cycle mapping for fan speed levels */
static const uint8_t s_speed_duty[] = {
    0,      /* OFF */
    30,     /* LOW: 30% duty */
    60,     /* MID: 60% duty */
    100,    /* HIGH: 100% duty */
};

static uint8_t s_duty[3];      /* Current duty for each fan (0-100) */
static uint8_t s_pwm_counter;  /* 0-99, incremented by TIM6 ISR */
static TIM_HandleTypeDef s_htim6;

void pwm_driver_init(void)
{
    /* GPIO init for PE2/PE3/PE4 as push-pull output */
    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    __HAL_RCC_GPIOE_CLK_ENABLE();

    gpio.Pin = BSP_PWM_FAN1_PIN | BSP_PWM_FAN2_PIN | BSP_PWM_FAN3_PIN;
    HAL_GPIO_Init(BSP_PWM_FAN1_PORT, &gpio);

    /* All fans off */
    HAL_GPIO_WritePin(BSP_PWM_FAN1_PORT, BSP_PWM_FAN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BSP_PWM_FAN2_PORT, BSP_PWM_FAN2_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(BSP_PWM_FAN3_PORT, BSP_PWM_FAN3_PIN, GPIO_PIN_RESET);

    s_duty[0] = s_duty[1] = s_duty[2] = 0;
    s_pwm_counter = 0;

    /* Configure TIM6 as 10kHz basic timer for software PWM */
    __HAL_RCC_TIM6_CLK_ENABLE();

    s_htim6.Instance = TIM6;
    s_htim6.Init.Prescaler = 71;            /* 72MHz / 72 = 1MHz tick */
    s_htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_htim6.Init.Period = 99;               /* 1MHz / 100 = 10kHz overflow */
    s_htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_Base_Init(&s_htim6);

    /* Enable TIM6 update interrupt — high priority for low PWM jitter */
    HAL_NVIC_SetPriority(TIM6_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM6_IRQn);

    /* Start timer with interrupt */
    HAL_TIM_Base_Start_IT(&s_htim6);
}

/**
 * @brief Software PWM tick — called from TIM6 ISR at 10kHz
 *        100 steps -> 100Hz PWM frequency, sufficient for DC fan motors
 */
static void pwm_timer_isr(void)
{
    s_pwm_counter++;
    if (s_pwm_counter >= 100) s_pwm_counter = 0;

    /* Fan 1 (PE2) */
    HAL_GPIO_WritePin(BSP_PWM_FAN1_PORT, BSP_PWM_FAN1_PIN,
                      (s_pwm_counter < s_duty[0]) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* Fan 2 (PE3) */
    HAL_GPIO_WritePin(BSP_PWM_FAN2_PORT, BSP_PWM_FAN2_PIN,
                      (s_pwm_counter < s_duty[1]) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    /* Fan 3 (PE4) */
    HAL_GPIO_WritePin(BSP_PWM_FAN3_PORT, BSP_PWM_FAN3_PIN,
                      (s_pwm_counter < s_duty[2]) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void pwm_set_fan1_duty(uint8_t percent) { s_duty[0] = (percent > 100) ? 100 : percent; }
void pwm_set_fan2_duty(uint8_t percent) { s_duty[1] = (percent > 100) ? 100 : percent; }
void pwm_set_fan3_duty(uint8_t percent) { s_duty[2] = (percent > 100) ? 100 : percent; }

void pwm_set_fan_speed(uint8_t level)
{
    if (level > FAN_SPEED_HIGH) level = FAN_SPEED_HIGH;
    uint8_t duty = s_speed_duty[level];
    s_duty[0] = duty;   /* PE2: Inner cycle fan */
    s_duty[2] = duty;   /* PE4: Inner cycle fan 2 */
    /* PE3 (PTC fan) independently controlled by temp_control via pwm_set_fan2_duty() */
}

/* ===== TIM6 Interrupt Handling ===== */

/* TIM6 IRQ Handler — vector table entry */
void TIM6_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&s_htim6);
}

/* HAL timer period elapsed callback — dispatches to software PWM */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) {
        pwm_timer_isr();
    }
}
