/**
 * @file    temp_control.c
 * @brief   Temperature control — threshold + hysteresis (±1°C = ±10 in x10 units)
 * @note    PTC fan (PE6 enable + PE9 PWM) MUST run when PTC heater is on (safety: no airflow = fire risk).
 *
 *          Stage 3 (2026-05-05) 重构: temp_control 不再直接调 pwm_set_fan2_duty().
 *          风机控制语义改为: temp_state 是 source-of-truth, ControlTask 末尾根据
 *          temp_state == HEATING 推算 safety_min (80%), 三方 max 仲裁后写 PTC 风机.
 *          这样可与 fresh_air / user fan_speed 协调, 满足 IEC 60601 加热不通风着火的安全规则.
 */
#include "temp_control.h"
#include "bsp_config.h"

#define TEMP_HYSTERESIS_X10     10  /* 1.0°C in x10 units */
/* PTC_FAN_DUTY_PERCENT 80 已迁移到 ControlTask 末尾的仲裁逻辑里 */

void temp_control_update(AppData_t *d)
{
    /* === Stage 8 (2026-05-06): 温控未启用 → 全关 + IDLE + return ===
     * 温控全权拥有 PTC/JIARE/YASUO/FENGJI 四继电器 (humidity 在 enable=0 时退让, oxygen 不用这些).
     * enable=0 路径直接清, 让 ControlTask 末尾的 PTC 风机仲裁看到 temp_state=IDLE → safety_min=0.
     * 这条门写在 sensor 验证之前, 避免传感器有效但用户未启用闭环时仍跑状态机. */
    if (!d->setpoint.enable_temp_ctrl) {
        uint16_t *r = &d->control.relay_status;
        *r &= ~((1U << BSP_RELAY_PTC_IO)
              | (1U << BSP_RELAY_JIARE_IO)
              | (1U << BSP_RELAY_YASUO_IO)
              | (1U << BSP_RELAY_FENGJI_IO));
        d->control.temp_state = TEMP_STATE_IDLE;
        return;
    }

    /* Sensor validity check: -999 = all channels invalid,
     * or temperature outside physically possible range (-40°C ~ +80°C).
     * Without this, disconnected NTC (ADC≈4094) gives ~-90°C → false heating. */
    /* 温控只用PA4(NTC通道2), 与屏幕显示一致 */
    if (d->sensor.temperature[2] == -999 ||
        d->sensor.temperature[2] < -400 ||
        d->sensor.temperature[2] > 800) {
        /* FAIL-SAFE: sensor invalid → force all temp-owned outputs OFF.
         * Do NOT "hold current state" — that leaves heaters running with no feedback.
         * Stage 3: PTC 风机不在此关; 让 ControlTask 末尾仲裁判断 (temp_state=IDLE
         * → safety_min=0, 风机仅由 fresh_air/user fan_speed 决定, 这是正确语义). */
        uint16_t *r = &d->control.relay_status;
        *r &= ~(1U << BSP_RELAY_PTC_IO);
        *r &= ~(1U << BSP_RELAY_JIARE_IO);
        *r &= ~(1U << BSP_RELAY_YASUO_IO);
        *r &= ~(1U << BSP_RELAY_FENGJI_IO);
        d->control.temp_state = TEMP_STATE_IDLE;
        return;
    }

    int16_t actual = d->sensor.temperature[2];  /* PA4 */
    int16_t setpoint = (int16_t)d->setpoint.target_temp;
    int16_t upper = setpoint + TEMP_HYSTERESIS_X10;
    int16_t lower = setpoint - TEMP_HYSTERESIS_X10;

    switch (d->control.temp_state) {
    case TEMP_STATE_IDLE:
        if (actual > upper) {
            d->control.temp_state = TEMP_STATE_COOLING;
        } else if (actual < lower) {
            d->control.temp_state = TEMP_STATE_HEATING;
        }
        break;

    case TEMP_STATE_COOLING:
        if (actual <= setpoint) {
            d->control.temp_state = TEMP_STATE_IDLE;
        }
        break;

    case TEMP_STATE_HEATING:
        if (actual >= setpoint) {
            d->control.temp_state = TEMP_STATE_IDLE;
        }
        break;
    }

    /* Apply relay + PWM outputs based on state */
    uint16_t *r = &d->control.relay_status;

    switch (d->control.temp_state) {
    case TEMP_STATE_IDLE:
        *r &= ~(1U << BSP_RELAY_YASUO_IO);     /* Compressor off */
        *r &= ~(1U << BSP_RELAY_FENGJI_IO);    /* Outer fan off — clear after cooling ends */
        *r &= ~(1U << BSP_RELAY_PTC_IO);        /* PTC off */
        *r &= ~(1U << BSP_RELAY_JIARE_IO);      /* Bottom heater off */
        /* Stage 3: PTC 风机由 ControlTask 末尾仲裁 (IDLE → safety_min=0) */
        break;

    case TEMP_STATE_COOLING:
        *r |= (1U << BSP_RELAY_YASUO_IO);       /* Compressor ON */
        *r |= (1U << BSP_RELAY_FENGJI_IO);      /* Outer fan ON */
        *r &= ~(1U << BSP_RELAY_PTC_IO);        /* PTC off */
        *r &= ~(1U << BSP_RELAY_JIARE_IO);      /* Bottom heater off */
        /* Stage 3: PTC 风机由 ControlTask 末尾仲裁 (COOLING → safety_min=0) */
        break;

    case TEMP_STATE_HEATING:
        *r |= (1U << BSP_RELAY_PTC_IO);         /* PTC heater ON */
        *r |= (1U << BSP_RELAY_JIARE_IO);       /* Bottom heater ON */
        *r &= ~(1U << BSP_RELAY_YASUO_IO);      /* Compressor off */
        /* Stage 3: PTC 风机由 ControlTask 末尾仲裁 (HEATING → safety_min=80, 不通风着火) */
        break;
    }
}
