/**
 * @file    main_jfc103_test2.c
 * @brief   JFC103 test — continuously send 0x8A every 200ms, listen for response.
 *          Previous observation: module LED briefly on when UART5 TX was active.
 *          Theory: module needs continuous RX activity to stay "connected".
 */
#include "stm32f1xx_hal.h"
#include <string.h>

void SysTick_Handler(void) { HAL_IncTick(); }

static UART_HandleTypeDef huart4, huart5;

static void uart4_send(const char *s)
{
    HAL_UART_Transmit(&huart4, (uint8_t *)s, strlen(s), 200);
}

static void uart4_hex(uint8_t val)
{
    const char hex[] = "0123456789ABCDEF";
    char buf[3] = { hex[val >> 4], hex[val & 0x0F], ' ' };
    HAL_UART_Transmit(&huart4, (uint8_t *)buf, 3, 50);
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

    gpio.Pin = GPIO_PIN_10;  /* UART4 TX = PC10 */
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_12;  /* UART5 TX = PC12 */
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin = GPIO_PIN_2;   /* UART5 RX = PD2 */
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOD, &gpio);

    /* UART4 at 9600 for monitoring via USB-TTL on CN16 */
    huart4.Instance = UART4;
    huart4.Init.BaudRate = 9600;
    huart4.Init.WordLength = UART_WORDLENGTH_8B;
    huart4.Init.StopBits = UART_STOPBITS_1;
    huart4.Init.Parity = UART_PARITY_NONE;
    huart4.Init.Mode = UART_MODE_TX;
    huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart4);

    /* UART5 at 38400 for JFC103 */
    huart5.Instance = UART5;
    huart5.Init.BaudRate = 38400;
    huart5.Init.WordLength = UART_WORDLENGTH_8B;
    huart5.Init.StopBits = UART_STOPBITS_1;
    huart5.Init.Parity = UART_PARITY_NONE;
    huart5.Init.Mode = UART_MODE_TX_RX;
    huart5.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart5);

    uart4_send("\r\n=== JFC103 Test v2 ===\r\n");
    uart4_send("Sending 0x8A every 200ms. Listening...\r\n\r\n");

    uint8_t start_cmd = 0x8A;
    uint8_t rx_byte;
    uint32_t send_count = 0;
    uint32_t byte_count = 0;
    uint32_t frame_count = 0;

    while (1) {
        /* Send 0x8A */
        HAL_UART_Transmit(&huart5, &start_cmd, 1, 50);
        send_count++;

        /* Print send count every 5 sends (1s) */
        if (send_count % 5 == 0) {
            uart4_send("[tx] 0x8A x");
            char num[6]; int nl = 0;
            uint32_t v = send_count;
            if (v == 0) num[nl++] = '0';
            else while (v > 0) { num[nl++] = '0' + (v % 10); v /= 10; }
            char rev[6]; for (int i = 0; i < nl; i++) rev[i] = num[nl-1-i];
            HAL_UART_Transmit(&huart4, (uint8_t *)rev, nl, 50);
            uart4_send("\r\n");
        }

        /* Listen for ~150ms (leave 50ms for the 0x8A send cycle) */
        uint32_t listen_start = HAL_GetTick();
        while (HAL_GetTick() - listen_start < 150) {
            if (HAL_UART_Receive(&huart5, &rx_byte, 1, 10) == HAL_OK) {
                if (rx_byte == 0xFF && byte_count > 0) {
                    uart4_send("\r\n--- Frame ");
                    char fn[4]; int fl = 0;
                    uint32_t fv = frame_count;
                    if (fv == 0) fn[fl++] = '0';
                    else while (fv > 0) { fn[fl++] = '0' + (fv % 10); fv /= 10; }
                    char fr[4]; for (int i = 0; i < fl; i++) fr[i] = fn[fl-1-i];
                    HAL_UART_Transmit(&huart4, (uint8_t *)fr, fl, 50);
                    uart4_send(" ---\r\nFF ");
                    frame_count++;
                    byte_count = 1;
                } else {
                    uart4_hex(rx_byte);
                    byte_count++;
                    if (byte_count % 16 == 0) uart4_send("\r\n");
                }
            }
        }
    }
}
