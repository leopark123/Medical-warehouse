/**
 * @file    temp_control.h
 * @brief   Temperature control state machine (hysteresis ±1°C)
 *          Idle:    setpoint-1°C < actual < setpoint+1°C → all OFF
 *          Cooling: actual > setpoint+1°C → compressor + inner fan
 *          Heating: actual < setpoint-1°C → PTC + PTC fan
 */
#ifndef TEMP_CONTROL_H
#define TEMP_CONTROL_H
#include "app_data.h"
void temp_control_update(AppData_t *d);
#endif
