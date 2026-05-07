/**
 * @file    jfc103_sensor.h
 * @brief   JFC103 vital signs module driver
 * @note    UART5 38400bps, 88 bytes/frame, 1.28s period
 *          Start command: send 0x8A
 *          Heart rate: Byte 65, SpO2: Byte 66
 *          Blood pressure / respiration rate: NOT supported by JFC103.
 */

#ifndef JFC103_SENSOR_H
#define JFC103_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

void jfc103_sensor_init(void);

/* Begin adaptive start sequence (sends 0x8A until data flows, then stops) */
void jfc103_sensor_start(void);

/* Call periodically (~100ms) to manage adaptive 0x8A keepalive */
void jfc103_sensor_tick(void);

/* Feed received byte from UART5 */
void jfc103_sensor_rx_byte(uint8_t byte);

/* Get latest readings */
uint8_t jfc103_get_heart_rate(void);    /* bpm, 0=invalid */
uint8_t jfc103_get_spo2(void);          /* %, 0=invalid */
bool    jfc103_is_valid(void);

#endif
