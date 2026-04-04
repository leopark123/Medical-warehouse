/**
 * @file    pwm_driver.h
 * @brief   PWM output for 3 fans — PE2/PE3/PE4
 * @note    PE2 = FENGJI-NEI-IO (内循环风机)
 *          PE3 = FENGJI-PTC-IO (PTC加热风机)
 *          PE4 = FENGJI-NEI2-IO (内循环风机2)
 *
 *          PE2/PE3/PE4 on STM32F103VET6:
 *          These pins are NOT standard timer output channels.
 *          Options: software PWM via timer ISR, or GPIO toggle.
 *          TODO: If CubeMX can remap a timer to PE2-PE4, use hardware PWM.
 *                Otherwise implement software PWM (~1kHz is sufficient for fans).
 */

#ifndef PWM_DRIVER_H
#define PWM_DRIVER_H

#include <stdint.h>

/* Fan speed levels per frozen spec */
#define FAN_SPEED_OFF       0
#define FAN_SPEED_LOW       1
#define FAN_SPEED_MID       2
#define FAN_SPEED_HIGH      3

/* Initialize PWM outputs (all off) */
void pwm_driver_init(void);

/* Set fan duty cycle (0-100%) */
void pwm_set_fan1_duty(uint8_t percent);    /* PE2: 内循环风机 */
void pwm_set_fan2_duty(uint8_t percent);    /* PE3: PTC风机 */
void pwm_set_fan3_duty(uint8_t percent);    /* PE4: 内循环风机2 */

/* Convenience: set fan speed by level (maps to duty cycle) */
void pwm_set_fan_speed(uint8_t level);      /* 0=off, 1=low, 2=mid, 3=high */

#endif
