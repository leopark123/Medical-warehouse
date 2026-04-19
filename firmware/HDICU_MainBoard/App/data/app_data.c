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

    /* v2.1: 校准值默认0 (不校准) */
    g_app_data.calibration.temp  = 0;
    g_app_data.calibration.humid = 0;
    g_app_data.calibration.o2    = 0;
    g_app_data.calibration.co2   = 0;

    /* v2.1: 出厂限值默认范围 (硬编码, Flash首次上电或恢复出厂后使用) */
    g_app_data.limits.temp_upper      = 400;    /* 40.0°C */
    g_app_data.limits.temp_lower      = 100;    /* 10.0°C */
    g_app_data.limits.humid_upper     = 900;    /* 90% */
    g_app_data.limits.humid_lower     = 300;    /* 30% */
    g_app_data.limits.o2_upper        = 1000;   /* 100% */
    g_app_data.limits.o2_lower        = 210;    /* 21% */
    g_app_data.limits.uv_upper        = 3600;   /* 60 min */
    g_app_data.limits.infrared_upper  = 3600;   /* 60 min (预留, 主板暂不控制) */
    g_app_data.limits.fog_upper       = 3600;   /* 60 min */

    /* v2.1: 取消标志默认全启用 (不保存Flash, 重启恢复默认) */
    g_app_data.cancel_flags.temp  = 1;
    g_app_data.cancel_flags.humid = 1;
    g_app_data.cancel_flags.o2    = 1;
    g_app_data.cancel_flags.co2   = 1;
}

void app_data_lock(void)
{
    taskENTER_CRITICAL();
}

void app_data_unlock(void)
{
    taskEXIT_CRITICAL();
}
