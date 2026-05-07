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

/* Set inner fans speed by level (fan1+fan3, not PTC)
 * Stage 3 (2026-05-05) 改: 函数体已变成空 stub (X 方案: PE5/PC13 与 fan_speed 解绑).
 *   PTC 风机的真三档调速由 pwm_set_ptc_arbiter() 统一管理.
 *   保留函数声明以避免 ABI 破坏, 但调用无效果. */
void pwm_set_fan_speed(uint8_t level);      /* 0=off, 1=low, 2=mid, 3=high */

/* === Stage 3 新增 (2026-05-05): PTC 风机三方 max 仲裁 ===
 * PTC 风机 (PE6+PE9) 由三个独立逻辑共享:
 *   1. temp_control HEATING 时安全要求最低 80% (不通风着火)
 *   2. fresh_air 新风模式强制 100%
 *   3. 用户 fan_speed 1/2/3 档 = 30/60/100%
 * 三方期望取最大值, 写到 pwm_set_fan2_duty.
 *
 * 调用约定: ControlTask 末尾统一调用一次, 传入三方当前需求 (0~100).
 *
 * @param safety_min  加热安全最低 duty (HEATING 时 80, 否则 0)
 * @param fresh_duty  新风需求 duty (FRESH_AIR active 时 100, 否则 0)
 * @param user_duty   用户档位 duty (查 pwm_fan_level_duty[fan_speed_actual])
 */
void pwm_set_ptc_arbiter(uint8_t safety_min, uint8_t fresh_duty, uint8_t user_duty);

/* 风速档位 → PWM duty 映射表 (4 档 0/30/60/100). 供 ControlTask 转换 fan_speed → user_duty */
extern const uint8_t pwm_fan_level_duty[4];

#endif
