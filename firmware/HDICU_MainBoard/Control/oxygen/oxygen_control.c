/**
 * @file    oxygen_control.c
 * @brief   O2 control — valve on/off with hysteresis (-2% = -20 in x10)
 */
#include "oxygen_control.h"
#include "bsp_config.h"

#define O2_HYSTERESIS_X10   20  /* 2.0% in x10 units */

void oxygen_control_update(AppData_t *d)
{
    /* FAIL-SAFE: O2 sensor offline → close O2 valve (except in manual open mode) */
    if (!d->sensor.o2_valid && d->control.o2_state != O2_STATE_OPEN_MODE) {
        d->control.relay_status &= ~(1U << BSP_RELAY_O2_IO);
        d->control.o2_state = O2_STATE_IDLE;
        return;
    }

    int16_t actual = (int16_t)d->sensor.o2_raw;
    int16_t setpoint = (int16_t)d->setpoint.target_o2;

    switch (d->control.o2_state) {
    case O2_STATE_IDLE:
        if (actual < setpoint - O2_HYSTERESIS_X10) {
            d->control.o2_state = O2_STATE_SUPPLYING;
        }
        break;

    case O2_STATE_SUPPLYING:
        if (actual >= setpoint) {
            d->control.o2_state = O2_STATE_IDLE;
        }
        break;

    case O2_STATE_OPEN_MODE:
        /* Open O2 mode: valve stays ON, managed by interlock module */
        break;
    }

    /* Handle open O2 mode from setpoint */
    if (d->setpoint.open_o2) {
        d->control.o2_state = O2_STATE_OPEN_MODE;
    } else if (d->control.o2_state == O2_STATE_OPEN_MODE) {
        d->control.o2_state = O2_STATE_IDLE;
    }

    /* Apply relay */
    uint16_t *r = &d->control.relay_status;
    if (d->control.o2_state == O2_STATE_SUPPLYING ||
        d->control.o2_state == O2_STATE_OPEN_MODE) {
        *r |= (1U << BSP_RELAY_O2_IO);
    } else {
        *r &= ~(1U << BSP_RELAY_O2_IO);
    }
}
