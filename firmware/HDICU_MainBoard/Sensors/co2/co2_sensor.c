/**
 * @file    co2_sensor.c
 * @brief   MWD1006E CO2 sensor — parse auto-reported frames
 * @note    Auto-report every 4 seconds. If no valid frame for >10s, mark invalid.
 */

#include "co2_sensor.h"
#include "stm32f1xx_hal.h"
#include <string.h>

#define CO2_FRAME_LEN       9
#define CO2_FRAME_HEAD      0xFF
#define CO2_CMD_BYTE        0x17
#define CO2_TIMEOUT_MS      10000   /* 10 seconds: >2× the 4s report interval */

static uint8_t s_buf[CO2_FRAME_LEN];
static uint8_t s_idx;
static uint16_t s_co2_ppm;
static uint32_t s_last_valid_tick;
static bool s_valid;

void co2_sensor_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_idx = 0;
    s_co2_ppm = 0;
    s_last_valid_tick = 0;
    s_valid = false;
}

void co2_sensor_rx_byte(uint8_t byte)
{
    if (s_idx == 0) {
        if (byte == CO2_FRAME_HEAD) {
            s_buf[s_idx++] = byte;
        }
        return;
    }

    s_buf[s_idx++] = byte;

    if (s_idx >= CO2_FRAME_LEN) {
        if (s_buf[1] == CO2_CMD_BYTE) {
            uint8_t sum = 0;
            for (int i = 1; i <= 7; i++) {
                sum += s_buf[i];
            }
            uint8_t cs_calc = (~sum) + 1;

            if (cs_calc == s_buf[8]) {
                s_co2_ppm = ((uint16_t)s_buf[4] << 8) | s_buf[5];
                s_last_valid_tick = HAL_GetTick();
                s_valid = true;
            }
        }
        s_idx = 0;
    }
}

uint16_t co2_sensor_get_ppm(void) { return s_co2_ppm; }

bool co2_sensor_is_valid(void)
{
    /* Mark invalid if no valid frame received for >10 seconds */
    if (s_valid && (HAL_GetTick() - s_last_valid_tick > CO2_TIMEOUT_MS)) {
        s_valid = false;
    }
    return s_valid;
}
