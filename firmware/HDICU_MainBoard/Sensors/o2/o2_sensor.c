/**
 * @file    o2_sensor.c
 * @brief   OCS-3RL-2.0 O2 sensor — parse 12-byte auto-reported frames
 *
 *          Frame format (confirmed from OCS-3RL-2.0 user manual page 5):
 *          Byte[0]  = 0x16 (header 1)
 *          Byte[1]  = 0x09 (header 2)
 *          Byte[2]  = 0x01 (PSA+air mode) or 0x02 (pure O2+air mode)
 *          Byte[3-4]  = O2 concentration (uint16, ÷10 = %)
 *          Byte[5-6]  = Humidity (uint16, ÷10 = %RH)
 *          Byte[7-8]  = Temperature (uint16, ÷10 = °C)
 *          Byte[9-10] = Pressure (uint16, ÷10 = kPa)
 *          Byte[11] = Checksum: sum of all 12 bytes must equal 0x00 (low 8 bits)
 *
 *          Auto-report: 2 frames/second (500ms interval)
 *          All values ÷10 for actual reading
 */

#include "o2_sensor.h"
#include <string.h>

#define O2_FRAME_LEN    12
#define O2_HEAD1        0x16
#define O2_HEAD2        0x09
#define O2_MODE_PSA     0x01
#define O2_MODE_PURE    0x02

static uint8_t s_buf[O2_FRAME_LEN];
static uint8_t s_idx;
static O2SensorData_t s_data;
static bool s_valid;

void o2_sensor_init(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    s_idx = 0;
    memset(&s_data, 0, sizeof(s_data));
    s_valid = false;
}

void o2_sensor_rx_byte(uint8_t byte)
{
    /* Frame sync: byte[0] must be 0x16 */
    if (s_idx == 0) {
        if (byte == O2_HEAD1) {
            s_buf[s_idx++] = byte;
        }
        return;
    }

    /* byte[1] must be 0x09 */
    if (s_idx == 1) {
        if (byte == O2_HEAD2) {
            s_buf[s_idx++] = byte;
        } else {
            s_idx = 0;     /* Resync */
        }
        return;
    }

    /* byte[2] must be 0x01 or 0x02 (mode) */
    if (s_idx == 2) {
        if (byte == O2_MODE_PSA || byte == O2_MODE_PURE) {
            s_buf[s_idx++] = byte;
        } else {
            s_idx = 0;     /* Resync */
        }
        return;
    }

    /* Accumulate remaining bytes */
    s_buf[s_idx++] = byte;

    if (s_idx >= O2_FRAME_LEN) {
        /* Validate checksum: sum of all 12 bytes, low 8 bits must be 0x00 */
        uint8_t checksum = 0;
        for (int i = 0; i < O2_FRAME_LEN; i++) {
            checksum += s_buf[i];
        }

        if (checksum == 0x00) {
            /* Parse data — all values are uint16 big-endian, ÷10 for actual */
            s_data.o2_raw       = ((uint16_t)s_buf[3] << 8) | s_buf[4];
            s_data.humidity_raw = ((uint16_t)s_buf[5] << 8) | s_buf[6];
            s_data.temp_raw     = ((uint16_t)s_buf[7] << 8) | s_buf[8];
            s_data.pressure_raw = ((uint16_t)s_buf[9] << 8) | s_buf[10];
            s_valid = true;
        }
        /* else: checksum fail, discard */

        s_idx = 0;
    }
}

O2SensorData_t o2_sensor_get_data(void) { return s_data; }
bool o2_sensor_is_valid(void) { return s_valid; }
