/**
 * @file    adc_driver.h
 * @brief   ADC driver for 4-channel NTC temperature reading
 * @note    ADC1: CH0(PA0), CH1(PA1), CH4(PA4), CH5(PA5)
 *          12-bit resolution, 3.3V reference
 */

#ifndef ADC_DRIVER_H
#define ADC_DRIVER_H

#include <stdint.h>

#define ADC_CHANNEL_COUNT   4

/* Initialize ADC1 for 4 NTC channels */
void adc_driver_init(void);

/* Read all 4 channels (blocking). Results in adc_values[0..3] */
void adc_driver_read_all(uint16_t adc_values[ADC_CHANNEL_COUNT]);

/* Read single channel by index (0-3) */
uint16_t adc_driver_read_channel(uint8_t ch_idx);

#endif
