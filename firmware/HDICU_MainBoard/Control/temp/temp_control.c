/**
 * @file    temp_control.c
 * @brief   Temperature control — threshold + hysteresis (±1°C = ±10 in x10 units)
 * @note    PTC fan (PE6 enable + PE9 PWM) MUST run when PTC heater is on (safety: no airflow = fire risk).
 */
#include "temp_control.h"
#include "bsp_config.h"
#include "pwm_driver.h"

#define TEMP_HYSTERESIS_X10     10  /* 1.0°C in x10 units */
#define PTC_FAN_DUTY_PERCENT    80  /* PE9 PWM duty when PTC heating active (PE6 auto-enables) */

void temp_control_update(AppData_t *d)
{
    /* Sensor validity check: -999 = all channels invalid,
     * or temperature outside physically possible range (-40°C ~ +80°C).
     * Without this, disconnected NTC (ADC≈4094) gives ~-90°C → false heating. */
    if (d->sensor.temperature_avg == -999 ||
        d->sensor.temperature_avg < -400 ||
        d->sensor.temperature_avg > 800) {
        /* FAIL-SAFE: sensor invalid → force all temp-owned outputs OFF.
         * Do NOT "hold current state" — that leaves heaters running with no feedback. */
        uint16_t *r = &d->control.relay_status;
        *r &= ~(1U << BSP_RELAY_PTC_IO);
        *r &= ~(1U << BSP_RELAY_JIARE_IO);
        *r &= ~(1U << BSP_RELAY_YASUO_IO);
        *r &= ~(1U << BSP_RELAY_FENGJI_IO);
        pwm_set_fan2_duty(0);
        d->control.temp_state = TEMP_STATE_IDLE;
        return;
    }

    int16_t actual = d->sensor.temperature_avg;
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
        pwm_set_fan2_duty(0);                    /* PTC fan off (PE6=OFF, PE9=0) */
        break;

    case TEMP_STATE_COOLING:
        *r |= (1U << BSP_RELAY_YASUO_IO);       /* Compressor ON */
        *r |= (1U << BSP_RELAY_FENGJI_IO);      /* Outer fan ON */
        *r &= ~(1U << BSP_RELAY_PTC_IO);        /* PTC off */
        *r &= ~(1U << BSP_RELAY_JIARE_IO);      /* Bottom heater off */
        pwm_set_fan2_duty(0);                    /* PTC fan off during cooling (PE6=OFF, PE9=0) */
        break;

    case TEMP_STATE_HEATING:
        *r |= (1U << BSP_RELAY_PTC_IO);         /* PTC heater ON */
        *r |= (1U << BSP_RELAY_JIARE_IO);       /* Bottom heater ON */
        pwm_set_fan2_duty(PTC_FAN_DUTY_PERCENT); /* PTC fan ON (PE6=ON, PE9=PWM) — SAFETY CRITICAL */
        *r &= ~(1U << BSP_RELAY_YASUO_IO);      /* Compressor off */
        break;
    }
}
