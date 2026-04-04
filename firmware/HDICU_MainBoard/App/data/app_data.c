/**
 * @file    app_data.c
 * @brief   Central data hub — initialization, access, and concurrency
 * @note    Power-on defaults per frozen spec 4.5:
 *          - Target temp/humidity/O2 = current sensor reading (set after first sensor poll)
 *          - Fan speed = 0 (off)
 *          - All lights = off
 *          - Cycle mode = 外循环 (outer cycle, inner_cycle=0)
 *          - No power-cycle memory for setpoints
 */

#include "app_data.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

static AppData_t g_app_data;

AppData_t* app_data_get(void)
{
    return &g_app_data;
}

void app_data_init(void)
{
    memset(&g_app_data, 0, sizeof(g_app_data));

    g_app_data.setpoint.target_temp     = 250;  /* 25.0°C */
    g_app_data.setpoint.target_humidity = 500;  /* 50.0% */
    g_app_data.setpoint.target_o2       = 210;  /* 21.0% */
    g_app_data.setpoint.target_co2      = 1000; /* 1000 ppm */
    g_app_data.setpoint.fan_speed       = 0;
    g_app_data.setpoint.nursing_level   = 1;
    g_app_data.setpoint.inner_cycle     = 0;
    g_app_data.setpoint.fresh_air       = 0;
    g_app_data.setpoint.open_o2         = 0;
    g_app_data.setpoint.light_ctrl      = 0x00;

    g_app_data.control.nursing_level_actual = 1;
}

void app_data_lock(void)
{
    taskENTER_CRITICAL();
}

void app_data_unlock(void)
{
    taskEXIT_CRITICAL();
}
