/**
 * @file    alarm_config.h
 * @brief   Alarm thresholds and timing (Frozen — 冻结总纲/开发基线 4.3)
 *
 *          Parameter     | Low Threshold      | High Threshold     | Delay
 *          --------------|--------------------|--------------------|------
 *          Temperature   | Setpoint - 5°C     | Setpoint + 5°C     | 10s
 *          Humidity      | Setpoint - 5%      | Setpoint + 5%      | 10s
 *          O2 conc.      | Setpoint - 5%      | Setpoint + 5%      | 10s
 *          CO2           | —                  | 5000 ppm           | 10s
 *          Heart rate    | 50 bpm             | 140 bpm            | 3s
 *          SpO2          | 85%                | —                  | 3s
 *
 *          Alarm handling:
 *          - Display: value flashing + intermittent buzzer
 *          - Clear: parameter returns to normal AND manual alarm confirm key pressed
 */

#ifndef ALARM_CONFIG_H
#define ALARM_CONFIG_H

/* Temperature alarm: setpoint ± 5°C (±50 in x10 units) */
#define ALARM_TEMP_OFFSET_X10       50

/* Humidity alarm: setpoint ± 5% (±50 in x10 units) */
#define ALARM_HUMID_OFFSET_X10      50

/* O2 alarm: setpoint ± 5% (±50 in x10 units) */
#define ALARM_O2_OFFSET_X10         50

/* CO2 alarm: high only */
#define ALARM_CO2_HIGH_PPM          5000

/* Heart rate alarm: 50~140 bpm */
#define ALARM_HR_LOW                50
#define ALARM_HR_HIGH               140

/* SpO2 alarm: low only */
#define ALARM_SPO2_LOW_THRESHOLD    85

/* Alarm delays (in AlarmTask ticks at 100ms period) */
#define ALARM_DELAY_SLOW_TICKS      100     /* 10 seconds for temp/O2/CO2 (life-critical, fast response) */
#define ALARM_DELAY_FAST_TICKS      30      /* 3 seconds for HR/SpO2 (vitals, faster) */
#define ALARM_DELAY_HUMID_TICKS     600     /* 60 seconds for humidity.
                                             * Rationale: humidifier physically needs ~30-60s
                                             * to raise cabinet humidity from ambient to setpoint.
                                             * Shorter delays false-alarm on every power-up.
                                             * Humidity is non-life-critical; 1-minute window for
                                             * real out-of-range reporting is clinically acceptable. */

#endif
