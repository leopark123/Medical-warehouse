/**
 * @file    o2_sensor.h
 * @brief   OCS-3RL-2.0 O2 sensor driver
 * @note    UART4 9600bps, auto-report 2 frames/second, 12 bytes per frame
 *          All values ÷10 for actual reading (O2 in 0.1%, humidity, temp, pressure)
 */

#ifndef O2_SENSOR_H
#define O2_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint16_t o2_raw;        /* x10 (e.g. 209 = 20.9%) */
    uint16_t humidity_raw;  /* x10 */
    uint16_t temp_raw;      /* x10 */
    uint16_t pressure_raw;  /* x10 */
} O2SensorData_t;

void o2_sensor_init(void);
void o2_sensor_rx_byte(uint8_t byte);
O2SensorData_t o2_sensor_get_data(void);
bool o2_sensor_is_valid(void);

#endif
