/**
 * @file    uart_driver.h
 * @brief   UART driver — init + transmit + ISR routing for 5 UARTs
 */

#ifndef UART_DRIVER_H
#define UART_DRIVER_H

#include <stdint.h>

typedef enum {
    UART_CH_SCREEN = 0,     /* UART1: PA9/PA10, 115200 */
    UART_CH_IPAD,           /* UART2: PA2/PA3, 115200 */
    UART_CH_CO2,            /* UART3: PB10/PB11, 9600 */
    UART_CH_O2,             /* UART4: PC10/PC11, 9600 */
    UART_CH_JFC103,         /* UART5: PC12/PD2, 38400 */
    UART_CH_COUNT
} UartChannel_t;

/* Initialize all 5 UARTs (GPIO + baud rate). Does NOT enable RX interrupts. */
void uart_driver_init(void);

/* Enable UART RX interrupts. Call AFTER FreeRTOS scheduler has started. */
void uart_driver_start_rx(void);

/* Transmit data on specified channel (blocking) */
void uart_driver_send(UartChannel_t ch, const uint8_t *data, uint16_t len);

/* Weak callback — override to route received bytes to protocol/sensor parsers.
 * Called from ISR context! */
void uart_rx_callback(UartChannel_t ch, uint8_t byte);

/* Force-recover UART_CH_SCREEN — DeInit+Init+re-arm RX.
 * Call from task context when screen RX appears stuck. */
void uart_driver_recover_screen(void);

/* Diagnostic counters (volatile, read from any context) */
extern volatile uint32_t g_uart_rx_ok[UART_CH_COUNT];
extern volatile uint32_t g_uart_rx_err[UART_CH_COUNT];
extern volatile uint32_t g_uart_rx_rearm_fail[UART_CH_COUNT];
extern volatile uint32_t g_uart_last_errcode[UART_CH_COUNT];
extern volatile uint8_t  g_uart_screen_recover_pending;

#endif
