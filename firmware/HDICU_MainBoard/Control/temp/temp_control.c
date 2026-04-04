/**
 * @file    temp_control.c
 * @brief   Temperature control — threshold + hysteresis (±1°C = ±10 in x10 units)
 * @note    PTC fan (PE3) MUST run when PTC heater is on (safety: no airflow = fire risk).
 */
#include "temp_control.h"
#include "bsp_config.h"
#include "pwm_driver.h"

#define TEMP_HYSTERESIS_X10     10  /* 1.0°C in x10 units */
#define PTC_FAN_DUTY_PERCENT    80  /* PE3 duty when PTC heating active */

void temp_control_update(AppData_t *d)
{
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
        *r &= ~(1U << BSP_RELAY_PTC_IO);        /* PTC off */
        *r &= ~(1U << BSP_RELAY_JIARE_IO);      /* Bottom heater off */
        pwm_set_fan2_duty(0);                    /* PE3 PTC fan off */
        break;

    case TEMP_STATE_COOLING:
        *r |= (1U << BSP_RELAY_YASUO_IO);       /* Compressor ON */
        *r |= (1U << BSP_RELAY_FENGJI_IO);      /* Outer fan ON */
        *r &= ~(1U << BSP_RELAY_PTC_IO);        /* PTC off */
        *r &= ~(1U << BSP_RELAY_JIARE_IO);      /* Bottom heater off */
        pwm_set_fan2_duty(0);                    /* PE3 PTC fan off during cooling */
        break;

    case TEMP_STATE_HEATING:
        *r |= (1U << BSP_RELAY_PTC_IO);         /* PTC heater ON */
        *r |= (1U << BSP_RELAY_JIARE_IO);       /* Bottom heater ON */
        pwm_set_fan2_duty(PTC_FAN_DUTY_PERCENT); /* PE3 PTC fan ON — SAFETY CRITICAL */
        *r &= ~(1U << BSP_RELAY_YASUO_IO);      /* Compressor off */
        break;
    }
}
