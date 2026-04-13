/**
 * @file    humidity_control.c
 * @brief   Humidity control — threshold + hysteresis (±3% = ±30 in x10 units)
 */
#include "humidity_control.h"
#include "bsp_config.h"

#define HUMID_HYSTERESIS_X10    30  /* 3.0% in x10 units */

void humidity_control_update(AppData_t *d)
{
    /* Humidity comes from O2 sensor — FAIL-SAFE if offline */
    if (!d->sensor.o2_valid) {
        uint16_t *r = &d->control.relay_status;
        *r &= ~(1U << BSP_RELAY_JIASHI_IO);    /* Humidifier off */
        /* Only clear compressor+fan if WE were using them (dehumidify) */
        if (d->control.humid_state == HUMID_STATE_DEHUMIDIFY) {
            *r &= ~(1U << BSP_RELAY_YASUO_IO);
            *r &= ~(1U << BSP_RELAY_FENGJI_IO);
        }
        d->control.humid_state = HUMID_STATE_IDLE;
        return;
    }

    int16_t actual = (int16_t)d->sensor.humidity_raw;
    int16_t setpoint = (int16_t)d->setpoint.target_humidity;

    switch (d->control.humid_state) {
    case HUMID_STATE_IDLE:
        if (actual < setpoint - HUMID_HYSTERESIS_X10) {
            d->control.humid_state = HUMID_STATE_HUMIDIFY;
        } else if (actual > setpoint + HUMID_HYSTERESIS_X10) {
            d->control.humid_state = HUMID_STATE_DEHUMIDIFY;
        }
        break;

    case HUMID_STATE_HUMIDIFY:
        if (actual >= setpoint) {
            d->control.humid_state = HUMID_STATE_IDLE;
        }
        break;

    case HUMID_STATE_DEHUMIDIFY:
        if (actual <= setpoint) {
            d->control.humid_state = HUMID_STATE_IDLE;
        }
        break;
    }

    uint16_t *r = &d->control.relay_status;

    switch (d->control.humid_state) {
    case HUMID_STATE_IDLE:
        *r &= ~(1U << BSP_RELAY_JIASHI_IO);
        /* NOTE: Do NOT clear YASUO here — temp_control owns compressor in non-dehumidify states.
         * Clearing it here would fight with temp_control COOLING state. */
        break;
    case HUMID_STATE_HUMIDIFY:
        *r |= (1U << BSP_RELAY_JIASHI_IO);
        break;
    case HUMID_STATE_DEHUMIDIFY:
        *r &= ~(1U << BSP_RELAY_JIASHI_IO);
        /* Engage cooling for dehumidification — compressor + outer fan must pair */
        *r |= (1U << BSP_RELAY_YASUO_IO);
        *r |= (1U << BSP_RELAY_FENGJI_IO);
        break;
    }
}
