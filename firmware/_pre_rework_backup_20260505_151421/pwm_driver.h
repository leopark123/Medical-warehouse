/**
 * @file    pwm_driver.h
 * @brief   Fan control: PE9 PWM (PTC fan speed) + PE5/PE6/PC13 ON/OFF
 * @note    Hardware engineer confirmed (2026-04-09):
 *          PE5  = FENGJI-NEI-IO  (内循环风机)   — ON/OFF
 *          PE6  = FENGJI-PTC-IO  (PTC风机使能)  — ON/OFF (enable gate)
 *          PC13 = FENGJI-NEI2-IO (空调内风机)   — ON/OFF
 *          PE9  = PWM1           (PTC风机调速)  — PWM (唯一调速通道)
 *
 *          Only PTC fan has speed control. All others are ON/OFF.
 *          PTC fan: PE6=enable + PE9=speed. Both managed by pwm_set_fan2_duty().
 */

#ifndef PWM_DRIVER_H
#define PWM_DRIVER_H

#include <stdint.h>

/* Fan speed levels per frozen spec */
#define FAN_SPEED_OFF       0
#define FAN_SPEED_LOW       1
#define FAN_SPEED_MID       2
#define FAN_SPEED_HIGH      3

/* Initialize all fan outputs (all off) */
void pwm_driver_init(void);

/* Set fan duty cycle (0-100%)
 * fan1: PE5 内循环风机 — ON/OFF (0=off, >0=on)
 * fan2: PE6+PE9 PTC风机 — PE6=enable, PE9=PWM speed
 * fan3: PC13 空调内风机 — ON/OFF (0=off, >0=on) */
void pwm_set_fan1_duty(uint8_t percent);
void pwm_set_fan2_duty(uint8_t percent);
void pwm_set_fan3_duty(uint8_t percent);

/* Set inner fans speed by level (fan1+fan3, not PTC) */
void pwm_set_fan_speed(uint8_t level);      /* 0=off, 1=low, 2=mid, 3=high */

#endif
