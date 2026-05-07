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

/* Process 4 channels of ADC data and return average temperature (x10).
 * Stage 8 (2026-05-06): 输入 = 4 路 ADC raw, 内部 push 入 3s 滑动平均环 (30×100ms),
 * 输出 temp_out[] 和返回值都是滤波后值 (-999 = sensor invalid).
 * 必须先调用 ntc_filter_init() 否则首次读到 0°C (BSS 初值). */
int16_t ntc_calc_average(const uint16_t adc_values[NTC_CHANNEL_COUNT],
                         int16_t temp_out[NTC_CHANNEL_COUNT]);

/* Stage 8 (2026-05-06): 显式初始化 NTC 滤波环为 -999 (invalid sentinel).
 * 必须在 SensorTask 第一次调用 ntc_calc_average 之前调用一次.
 * 不调用的后果: BSS 初值 0 → 启动阶段 warmup 期间输出会被 0°C 拖低. */
void ntc_filter_init(void);

#endif
