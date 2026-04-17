/**
 * @file    interlock.c
 * @brief   Safety interlock enforcement
 * @note    This module is the LAST gate before actuator output.
 *          It never ADDS actions, only BLOCKS or FORCES them.
 *
 *          switch_status bit definitions (from app_data.h):
 *            SW_BIT_INNER_CYCLE (0x01) = inner cycle active
 *            SW_BIT_FRESH_AIR   (0x02) = fresh air active
 *            SW_BIT_OPEN_O2     (0x04) = open O2 mode active
 *
 *          NOTE: bit0=0 means outer cycle (inner off = outer on).
 *          The "outer cycle relay" is CN32 新风口电磁阀 (same as fresh air path).
 *          内循环=通电(12V), 外循环=断电. (From 功能表: "内循环，通电；外循环，断电")
 */

#include "interlock.h"
#include "bsp_config.h"

/* Helper: check if a relay is on in the bitmap */
static inline bool relay_is_on(uint16_t bitmap, uint8_t idx) {
    return (bitmap & (1U << idx)) != 0;
}
static inline void relay_set(uint16_t *bitmap, uint8_t idx) {
    *bitmap |= (1U << idx);
}
static inline void relay_clear(uint16_t *bitmap, uint8_t idx) {
    *bitmap &= ~(1U << idx);
}

/**
 * @brief Map setpoint switches to control.switch_status BEFORE interlock enforcement.
 *        Call this at the start of interlock_apply so the base state is correct.
 */
static void sync_switch_status(AppData_t *d)
{
    d->control.switch_status = 0;

    if (d->setpoint.inner_cycle) {
        d->control.switch_status |= SW_BIT_INNER_CYCLE;
    }
    if (d->setpoint.fresh_air) {
        d->control.switch_status |= SW_BIT_FRESH_AIR;
    }
    if (d->setpoint.open_o2) {
        d->control.switch_status |= SW_BIT_OPEN_O2;
    }
}

