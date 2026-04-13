/**
 * @file    control_timers.c
 * @brief   Countdown and accumulation timer logic
 */
#include "control_timers.h"
#include "bsp_config.h"
#include "interlock.h"

void control_timers_tick_1s(AppData_t *d)
{
    /* Fogging countdown */
    if (d->control.fog_remaining > 0) {
        d->control.fog_remaining--;
        if (d->control.fog_remaining == 0) {
            /* Timer expired: turn off fogging relay + request beep */
            d->control.relay_status &= ~(1U << BSP_RELAY_WH_IO);
            d->control.timer_beep_request |= 0x01;     /* bit0 = fog done */
            d->control.timer_beep_counter = 15;         /* 15 × 200ms = 3s beep */
        }
    }

    /* Disinfect countdown */
    if (d->control.disinfect_remaining > 0) {
        d->control.disinfect_remaining--;
        if (d->control.disinfect_remaining == 0) {
            /* Timer expired: turn off UV relay + request beep */
            d->control.relay_status &= ~(1U << BSP_RELAY_ZIY_IO);
            d->control.timer_beep_request |= 0x02;     /* bit1 = disinfect done */
            d->control.timer_beep_counter = 15;         /* 15 × 200ms = 3s beep */
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
        /* Interlock pre-check: fogging forbidden during open O2, UV, etc. */
        if (!interlock_can_start_fogging(d)) return;
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
        /* Interlock pre-check: UV forbidden during open O2, fogging, etc. */
        if (!interlock_can_start_uv(d)) return;
        d->control.disinfect_remaining = duration_sec;
        d->control.relay_status |= (1U << BSP_RELAY_ZIY_IO);
    }
}

void control_timers_reset_o2_accum(AppData_t *d)
{
    d->control.o2_accumulated = 0;
}
