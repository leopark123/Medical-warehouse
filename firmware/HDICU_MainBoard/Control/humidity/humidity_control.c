/**
 * @file    humidity_control.c
 * @brief   Humidity control — threshold + hysteresis (±3% = ±30 in x10 units)
 *
 *          Dehumidification strategy (P1 fix):
 *          The physical mechanism for lowering humidity is the AC compressor
 *          (condenses water vapor from the cabinet air). This shares the
 *          YASUO + FENGJI relays with the cooling function — and interlock
 *          rule 1 forbids running cooling and heating simultaneously.
 *
 *          Without temperature priority, a scenario like:
 *            humidity > target AND temperature < target
 *          would turn on cooling for dehumidification, interlock would kill
 *          heating, and the cabinet would cool further without ever heating —
 *          violating the primary clinical objective (keep patient warm).
 *
 *          Fix: temperature is life-critical for an incubator patient, so
 *          heating takes priority over dehumidification:
 *            - Do NOT enter DEHUMIDIFY while temp_state == HEATING
 *            - EXIT DEHUMIDIFY if temp transitions to HEATING
 *            - Release YASUO+FENGJI on exit ONLY if temp_control does not
 *              also want them (i.e. temp_state != COOLING)
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
            /* Enter DEHUMIDIFY only if temperature control is NOT calling for
             * heat. Heating is life-critical for the incubator patient and
             * must not be blocked by dehumidification's cooling demand. */
            if (d->control.temp_state != TEMP_STATE_HEATING) {
                d->control.humid_state = HUMID_STATE_DEHUMIDIFY;
            }
            /* else: stay IDLE; once temp reaches target, re-evaluate */
        }
        break;

    case HUMID_STATE_HUMIDIFY:
        if (actual >= setpoint) {
            d->control.humid_state = HUMID_STATE_IDLE;
        }
        break;

    case HUMID_STATE_DEHUMIDIFY:
        /* Exit conditions (either one triggers):
         *   1. Humidity dropped back to target (normal success)
         *   2. Temperature now needs heating — yield priority, release cooling HW */
        if (actual <= setpoint ||
            d->control.temp_state == TEMP_STATE_HEATING) {
            d->control.humid_state = HUMID_STATE_IDLE;

            /* Release compressor+fan ONLY if temp_control is not also using
             * them (temp COOLING state shares the same hardware). Otherwise
             * we'd fight temp_control and starve a legitimate cooling call. */
            if (d->control.temp_state != TEMP_STATE_COOLING) {
                d->control.relay_status &= ~(1U << BSP_RELAY_YASUO_IO);
                d->control.relay_status &= ~(1U << BSP_RELAY_FENGJI_IO);
            }
        }
        break;
    }

    uint16_t *r = &d->control.relay_status;

    switch (d->control.humid_state) {
    case HUMID_STATE_IDLE:
        *r &= ~(1U << BSP_RELAY_JIASHI_IO);
        /* NOTE: Do NOT clear YASUO/FENGJI here — temp_control owns those
         * in non-dehumidify states. Clearing would fight COOLING logic. */
        break;
    case HUMID_STATE_HUMIDIFY:
        *r |= (1U << BSP_RELAY_JIASHI_IO);
        break;
    case HUMID_STATE_DEHUMIDIFY:
        *r &= ~(1U << BSP_RELAY_JIASHI_IO);
        /* Engage cooling for dehumidification — compressor + outer fan must
         * pair. Only reached when temp_state != HEATING (guard in transition
         * switch above), so interlock rule 1 will not conflict with heating. */
        *r |= (1U << BSP_RELAY_YASUO_IO);
        *r |= (1U << BSP_RELAY_FENGJI_IO);
        break;
    }
}
