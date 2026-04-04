/**
 * @file    control_timers.c
 * @brief   Countdown and accumulation timer logic
 */
#include "control_timers.h"
#include "bsp_config.h"

void control_timers_tick_1s(AppData_t *d)
{
    /* Fogging countdown */
    if (d->control.fog_remaining > 0) {
        d->control.fog_remaining--;
        if (d->control.fog_remaining == 0) {
            /* Timer expired: turn off fogging relay */
            d->control.relay_status &= ~(1U << BSP_RELAY_WH_IO);
            /* TODO: Send buzzer command to screen board */
        }
    }

    /* Disinfect countdown */
    if (d->control.disinfect_remaining > 0) {
        d->control.disinfect_remaining--;
        if (d->control.disinfect_remaining == 0) {
            /* Timer expired: turn off UV relay */
            d->control.relay_status &= ~(1U << BSP_RELAY_ZIY_IO);
            /* TODO: Send buzzer command to screen board */
        }
    }

    /* O2 accumulation: increment while open O2 is active */
    if (d->setpoint.open_o2 && d->control.o2_accumulated < 0xFFFF) {
        d->control.o2_accumulated++;
    }
}

void control_timers_start_fog(AppData_t *d, uint16_t duration_sec)
{
    if (duration_sec == 0) {
        d->control.fog_remaining = 0;
        d->control.relay_status &= ~(1U << BSP_RELAY_WH_IO);
    } else {
        d->control.fog_remaining = duration_sec;
        d->control.relay_status |= (1U << BSP_RELAY_WH_IO);
    }
}

void control_timers_start_disinfect(AppData_t *d, uint16_t duration_sec)
{
    if (duration_sec == 0) {
        d->control.disinfect_remaining = 0;
        d->control.relay_status &= ~(1U << BSP_RELAY_ZIY_IO);
    } else {
        d->control.disinfect_remaining = duration_sec;
        d->control.relay_status |= (1U << BSP_RELAY_ZIY_IO);
    }
}

void control_timers_reset_o2_accum(AppData_t *d)
{
    d->control.o2_accumulated = 0;
}