bool interlock_apply(AppData_t *d)
{
    bool triggered = false;
    uint16_t *r = &d->control.relay_status;

    /* Step 0: Sync setpoint switches to control state */
    sync_switch_status(d);

    /* Step 1: Snapshot relay state BEFORE any interlock modifications.
     * This is critical for Rule 5: we need to know if fogging was
     * running before Rule 4 clears it. */
    const bool fogging_active = relay_is_on(*r, BSP_RELAY_WH_IO);
    const bool open_o2_requested = (d->control.switch_status & SW_BIT_OPEN_O2) != 0;
    const bool external_o2_demand = d->sensor.o2_master_demand || d->sensor.o2_req_demand;

    /* ------------------------------------------------------------------- */
    /* Rule 5: During fogging, Open O2 forbidden (MUST run BEFORE Rule 4)  */
    /* Extended: external O2 demand (PD8/PB6) also blocked during fogging  */
    /* to prevent oxygen-rich + aerosol mix (safety critical).             */
    /* ------------------------------------------------------------------- */
    if (fogging_active && (open_o2_requested || external_o2_demand)) {
        /* Fogging takes priority — block open O2 request */
        d->control.switch_status &= ~SW_BIT_OPEN_O2;
        d->setpoint.open_o2 = 0;   /* Revert setpoint to prevent re-sync next cycle */

        /* Close O2 valve unless normal supply (oxygen_control) needs it
         * 注意: 外部请求被互锁阻止时, O2阀会被强制关闭 — 外部设备的请求在此失效 */
        if (d->control.o2_state != O2_STATE_SUPPLYING) {
            relay_clear(r, BSP_RELAY_O2_IO);
        }
        triggered = true;
    }

    /* ------------------------------------------------------------------- */
    /* Rule 4: Open O2 mode — most complex interlock set                   */
    /* Only runs if open_o2 survived Rule 5 above.                         */
    /* ------------------------------------------------------------------- */
    if (d->control.switch_status & SW_BIT_OPEN_O2) {
        /* O2 valve: ON */
        relay_set(r, BSP_RELAY_O2_IO);

        /* Inner cycle: FORBIDDEN — force off */
        d->control.switch_status &= ~SW_BIT_INNER_CYCLE;
        /* CN32 FAI solenoid valve GPIO not yet defined in BSP. inner_cycle/fresh_air
         * currently only sets software status bits (switch_status). Hardware output will
         * be added after hardware engineer confirms CN32 GPIO pin. */

        /* Outer cycle: FORCED ON (= inner off, done above) */
        /* Fresh air: FORCED ON */
        d->control.switch_status |= SW_BIT_FRESH_AIR;
        /* CN32 FAI solenoid valve GPIO not yet defined in BSP. inner_cycle/fresh_air
         * currently only sets software status bits (switch_status). Hardware output will
         * be added after hardware engineer confirms CN32 GPIO pin. */

        /* Heating: FORBIDDEN */
        relay_clear(r, BSP_RELAY_PTC_IO);
        relay_clear(r, BSP_RELAY_JIARE_IO);

        /* Fogging: FORBIDDEN (if somehow still on) */
        relay_clear(r, BSP_RELAY_WH_IO);
        d->control.fog_remaining = 0;

        /* UV disinfect: FORBIDDEN */
        relay_clear(r, BSP_RELAY_ZIY_IO);
        d->control.disinfect_remaining = 0;

        /* Cooling: CONDITIONAL — allowed because we just forced outer+fresh on */

        triggered = true;
    }

    /* ------------------------------------------------------------------- */
    /* Rule 1: Cooling ↔ Heating mutually exclusive                        */
    /* ------------------------------------------------------------------- */
    bool cooling = relay_is_on(*r, BSP_RELAY_YASUO_IO);
    bool heating = relay_is_on(*r, BSP_RELAY_PTC_IO) ||
                   relay_is_on(*r, BSP_RELAY_JIARE_IO);

    if (cooling && heating) {
        relay_clear(r, BSP_RELAY_PTC_IO);
        relay_clear(r, BSP_RELAY_JIARE_IO);
        triggered = true;
    }

    /* ------------------------------------------------------------------- */
    /* Rule 2: UV disinfect ↔ Open O2 mutually exclusive                   */
    /* 原规则: UV让步给手动开放供氧                                         */
    /* 扩展: 外部O2请求(PD8/PB6) 让步给UV消毒 — UV安全优先                  */
    /* ------------------------------------------------------------------- */
    if (relay_is_on(*r, BSP_RELAY_ZIY_IO) &&
        (d->control.switch_status & SW_BIT_OPEN_O2)) {
        relay_clear(r, BSP_RELAY_ZIY_IO);
        d->control.disinfect_remaining = 0;
        triggered = true;
    }
    if (relay_is_on(*r, BSP_RELAY_ZIY_IO) && external_o2_demand) {
        /* UV运行中, 外部O2请求 让步 (UV优先) */
        if (d->control.o2_state != O2_STATE_SUPPLYING &&
            !(d->control.switch_status & SW_BIT_OPEN_O2)) {
            relay_clear(r, BSP_RELAY_O2_IO);
        }
        triggered = true;
    }

    /* ------------------------------------------------------------------- */
    /* Rule 3: Fogging ↔ UV disinfect mutually exclusive                   */
    /* ------------------------------------------------------------------- */
    if (relay_is_on(*r, BSP_RELAY_WH_IO) && relay_is_on(*r, BSP_RELAY_ZIY_IO)) {
        relay_clear(r, BSP_RELAY_ZIY_IO);
        d->control.disinfect_remaining = 0;
        triggered = true;
    }

    /* ------------------------------------------------------------------- */
    /* Rule 6: Heating blocked during open O2 (redundant safety check)     */
    /* ------------------------------------------------------------------- */
    if ((d->control.switch_status & SW_BIT_OPEN_O2) &&
        (relay_is_on(*r, BSP_RELAY_PTC_IO) || relay_is_on(*r, BSP_RELAY_JIARE_IO))) {
        relay_clear(r, BSP_RELAY_PTC_IO);
        relay_clear(r, BSP_RELAY_JIARE_IO);
        triggered = true;
    }

    return triggered;
}

/* ========================================================================= */
/*  Pre-start checks                                                         */
/* ========================================================================= */

bool interlock_can_start_open_o2(const AppData_t *d)
{
    if (relay_is_on(d->control.relay_status, BSP_RELAY_ZIY_IO)) return false;
    if (relay_is_on(d->control.relay_status, BSP_RELAY_WH_IO)) return false;
    return true;
}

bool interlock_can_start_fogging(const AppData_t *d)
{
    if (relay_is_on(d->control.relay_status, BSP_RELAY_ZIY_IO)) return false;
    if (d->control.switch_status & SW_BIT_OPEN_O2) return false;
    return true;
}

bool interlock_can_start_uv(const AppData_t *d)
{
    if (d->control.switch_status & SW_BIT_OPEN_O2) return false;
    if (relay_is_on(d->control.relay_status, BSP_RELAY_WH_IO)) return false;
    return true;
}

bool interlock_can_start_heating(const AppData_t *d)
{
    if (d->control.switch_status & SW_BIT_OPEN_O2) return false;
    if (relay_is_on(d->control.relay_status, BSP_RELAY_YASUO_IO)) return false;
    return true;
}

bool interlock_can_start_cooling(const AppData_t *d)
{
    if (relay_is_on(d->control.relay_status, BSP_RELAY_PTC_IO)) return false;
    if (relay_is_on(d->control.relay_status, BSP_RELAY_JIARE_IO)) return false;
    if (d->control.switch_status & SW_BIT_OPEN_O2) {
        bool outer_on = !(d->control.switch_status & SW_BIT_INNER_CYCLE);
        bool fresh_on = (d->control.switch_status & SW_BIT_FRESH_AIR) != 0;
        return outer_on && fresh_on;
    }
    return true;
}
