/**
 * @file    ipad_protocol.h
 * @brief   iPad APP communication protocol handler
 * @note    Implements: 0x01/0x02, 0x03/0x04, 0x05/0x06, 0xFF
 *          Reserved:   0x07/0x08 (not implemented this phase)
 *
 *          Timing: APP queries every 500ms, response must be within 100ms.
 *          Disconnect: no interaction for 3 seconds.
 *          Write: 22 bytes full overwrite, any param OOB => reject all.
 */

#ifndef IPAD_PROTOCOL_H
#define IPAD_PROTOCOL_H

#include "protocol_defs.h"
#include "app_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the iPad protocol handler */
void ipad_protocol_init(void);

/* Feed received byte from UART2. Internally parses frame and triggers response. */
void ipad_protocol_rx_byte(uint8_t byte);

/* Periodic check — call from CommIPadTask or timer.
 * Detects disconnect (>3s no interaction). */
void ipad_protocol_tick(uint32_t now_ms);

/* Check if iPad is connected */
bool ipad_protocol_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* IPAD_PROTOCOL_H */
