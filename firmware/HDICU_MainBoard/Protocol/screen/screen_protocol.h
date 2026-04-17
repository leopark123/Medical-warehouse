/**
 * @file    screen_protocol.h
 * @brief   Dual-board screen communication protocol
 * @note    Frame: AA 55 [CMD] [LEN] [DATA...] [CS] ED
 *          CS = (CMD+LEN+DATA) & 0xFF (does NOT include header)
 *          Refresh: 100ms display data, 1s heartbeat
 */

#ifndef SCREEN_PROTOCOL_H
#define SCREEN_PROTOCOL_H

#include "protocol_defs.h"
#include "app_data.h"

#ifdef __cplusplus
extern "C" {
#endif

void screen_protocol_init(void);

/* Feed received byte from UART1 (screen board → main controller) */
void screen_protocol_rx_byte(uint8_t byte);

/* Send 0x01 display data packet (call every 100ms) */
void screen_send_display_data(void);

/* Send 0x04 heartbeat (call every 1s) */
void screen_send_heartbeat(void);

/* Periodic tick for heartbeat timeout detection */
void screen_protocol_tick(uint32_t now_ms);

bool screen_protocol_is_connected(void);

/* Return tick of last successfully parsed frame (0 if none yet) */
uint32_t screen_protocol_last_frame_tick(void);

/* Send encoder event to screen board (0x06 command)
 * event_type: 0x01=push click, 0x02=push long, 0x03=rotation
 * delta: signed rotation clicks (for type 0x03) */
void screen_send_encoder_event(uint8_t event_type, int8_t delta);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_PROTOCOL_H */
