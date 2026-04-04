/**
 * @file    co2_sensor.h
 * @brief   MWD1006E CO2 sensor driver
 * @note    UART3 9600bps, auto-report every 4 seconds
 *          Frame: FF 17 04 00 [CO2_H] [CO2_L] 00 00 [CS]
 *          CS = (~(sum of bytes 1~7) + 1) & 0xFF
 *          CO2 = (CO2_H << 8) | CO2_L, range 0~5000 ppm
 */

#ifndef CO2_SENSOR_H
#define CO2_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

void co2_sensor_init(void);
void co2_sensor_rx_byte(uint8_t byte);
uint16_t co2_sensor_get_ppm(void);
bool co2_sensor_is_valid(void);

#endif
