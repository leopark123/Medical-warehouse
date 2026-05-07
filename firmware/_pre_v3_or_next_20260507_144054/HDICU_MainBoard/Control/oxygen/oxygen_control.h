/**
 * @file    oxygen_control.h
 * @brief   O2 concentration control (hysteresis -2%)
 *          Idle: actual >= setpoint → valve OFF
 *          Supplying: actual < setpoint-2% → valve ON
 */
#ifndef OXYGEN_CONTROL_H
#define OXYGEN_CONTROL_H
#include "app_data.h"
void oxygen_control_update(AppData_t *d);
#endif
