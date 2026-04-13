/**
 * @file    ntc_sensor.c
 * @brief   NTC temperature calculation using Steinhart-Hart simplified formula
 */

#include "ntc_sensor.h"
#include <math.h>

#define NTC_R_REF       10000.0f    /* Pullup resistor: 10kΩ */
#define NTC_R0          10000.0f    /* NTC nominal: 10kΩ @ 25°C */
#define NTC_T0_K        298.15f     /* 25°C in Kelvin */
#define NTC_B           3950.0f     /* B-value */
#define NTC_ADC_MAX     4095.0f     /* 12-bit ADC */

int16_t ntc_adc_to_temp_x10(uint16_t adc_value)
{
    if (adc_value < 10 || adc_value > 4085) {
        return -999;    /* Invalid: open(>4085) or shorted(<10).
                         * ADC=4094 with NTC disconnected gives -90°C → false heating.
                         * Widen deadband to catch near-rail readings. */
    }

    /* R_ntc = R_ref * ADC / (4095 - ADC) */
    float r_ntc = NTC_R_REF * (float)adc_value / (NTC_ADC_MAX - (float)adc_value);

    /* T(K) = 1 / (1/T0 + (1/B) * ln(R_ntc/R0)) */
    float t_kelvin = 1.0f / (1.0f / NTC_T0_K + (1.0f / NTC_B) * logf(r_ntc / NTC_R0));

    /* T(°C) = T(K) - 273.15, then x10 for 0.1°C resolution */
    float t_celsius = t_kelvin - 273.15f;

    return (int16_t)(t_celsius * 10.0f);
}

int16_t ntc_calc_average(const uint16_t adc_values[NTC_CHANNEL_COUNT],
                         int16_t temp_out[NTC_CHANNEL_COUNT])
{
    int32_t sum = 0;
    int count = 0;

    for (int i = 0; i < NTC_CHANNEL_COUNT; i++) {
        temp_out[i] = ntc_adc_to_temp_x10(adc_values[i]);
        if (temp_out[i] != -999) {
            sum += temp_out[i];
            count++;
        }
    }

    if (count == 0) return -999;   /* CRITICAL: all 4 NTC invalid — must return -999
                                     * so temp_control detects sensor failure and holds state.
                                     * Returning 0 would be misread as 0.0°C → runaway heating */
    return (int16_t)(sum / count);
}
