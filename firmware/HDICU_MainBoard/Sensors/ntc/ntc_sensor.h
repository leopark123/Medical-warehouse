/**
 * @file    ntc_sensor.h
 * @brief   NTC thermistor temperature calculation
 * @note    4 channels: PA0(CH0), PA1(CH1), PA4(CH4), PA5(CH5)
 *          NTC: 10kΩ@25°C, B=3950, pullup 10kΩ to 3.3V
 *          ADC: 12-bit (0~4095)
 *          Formula: T(°C) = 1/(1/298.15 + ln(Rntc/10000)/3950) - 273.15
 */

#ifndef NTC_SENSOR_H
#define NTC_SENSOR_H

#include <stdint.h>

#define NTC_CHANNEL_COUNT   4

/* Convert raw 12-bit ADC value to temperature in 0.1°C (x10) */
int16_t ntc_adc_to_temp_x10(uint16_t adc_value);

/* Process 4 channels of ADC data and return average temperature (x10) */
int16_t ntc_calc_average(const uint16_t adc_values[NTC_CHANNEL_COUNT],
                         int16_t temp_out[NTC_CHANNEL_COUNT]);

#endif
