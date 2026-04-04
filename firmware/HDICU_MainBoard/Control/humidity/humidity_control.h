/**
 * @file    humidity_control.h
 * @brief   Humidity control (hysteresis ±3%)
 *          Idle: setpoint-3% < actual < setpoint+3%
 *          Humidify: actual < setpoint-3% → humidifier ON
 *          Dehumidify: actual > setpoint+3% → engage cooling
 */
#ifndef HUMIDITY_CONTROL_H
#define HUMIDITY_CONTROL_H
#include "app_data.h"
void humidity_control_update(AppData_t *d);
#endif
