/**
 * @file    sensor_sanity.c
 * @brief   Sensor plausibility implementation
 *
 *          Sampling cadence: 100ms (SensorTask). Rates are expressed per
 *          second and converted to per-100ms internally for delta checks.
 *
 *          First call after init (or INT16_MIN sentinel) bypasses rate
 *          check since we have no baseline.
 *
 *          FIX I4/I5: `valid` parameter clears baseline on comms dropout
 *          to prevent spurious rate violations on reconnect.
 */
#include "sensor_sanity.h"
#include "safety.h"

/* ========================================================================= */
/*  Physical limits and max rates                                            */
/* ========================================================================= */
/* All x10 fixed-point except CO2. Rates are max-allowed delta per 100ms. */
#define TEMP_MIN_X10        (-200)   /* -20.0°C */
#define TEMP_MAX_X10        (800)    /* +80.0°C */
#define TEMP_MAX_DELTA_X10  (50)     /* 5.0°C / 100ms = 50°C/s (conservative) */

#define HUMID_MIN_X10       (0)
#define HUMID_MAX_X10       (1000)   /* 100.0% */
#define HUMID_MAX_DELTA_X10 (200)    /* 20%/100ms */

#define O2_MIN_X10          (0)
#define O2_MAX_X10          (1000)
#define O2_MAX_DELTA_X10    (100)    /* 10%/100ms */

#define CO2_MIN_PPM         (0)
#define CO2_MAX_PPM         (10000)
#define CO2_MAX_DELTA_PPM   (2000)

/* ========================================================================= */
/*  Sentinel "no previous sample" marker                                     */
/* ========================================================================= */
#define NO_PREV_I16  ((int16_t)0x7FFF)
#define NO_PREV_U16  ((uint16_t)0xFFFF)

static int16_t  s_prev_temp  = NO_PREV_I16;
static uint16_t s_prev_humid = NO_PREV_U16;
static uint16_t s_prev_o2    = NO_PREV_U16;
static uint16_t s_prev_co2   = NO_PREV_U16;

void sensor_sanity_init(void)
{
    s_prev_temp  = NO_PREV_I16;
    s_prev_humid = NO_PREV_U16;
    s_prev_o2    = NO_PREV_U16;
    s_prev_co2   = NO_PREV_U16;
}

/* ========================================================================= */
/*  Temperature                                                              */
/* ========================================================================= */
bool sensor_sanity_check_temp(int16_t temp_x10, bool valid)
{
    /* Invalid link: clear baseline, skip check (next good reading is fresh) */
    if (!valid || temp_x10 == -999) {
        s_prev_temp = NO_PREV_I16;
        return false;
    }

    /* Absolute range */
    if (temp_x10 < TEMP_MIN_X10 || temp_x10 > TEMP_MAX_X10) {
        safety_record(SAFETY_EVT_TEMP_IMPLAUSIBLE, (uint32_t)(int32_t)temp_x10);
        return false;
    }

    /* Rate check (only if we have a baseline) */
    if (s_prev_temp != NO_PREV_I16) {
        int32_t delta = (int32_t)temp_x10 - (int32_t)s_prev_temp;
        if (delta < 0) delta = -delta;
        if (delta > TEMP_MAX_DELTA_X10) {
            safety_record(SAFETY_EVT_TEMP_IMPLAUSIBLE,
                          ((uint32_t)(uint16_t)temp_x10) |
                          ((uint32_t)(uint16_t)s_prev_temp << 16));
            s_prev_temp = temp_x10;  /* still update baseline */
            return false;
        }
    }

    s_prev_temp = temp_x10;
    return true;
}

/* ========================================================================= */
/*  Humidity                                                                 */
/* ========================================================================= */
bool sensor_sanity_check_humid(uint16_t humid_x10, bool valid)
{
    if (!valid) {
        s_prev_humid = NO_PREV_U16;
        return false;
    }

    if (humid_x10 > HUMID_MAX_X10) {
        safety_record(SAFETY_EVT_HUMID_IMPLAUSIBLE, humid_x10);
        return false;
    }

    if (s_prev_humid != NO_PREV_U16) {
        int32_t delta = (int32_t)humid_x10 - (int32_t)s_prev_humid;
        if (delta < 0) delta = -delta;
        if (delta > HUMID_MAX_DELTA_X10) {
            safety_record(SAFETY_EVT_HUMID_IMPLAUSIBLE,
                          (uint32_t)humid_x10 | ((uint32_t)s_prev_humid << 16));
            s_prev_humid = humid_x10;
            return false;
        }
    }

    s_prev_humid = humid_x10;
    return true;
}

/* ========================================================================= */
/*  O2                                                                       */
/* ========================================================================= */
bool sensor_sanity_check_o2(uint16_t o2_x10, bool valid)
{
    if (!valid) {
        s_prev_o2 = NO_PREV_U16;
        return false;
    }

    if (o2_x10 > O2_MAX_X10) {
        safety_record(SAFETY_EVT_O2_IMPLAUSIBLE, o2_x10);
        return false;
    }

    if (s_prev_o2 != NO_PREV_U16) {
        int32_t delta = (int32_t)o2_x10 - (int32_t)s_prev_o2;
        if (delta < 0) delta = -delta;
        if (delta > O2_MAX_DELTA_X10) {
            safety_record(SAFETY_EVT_O2_IMPLAUSIBLE,
                          (uint32_t)o2_x10 | ((uint32_t)s_prev_o2 << 16));
            s_prev_o2 = o2_x10;
            return false;
        }
    }

    s_prev_o2 = o2_x10;
    return true;
}

/* ========================================================================= */
/*  CO2                                                                      */
/* ========================================================================= */
bool sensor_sanity_check_co2(uint16_t co2_ppm, bool valid)
{
    if (!valid) {
        s_prev_co2 = NO_PREV_U16;
        return false;
    }

    if (co2_ppm > CO2_MAX_PPM) {
        safety_record(SAFETY_EVT_CO2_IMPLAUSIBLE, co2_ppm);
        return false;
    }

    if (s_prev_co2 != NO_PREV_U16) {
        int32_t delta = (int32_t)co2_ppm - (int32_t)s_prev_co2;
        if (delta < 0) delta = -delta;
        if (delta > CO2_MAX_DELTA_PPM) {
            safety_record(SAFETY_EVT_CO2_IMPLAUSIBLE,
                          (uint32_t)co2_ppm | ((uint32_t)s_prev_co2 << 16));
            s_prev_co2 = co2_ppm;
            return false;
        }
    }

    s_prev_co2 = co2_ppm;
    return true;
}
