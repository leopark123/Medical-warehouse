/**
 * @file    control_timers.c
 * @brief   Countdown and accumulation timer logic
 */
#include "control_timers.h"
#include "bsp_config.h"
#include "interlock.h"
#include "FreeRTOS.h"   /* configASSERT */

void control_timers_tick_1s(AppData_t *d)
{
    configASSERT(d != NULL);

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

    /* O2 accumulation — Stage 8 redo v4 (2026-05-07):
     * 累加条件从 "setpoint.open_o2=1" 改成 "BSP_RELAY_O2_IO 阀实际开".
     * 这样手动开放供氧 / 编码器调 target_o2 触发自动闭环 / 外部 PD8/PB6 demand
     * 任何让 O2 阀打开的路径都会累加, 反映"实际供氧时长". */
    if ((d->control.relay_status & (1U << BSP_RELAY_O2_IO)) &&
        d->control.o2_accumulated < 0xFFFF) {
        d->control.o2_accumulated++;
    }
}

void control_timers_start_fog(AppData_t *d, uint16_t duration_sec)
{
    configASSERT(d != NULL);
    /* FIX I3: compare against runtime limits, not hard-coded 3600.
     * iPad 0x09 may legitimately adjust limits.fog_upper in the future.
     * duration_sec == 0 means "stop" and is always allowed. */
    configASSERT(duration_sec == 0 || duration_sec <= d->limits.fog_upper);

    if (duration_sec == 0) {
        d->control.fog_remaining = 0;
        d->setpoint.fog_time = 0;          /* Stage 8 redo v2 修订: 同步清 setpoint, 让屏幕板 0x01 帧 byte 34-35 真实反映 */
        d->control.relay_status &= ~(1U << BSP_RELAY_WH_IO);
    } else {
        /* Interlock pre-check: fogging forbidden during open O2, UV, etc. */
        if (!interlock_can_start_fogging(d)) return;
        d->control.fog_remaining = duration_sec;
        d->setpoint.fog_time = duration_sec;  /* Stage 8 redo v2 修订: 同步设 setpoint, 修 T4 显示归 0 bug */
        d->control.relay_status |= (1U << BSP_RELAY_WH_IO);
    }
}

void control_timers_start_disinfect(AppData_t *d, uint16_t duration_sec)
{
    configASSERT(d != NULL);
    /* FIX I3: dynamic limit — uv_upper covers UV/disinfect cycle */
    configASSERT(duration_sec == 0 || duration_sec <= d->limits.uv_upper);

    if (duration_sec == 0) {
        d->control.disinfect_remaining = 0;
        d->setpoint.disinfect_time = 0;       /* Stage 8 redo v2 修订: 同步清 setpoint */
        d->control.relay_status &= ~(1U << BSP_RELAY_ZIY_IO);
    } else {
        /* Interlock pre-check: UV forbidden during open O2, fogging, etc. */
        if (!interlock_can_start_uv(d)) return;
        d->control.disinfect_remaining = duration_sec;
        d->setpoint.disinfect_time = duration_sec;  /* Stage 8 redo v2 修订: 同步设 setpoint, 修 T8 显示归 0 bug */
        d->control.relay_status |= (1U << BSP_RELAY_ZIY_IO);
    }
}

void control_timers_reset_o2_accum(AppData_t *d)
{
    configASSERT(d != NULL);
    d->control.o2_accumulated = 0;
}
