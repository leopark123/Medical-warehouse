/**
 * @file    main_jfc103_test3.c
 * @brief   JFC103 test v4 — adaptive 0x8A: start until data flows, then stop.
 *
 * Previous finding: continuous 0x8A every 2s resets module algorithm,
 * HR/SpO2 never converge. Need to let module run uninterrupted.
 *
 * Strategy:
 * Phase 1: Send 0x8A every 200ms until first valid frame received
 * Phase 2: Stop sending. Module should keep transmitting on its own.
 * Phase 3: If no data for 10s, re-send 0x8A to recover.
 */
#include "stm32f1xx_hal.h"
#include <string.h>

void SysTick_Handler(void) { HAL_IncTick(); }

static UART_HandleTypeDef huart4, huart5;

static void uart4_send(const char *s)
{
    HAL_UART_Transmit(&huart4, (uint8_t *)s, strlen(s), 500);
}

static void uart4_num(uint32_t v)
{
    char num[10]; int nl = 0;
    if (v == 0) num[nl++] = '0';
    else while (v > 0) { num[nl++] = '0' + (v % 10); v /= 10; }
    char rev[10]; for (int i = 0; i < nl; i++) rev[i] = num[nl-1-i];
    HAL_UART_Transmit(&huart4, (uint8_t *)rev, nl, 50);
}

static void uart4_hex_buf(const uint8_t *buf, uint16_t len)
{
    const char hex[] = "0123456789ABCDEF";
    for (uint16_t i = 0; i < len; i++) {
        char out[3] = { hex[buf[i] >> 4], hex[buf[i] & 0x0F], ' ' };
        HAL_UART_Transmit(&huart4, (uint8_t *)out, 3, 50);
        if ((i + 1) % 16 == 0) uart4_send("\r\n  ");
    }
}

int main(void)
{
    HAL_Init();
    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_UART5_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOD, &gpio);

    huart4.Instance = UART4;
    huart4.Init.BaudRate = 9600;
    huart4.Init.WordLength = UART_WORDLENGTH_8B;
    huart4.Init.StopBits = UART_STOPBITS_1;
    huart4.Init.Parity = UART_PARITY_NONE;
    huart4.Init.Mode = UART_MODE_TX;
    huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart4);

    huart5.Instance = UART5;
    huart5.Init.BaudRate = 38400;
    huart5.Init.WordLength = UART_WORDLENGTH_8B;
    huart5.Init.StopBits = UART_STOPBITS_1;
    huart5.Init.Parity = UART_PARITY_NONE;
    huart5.Init.Mode = UART_MODE_TX_RX;
    huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart5);

    uart4_send("\r\n=== JFC103 Test v4 (adaptive) ===\r\n");
    uart4_send("Phase 1: 0x8A until data. Phase 2: listen only.\r\n\r\n");

    uint8_t cmd = 0x8A;
    uint8_t frame_buf[88];
    uint32_t frame_count = 0;
    uint32_t last_data_time = 0;
    uint32_t last_send_time = 0;
    uint8_t phase = 1;  /* 1=starting, 2=listening */

    while (1) {
        /* Phase 1: send 0x8A every 200ms until first data */
        if (phase == 1) {
            if (HAL_GetTick() - last_send_time >= 200) {
                HAL_UART_Transmit(&huart5, &cmd, 1, 10);
                last_send_time = HAL_GetTick();
            }
        }

        /* Phase 2: if no data for 10s, go back to phase 1 */
        if (phase == 2 && last_data_time > 0) {
            if (HAL_GetTick() - last_data_time > 10000) {
                uart4_send("[TIMEOUT] No data 10s, re-sending 0x8A...\r\n");
                phase = 1;
            }
        }

        /* Try to receive 0xFF header */
        uint8_t rx;
        if (HAL_UART_Receive(&huart5, &rx, 1, 5) == HAL_OK) {
            if (rx == 0xFF) {
                /* Got frame header — receive remaining 87 bytes */
                frame_buf[0] = 0xFF;
                uint16_t got = 1;
                if (HAL_UART_Receive(&huart5, &frame_buf[1], 87, 50) == HAL_OK) {
                    got = 88;
                } else {
                    got = 88 - huart5.RxXferCount;
                }

                last_data_time = HAL_GetTick();

                /* Switch to phase 2 after first valid frame */
                if (phase == 1 && got >= 88) {
                    phase = 2;
                    uart4_send("[Phase 2] Data flowing, stopped sending 0x8A\r\n");
                }

                /* Skip all-zero frames (likely caused by residual 0x8A interference) */
                uint8_t is_zero = 1;
                for (int i = 1; i < 10; i++) {
                    if (frame_buf[i] != 0) { is_zero = 0; break; }
                }
                if (is_zero && got >= 88) {
                    continue;  /* Skip zero frame, don't print */
                }

                /* Print frame summary */
                uart4_send("F");
                uart4_num(frame_count);
                uart4_send(" (");
                uart4_num(got);
                uart4_send("B) HR=");
                if (got >= 66) uart4_num(frame_buf[65]);
                else uart4_send("?");
                uart4_send(" SpO2=");
                if (got >= 67) uart4_num(frame_buf[66]);
                else uart4_send("?");
                uart4_send(" bk=");
                if (got >= 68) uart4_num(frame_buf[67]);
                else uart4_send("?");

                /* Print blood pressure if available (byte 71=systolic, 72=diastolic) */
                if (got >= 73) {
                    uart4_send(" BP=");
                    uart4_num(frame_buf[71]);
                    uart4_send("/");
                    uart4_num(frame_buf[72]);
                }

                uart4_send(" ph=");
                uart4_num(phase);
                uart4_send("\r\n");

                /* Print first 20 bytes of waveform for debug */
                if (frame_count % 10 == 0) {
                    uart4_send("  wave: ");
                    uart4_hex_buf(frame_buf + 1, 20);
                    uart4_send("\r\n");
                }

                frame_count++;
            }
        }
    }
}
