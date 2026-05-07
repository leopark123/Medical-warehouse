/**
 * @file    sensor_sanity.h
 * @brief   Sensor plausibility & rate-of-change checks
 *
 *          Purpose: detect sensor hardware faults (wire loose, ADC stuck,
 *          module crash) that manifest as plausible-looking but impossible
 *          values. Complements the existing 'valid' flag (which only tracks
 *          communication timeout).
 *
 *          Checks per sensor:
 *            1. Absolute range — value must lie in a physically realistic band
 *            2. Rate-of-change — delta per 100ms within physically possible
 *               (e.g. temperature can't jump 10°C in 100ms)
 *
 *          On failure:
 *            - Event recorded via safety_record() (non-fatal, ring buffer)
 *            - Returns false so caller can mark suspect / raise alarm
 *            - AppData sensor.*_valid is NOT modified here (caller's choice)
 *
 *          The `valid` parameter tells the checker whether the upstream link
 *          is alive. On valid=0 → valid=1 transitions the rate-check baseline
 *          is reset to avoid spurious rate violations after comms dropout.
 *
 *          Thresholds follow frozen spec 3.x sensor ranges + engineering
 *          safety margins. Tune per field experience.
 */
#ifndef SENSOR_SANITY_H
#define SENSOR_SANITY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at startup to init internal "previous value" cache */
void sensor_sanity_init(void);

/* Check each sensor. Returns true if value passes both range + rate check.
 * Call once per SensorTask iteration (100ms) AFTER calibration is applied.
 *
 * `valid` = communication/acquisition status from the sensor driver.
 * When false, the check is skipped and the internal baseline is cleared so
 * the next valid reading does not trip a spurious rate violation.
 *
 * Return false → caller should NOT trust this reading for control decisions
 * (but the reading is already stored in AppData; this is advisory).
 *
 * Failures are auto-logged to safety_record() with context = raw value. */
bool sensor_sanity_check_temp(int16_t temp_x10, bool valid);    /* range [-200, +800] = -20~+80℃, rate 5℃/s */
bool sensor_sanity_check_humid(uint16_t humid_x10, bool valid); /* range [0, 1000] = 0~100%, rate 20%/s */
bool sensor_sanity_check_o2(uint16_t o2_x10, bool valid);       /* range [0, 1000] = 0~100%, rate 10%/s */
bool sensor_sanity_check_co2(uint16_t co2_ppm, bool valid);     /* range [0, 10000] ppm, rate 2000/s */

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_SANITY_H */
